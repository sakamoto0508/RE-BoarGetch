#include "Misc/AutomationTest.h"
#include "ACFCameraProfiler.h" // defines ACF_WITH_PROFILER

#if WITH_DEV_AUTOMATION_TESTS && ACF_WITH_PROFILER

#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSubsystem.h"
#include "ACFSequencerBlueprintLibrary.h"
#include "ACFSequencerCameraSubsystem.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "ACFCameraHealth.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "ACFCameraTestEnvironment.h"

/**
 * Phase 6B / B-1 performance baseline. NOT a regression test: it only runs when the command line has -ACFProfile and
 * the map is /ActionCameraFramework/Samples/L_ACFCameraTest, and writes its results to Saved/ACFProfile/:
 *   UnrealEditor-Cmd <project> /ActionCameraFramework/Samples/L_ACFCameraTest -game -NullRHI -ACFProfile
 *     "-ExecCmds=Automation RunTests ActionCameraFramework.Profile.Scenarios" "-TestExit=Automation Test Queue Empty"
 * Each scenario: set up with the README sample commands, 120 warm-up frames, 3 timing runs of 600 recorded frames,
 * then 1 run of 600 frames with the allocation counter on (kept apart so its overhead never enters the timings).
 * Time advances with a fixed step of 1/60 s (deterministic motion); frames are not rate limited.
 */
namespace ACFProfileScenarios
{
	constexpr int32 WarmupFrames = 120;
	constexpr int32 RecordFrames = 600;
	constexpr int32 TimingRuns = 3;
	constexpr double FixedDt = 1.0 / 60.0;

	class FRunner;

	struct FScenarioDef
	{
		FString Name;
		FString Description;
		TFunction<void(FRunner&)> Setup;
		TFunction<void(FRunner&, int32 Frame)> Tick;
		bool bBackgroundOnly = false;
		bool bTwoPlayers = false;
	};

	struct FRunData
	{
		TArray<FACFCameraProfileFrame> P1;
		TArray<FACFCameraProfileFrame> P2;
		int32 CollisionHitFrames = 0;
		int32 SearchFrames = 0;
	};

	static FString Row(const FACFCameraProfileStats& S, int32 Decimals = 2)
	{
		return FString::Printf(TEXT("%.*f | %.*f | %.*f | %.*f"), Decimals, S.Mean, Decimals, S.Median, Decimals, S.P95, Decimals, S.Max);
	}

	static FString RowQ(const FACFCameraProfileStats& S)
	{
		return FString::Printf(TEXT("%.2f | %.0f | %.0f"), S.Mean, S.P95, S.Max);
	}

	static TArray<const FACFCameraProfileFrame*> Ptrs(const TArray<FACFCameraProfileFrame>& Frames)
	{
		TArray<const FACFCameraProfileFrame*> Out;
		for (const FACFCameraProfileFrame& F : Frames)
		{
			Out.Add(&F);
		}
		return Out;
	}

	class FRunner : public IAutomationLatentCommand
	{
	public:
		explicit FRunner(FAutomationTestBase* InTest) : Test(InTest) {}

		// --- helpers used by the scenarios ------------------------------------------------------------------
		UWorld* World() const { return PC.IsValid() ? PC->GetWorld() : nullptr; }
		void Exec(const FString& Command) const { GEngine->Exec(World(), *Command); }
		AActor* Enemy(const TCHAR* Label, int32* OutIndex = nullptr) const
		{
			TArray<AActor*> Enemies;
			UGameplayStatics::GetAllActorsWithTag(World(), TEXT("ACFEnemy"), Enemies);
			Enemies.Sort([](const AActor& A, const AActor& B) { return A.GetActorNameOrLabel() < B.GetActorNameOrLabel(); });
			const int32 Index = Enemies.IndexOfByPredicate([Label](const AActor* A) { return A->GetActorNameOrLabel().StartsWith(Label); });
			if (OutIndex)
			{
				*OutIndex = Index;
			}
			return Index != INDEX_NONE ? Enemies[Index] : nullptr;
		}
		void MovePawnOnCircle(const FVector& Center, float Radius, float DegPerSecond, int32 Frame) const
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				const float Angle = FMath::DegreesToRadians(DegPerSecond * Frame * FixedDt);
				const FVector Location(Center.X + FMath::Cos(Angle) * Radius, Center.Y + FMath::Sin(Angle) * Radius, Pawn->GetActorLocation().Z);
				Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::None);
			}
		}

		TWeakObjectPtr<APlayerController> PC;
		TWeakObjectPtr<UACFCameraSubsystem> Acf;
		TWeakObjectPtr<ULocalPlayer> Second;
		TStrongObjectPtr<UACFCameraPreset> SecondPreset;
		int32 CollisionHits = 0;

		virtual bool Update() override;

	private:
		enum class EPhase : uint8 { Init, OverheadSetup, OverheadRun, Setup, Warmup, Record, Teardown, Finish };

		void BuildScenarios();
		UACFCameraDirector* Director(int32 Player) const;
		void ClearProfilers();
		bool Qualifies(const FACFCameraProfileFrame& Frame) const;
		int32 CountQualifying() const;
		void CollectRun();
		void Teardown();
		void RunOverhead();
		void WriteResults();

		FAutomationTestBase* Test;
		TArray<FScenarioDef> Scenarios;
		TArray<TArray<FRunData>> Results; // [scenario][run]; last run = allocation run
		TArray<TArray<FString>> AllocationSites;
		FString OverheadText;
		EPhase Phase = EPhase::Init;
		int32 ScenarioIndex = 0;
		int32 RunIndex = 0;
		int32 FrameIndex = 0;
		uint64 LastEngineFrame = 0;
		int32 SavedProfileCVar = 0;
		bool bSavedFixedStep = false;
		double SavedFixedDt = 0.0;
		// Overhead benchmark
		TStrongObjectPtr<UACFCameraPreset> BenchPreset;
		TArray<TWeakObjectPtr<AActor>> BenchActors;
	};

	UACFCameraDirector* FRunner::Director(int32 Player) const
	{
		if (Player == 0)
		{
			return Acf.IsValid() ? Acf->GetDirector() : nullptr;
		}
		const UACFCameraSubsystem* Acf2 = Second.IsValid() ? Second->GetSubsystem<UACFCameraSubsystem>() : nullptr;
		return Acf2 ? Acf2->GetDirector() : nullptr;
	}

	void FRunner::ClearProfilers()
	{
		for (int32 P = 0; P < 2; ++P)
		{
			if (UACFCameraDirector* D = Director(P))
			{
				D->GetProfiler().Clear();
			}
		}
	}

	bool FRunner::Qualifies(const FACFCameraProfileFrame& F) const
	{
		return F.NumEvaluations > 0 && (!Scenarios[ScenarioIndex].bBackgroundOnly || F.bBackground);
	}

	int32 FRunner::CountQualifying() const
	{
		const UACFCameraDirector* D = Director(0);
		if (!D)
		{
			return 0;
		}
		int32 N = 0;
		for (int32 I = 0; I < D->GetProfiler().Num(); ++I)
		{
			N += Qualifies(D->GetProfiler().GetFrame(I)) ? 1 : 0;
		}
		return N;
	}

	void FRunner::CollectRun()
	{
		FRunData Data;
		const FACFCameraProfiler& P1 = Director(0)->GetProfiler();
		TSet<uint64> FrameNumbers;
		for (int32 I = 0; I < P1.Num(); ++I)
		{
			const FACFCameraProfileFrame& F = P1.GetFrame(I);
			if (Qualifies(F) && Data.P1.Num() < RecordFrames)
			{
				Data.P1.Add(F);
				FrameNumbers.Add(F.FrameNumber);
			}
		}
		if (const UACFCameraDirector* D2 = Director(1))
		{
			for (int32 I = 0; I < D2->GetProfiler().Num(); ++I)
			{
				const FACFCameraProfileFrame& F = D2->GetProfiler().GetFrame(I);
				if (FrameNumbers.Contains(F.FrameNumber))
				{
					Data.P2.Add(F);
				}
			}
		}
		Results[ScenarioIndex].Add(MoveTemp(Data));
		if (RunIndex == 0)
		{
			FFileHelper::SaveStringToFile(ACFCameraProfiling::BuildCsv(P1), *(FPaths::ProjectSavedDir() / TEXT("ACFProfile") / FString::Printf(TEXT("B1_%s_run1.csv"), *Scenarios[ScenarioIndex].Name)));
		}
	}

	void FRunner::Teardown()
	{
		Exec(TEXT("ACF.Sample.Sequencer Stop"));
		Exec(TEXT("ACF.Sample.Combat Off"));
		for (int32 P = 0; P < 2; ++P)
		{
			if (UACFCameraDirector* D = Director(P))
			{
				D->StopAllSequences(true);
				D->StopAllEffects(true);
			}
		}
		if (Acf.IsValid())
		{
			Acf->ReleaseAllCameras(true);
		}
		if (ULocalPlayer* Player = Second.Get())
		{
			if (UACFCameraSubsystem* Acf2 = Player->GetSubsystem<UACFCameraSubsystem>())
			{
				Acf2->ReleaseAllCameras(true);
			}
			World()->GetGameInstance()->RemoveLocalPlayer(Player);
		}
		Second.Reset();
	}

	void FRunner::BuildScenarios()
	{
		const FVector Start(0, 0, 120);
		Scenarios.Add({ TEXT("1_Follow"), TEXT("Follow + Look At + Collision (DA_ACFSample_Follow). The pawn walks a 3 m circle at 90 deg/s."),
			[](FRunner& R) { R.Exec(TEXT("ACF.Sample.Goto Start")); R.Exec(TEXT("ACF.Sample.Use Follow 0")); },
			[Start](FRunner& R, int32 F) { R.MovePawnOnCircle(Start, 300.f, 90.f, F); } });
		Scenarios.Add({ TEXT("2_FreeLookInput"), TEXT("Orbital Follow with FreeLook rings + Collision (DA_ACFSample_FreeLook). Continuous look input: yaw 120 deg/s, pitch -20 +/- 25 deg."),
			[](FRunner& R) { R.Exec(TEXT("ACF.Sample.Goto Start")); R.Exec(TEXT("ACF.Sample.Use FreeLook 0")); },
			[](FRunner& R, int32 F) { const double T = F * FixedDt; R.PC->SetControlRotation(FRotator(-20.0 + 25.0 * FMath::Sin(T * 1.3), 120.0 * T, 0.0)); } });
		Scenarios.Add({ TEXT("3_CollisionCorridor"), TEXT("ThirdPerson (Orbit + Combat + Collision with solver) inside the narrow corridor; the camera yaw sweeps +/-80 deg into the walls and ceiling."),
			[](FRunner& R) { R.Exec(TEXT("ACF.Sample.Goto Corridor")); R.Exec(TEXT("ACF.Sample.Use ThirdPerson 0")); },
			[](FRunner& R, int32 F) { const double T = F * FixedDt; R.PC->SetControlRotation(FRotator(-15.0 + 20.0 * FMath::Sin(T * 0.7), 80.0 * FMath::Sin(T * 1.1), 0.0)); } });
		Scenarios.Add({ TEXT("4_LockOnGroup"), TEXT("Lock-On on Enemy_0 (DA_ACFSample_LockOn: Lock-On + Combat + Group Framing + Collision with solver). Player walks a circle, the enemy circles 4 m around the start."),
			[](FRunner& R) { R.Exec(TEXT("ACF.Sample.Goto Start")); R.Exec(TEXT("ACF.Sample.Use ThirdPerson 0")); int32 I = 0; R.Enemy(TEXT("Enemy_0"), &I); R.Exec(FString::Printf(TEXT("ACF.Sample.LockOn %d"), I)); },
			[Start](FRunner& R, int32 F)
			{
				R.MovePawnOnCircle(Start, 200.f, 60.f, F);
				if (AActor* E = R.Enemy(TEXT("Enemy_0")))
				{
					const float A = FMath::DegreesToRadians(45.f * F * FixedDt);
					E->SetActorLocation(FVector(Start.X + FMath::Cos(A) * 400.f, Start.Y + FMath::Sin(A) * 400.f, E->GetActorLocation().Z));
				}
			} });
		Scenarios.Add({ TEXT("5_CombatDeoccluder"), TEXT("Lock-On on Enemy_4 behind the pillar row, Combat on; the enemy patrols an ellipse around the pillars (occlusion appears and clears)."),
			[](FRunner& R) { R.Exec(TEXT("ACF.Sample.Goto Pillars")); R.Exec(TEXT("ACF.Sample.Use ThirdPerson 0")); int32 I = 0; R.Enemy(TEXT("Enemy_4"), &I); R.Exec(FString::Printf(TEXT("ACF.Sample.LockOn %d"), I)); R.Exec(TEXT("ACF.Sample.Combat On")); },
			[](FRunner& R, int32 F)
			{
				if (AActor* E = R.Enemy(TEXT("Enemy_4")))
				{
					const float A = FMath::DegreesToRadians(35.f * F * FixedDt);
					E->SetActorLocation(FVector(-600.f + FMath::Cos(A) * 350.f, -1200.f + FMath::Sin(A) * 750.f, E->GetActorLocation().Z));
				}
			} });
		Scenarios.Add({ TEXT("6_Effects"), TEXT("ThirdPerson + effect stack: Motion (speed FOV + roll) always on, Recoil every 6 frames, Impulse every 30, Shake every 90. Pawn walks a circle."),
			[](FRunner& R) { R.Exec(TEXT("ACF.Sample.Goto Start")); R.Exec(TEXT("ACF.Sample.Use ThirdPerson 0")); R.Exec(TEXT("ACF.Sample.Effect Motion")); },
			[Start](FRunner& R, int32 F)
			{
				R.MovePawnOnCircle(Start, 300.f, 90.f, F);
				if (F % 6 == 0) { R.Exec(TEXT("ACF.Sample.Effect Recoil")); }
				if (F % 30 == 0) { R.Exec(TEXT("ACF.Sample.Effect Impulse")); }
				if (F % 90 == 0) { R.Exec(TEXT("ACF.Sample.Effect Shake")); }
			} });
		FScenarioDef Seq{ TEXT("7_SequencerBackground"), TEXT("ThirdPerson while LS_ACFSample_Cut plays at rate 0.2 (15 s): only frames where ACF is bypassed (Sequencer camera cut on screen, background evaluation)."),
			[](FRunner& R) { R.Exec(TEXT("ACF.Sample.Goto Start")); R.Exec(TEXT("ACF.Sample.Use ThirdPerson 0")); },
			[](FRunner& R, int32 F)
			{
				// (Re)start the sequence at the beginning of every run and keep it playing.
				const UACFSequencerCameraSubsystem* SeqSubsystem = UACFSequencerCameraSubsystem::Get(R.World());
				if (SeqSubsystem && SeqSubsystem->GetCameraState(R.PC.Get()).NumActiveSequences == 0)
				{
					R.Exec(TEXT("ACF.Sample.Sequencer Play Cut 0.2"));
				}
			} };
		Seq.bBackgroundOnly = true;
		Scenarios.Add(Seq);
		FScenarioDef Two{ TEXT("8_TwoLocalPlayers"), TEXT("Two local players (split screen), each with DA_ACFSample_ThirdPerson following its own pawn; player 1 walks a circle."),
			[](FRunner& R)
			{
				R.Exec(TEXT("ACF.Sample.Goto Start"));
				R.Exec(TEXT("ACF.Sample.Use ThirdPerson 0"));
				FString Error;
				R.Second = R.World()->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(1), Error, true);
			},
			[Start](FRunner& R, int32 F)
			{
				R.MovePawnOnCircle(Start, 300.f, 90.f, F);
				UACFCameraSubsystem* Acf2 = R.Second.IsValid() ? R.Second->GetSubsystem<UACFCameraSubsystem>() : nullptr;
				APlayerController* PC2 = R.Second.IsValid() ? R.Second->GetPlayerController(R.World()) : nullptr;
				if (Acf2 && PC2 && Acf2->GetDirector()->GetNumRequests() == 0)
				{
					R.SecondPreset.Reset(LoadObject<UACFCameraPreset>(nullptr, TEXT("/ActionCameraFramework/Samples/DA_ACFSample_ThirdPerson.DA_ACFSample_ThirdPerson")));
					FACFCameraRequestParams Params;
					Params.FollowTarget.Actor = PC2->GetPawn() ? static_cast<AActor*>(PC2->GetPawn()) : static_cast<AActor*>(PC2);
					Acf2->RequestCamera(R.SecondPreset.Get(), Params);
				}
			} };
		Two.bTwoPlayers = true;
		Scenarios.Add(Two);
		Results.SetNum(Scenarios.Num());
		AllocationSites.SetNum(Scenarios.Num());
	}

	/** Standalone director benchmark: the same evaluation with the recorder off, on, and on + allocation counter. */
	void FRunner::RunOverhead()
	{
		UWorld* W = World();
		auto Bench = [&](bool bProfile, bool bCountAllocations, int32 Evaluations) -> double
		{
			TStrongObjectPtr<UACFCameraDirector> D(NewObject<UACFCameraDirector>());
			D->SetProfilerForced(bProfile);
			FACFCameraRequestParams Params;
			Params.FollowTarget.Actor = BenchActors[0].Get();
			Params.LookAtTarget.Actor = BenchActors[1].Get();
			D->AddRequest(BenchPreset.Get(), Params);
			FACFCameraAllocationCounter::SetCounting(bCountAllocations);
			FACFCameraEvaluationContext Context;
			Context.World = W;
			Context.DeltaTime = 1.f / 60.f;
			// Warm up (activation, first search), then time.
			for (int32 I = 0; I < 120; ++I)
			{
				Context.FrameNumber = 90000000ull + I;
				D->Evaluate(Context);
			}
			const uint64 T0 = FPlatformTime::Cycles64();
			for (int32 I = 0; I < Evaluations; ++I)
			{
				Context.FrameNumber = 91000000ull + I;
				D->Evaluate(Context);
			}
			const double Us = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T0) * 1000.0 / Evaluations;
			FACFCameraAllocationCounter::SetCounting(false);
			return Us;
		};
		TArray<double> Off, On, OnAlloc;
		for (int32 Round = 0; Round < 7; ++Round) // interleaved to spread drift / turbo effects over all variants
		{
			Off.Add(Bench(false, false, 3000));
			On.Add(Bench(true, false, 3000));
			OnAlloc.Add(Bench(true, true, 3000));
		}
		const FACFCameraProfileStats SOff = FACFCameraProfileStats::Compute(Off);
		const FACFCameraProfileStats SOn = FACFCameraProfileStats::Compute(On);
		const FACFCameraProfileStats SAlloc = FACFCameraProfileStats::Compute(OnAlloc);
		OverheadText = FString::Printf(TEXT("Standalone director, Orbit + Look At + Collision with deoccluder (occluded target, search every 0.3 s), 7 rounds x 3000 evaluations, us per evaluation (median of rounds / min / max):\n")
			TEXT("| Recorder | median | min | max |\n|---|---|---|---|\n| off (ACF.Profile 0) | %.3f | %.3f | %.3f |\n| on | %.3f | %.3f | %.3f |\n| on + allocation counter | %.3f | %.3f | %.3f |\n")
			TEXT("Recorder overhead (median on - median off): %.3f us per evaluation (%.1f %%).\n"),
			SOff.Median, Off[0], Off.Last(), SOn.Median, On[0], On.Last(), SAlloc.Median, OnAlloc[0], OnAlloc.Last(),
			SOn.Median - SOff.Median, SOff.Median > 0.0 ? 100.0 * (SOn.Median - SOff.Median) / SOff.Median : 0.0);

		// Phase 6B / B-3: the same evaluation with N actors in the collision ignore list (weak references resolved every
		// frame; 1/3 of them destroyed, so the invalid-entry path is measured too).
		{
			TArray<AActor*> Extra;
			for (int32 I = 0; I < 32; ++I)
			{
				FActorSpawnParameters SP;
				SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Extra.Add(W->SpawnActor<AActor>(AActor::StaticClass(), FTransform(BenchActors[0]->GetActorLocation() + FVector(0.0, 5000.0 + I * 10.0, 0.0)), SP));
			}
			FString Rows;
			for (const int32 Count : { 0, 8, 32 })
			{
				TArray<double> Us;
				for (int32 Round = 0; Round < 7; ++Round)
				{
					TStrongObjectPtr<UACFCameraDirector> D(NewObject<UACFCameraDirector>());
					FACFCameraRequestParams Params;
					Params.FollowTarget.Actor = BenchActors[0].Get();
					Params.LookAtTarget.Actor = BenchActors[1].Get();
					for (int32 I = 0; I < Count; ++I)
					{
						Params.CollisionIgnoredActors.Add(Extra[I]);
					}
					D->AddRequest(BenchPreset.Get(), Params);
					FACFCameraEvaluationContext Context;
					Context.World = W;
					Context.DeltaTime = 1.f / 60.f;
					for (int32 I = 0; I < 60; ++I)
					{
						Context.FrameNumber = 95000000ull + Round * 10000ull + I;
						D->Evaluate(Context);
					}
					const uint64 T0 = FPlatformTime::Cycles64();
					for (int32 I = 0; I < 2000; ++I)
					{
						Context.FrameNumber = 95100000ull + Round * 10000ull + I;
						D->Evaluate(Context);
					}
					Us.Add(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T0) * 1000.0 / 2000);
				}
				Rows += FString::Printf(TEXT("| %d | %.3f |\n"), Count, FACFCameraProfileStats::Compute(Us).Median);
			}
			for (int32 I = 0; I < Extra.Num(); I += 3)
			{
				Extra[I]->Destroy(); // invalid entries for the last row
			}
			{
				TArray<double> Us;
				for (int32 Round = 0; Round < 7; ++Round)
				{
					TStrongObjectPtr<UACFCameraDirector> D(NewObject<UACFCameraDirector>());
					FACFCameraRequestParams Params;
					Params.FollowTarget.Actor = BenchActors[0].Get();
					Params.LookAtTarget.Actor = BenchActors[1].Get();
					for (AActor* Actor : Extra)
					{
						Params.CollisionIgnoredActors.Add(Actor);
					}
					D->AddRequest(BenchPreset.Get(), Params);
					FACFCameraEvaluationContext Context;
					Context.World = W;
					Context.DeltaTime = 1.f / 60.f;
					const uint64 T0 = FPlatformTime::Cycles64();
					for (int32 I = 0; I < 2000; ++I)
					{
						Context.FrameNumber = 96100000ull + Round * 10000ull + I;
						D->Evaluate(Context);
					}
					Us.Add(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T0) * 1000.0 / 2000);
				}
				Rows += FString::Printf(TEXT("| 32 (11 destroyed) | %.3f |\n"), FACFCameraProfileStats::Compute(Us).Median);
			}
			for (AActor* Actor : Extra)
			{
				if (IsValid(Actor))
				{
					Actor->Destroy();
				}
			}
			OverheadText += FString::Printf(TEXT("\nCollision ignore list (B-3), same evaluation, us per evaluation (median of 7 x 2000):\n| Ignored actors | us |\n|---|---|\n%s"), *Rows);
		}

		for (const TWeakObjectPtr<AActor>& A : BenchActors)
		{
			if (A.IsValid())
			{
				A->Destroy();
			}
		}

		// Phase 6B / B-2: output gate of one modifier pass on the standard PCM, before (POV address + BeginOutputForViewTarget,
		// the Phase 5A code path) vs after (BeginOutputForModifierPass). Same process, interleaved rounds.
		APlayerCameraManager* CameraManager = PC.IsValid() ? PC->PlayerCameraManager.Get() : nullptr;
		if (CameraManager && Acf.IsValid())
		{
			UACFCameraSubsystem* Subsystem = Acf.Get();
			const FMinimalViewInfo& POV = CameraManager->ViewTarget.POV;
			constexpr int32 Calls = 200000;
			int32 Sink = 0;
			auto OldGate = [&]() -> double
			{
				const uint64 T0 = FPlatformTime::Cycles64();
				for (int32 I = 0; I < Calls; ++I)
				{
					AActor* Target = &POV == &CameraManager->PendingViewTarget.POV ? CameraManager->PendingViewTarget.Target.Get()
						: (&POV == &CameraManager->ViewTarget.POV ? CameraManager->ViewTarget.Target.Get() : nullptr);
					Sink += Subsystem->BeginOutputForViewTarget(Target, 1.f / 60.f) ? 1 : 0;
				}
				return FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T0) * 1.0e6 / Calls; // ns per pass
			};
			auto NewGate = [&]() -> double
			{
				const uint64 T0 = FPlatformTime::Cycles64();
				for (int32 I = 0; I < Calls; ++I)
				{
					Sink += Subsystem->BeginOutputForModifierPass(CameraManager, POV, 1.f / 60.f) ? 1 : 0;
				}
				return FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T0) * 1.0e6 / Calls;
			};
			TArray<double> OldNs, NewNs;
			for (int32 Round = 0; Round < 9; ++Round)
			{
				OldNs.Add(OldGate());
				NewNs.Add(NewGate());
			}
			const FACFCameraProfileStats SOld = FACFCameraProfileStats::Compute(OldNs);
			const FACFCameraProfileStats SNew = FACFCameraProfileStats::Compute(NewNs);
			OverheadText += FString::Printf(TEXT("\nOutput gate per modifier pass on the standard PCM (%s), 9 rounds x %d calls, ns per pass (median / min / max):\n")
				TEXT("| Gate | median | min | max |\n|---|---|---|---|\n| before B-2 (POV address + BeginOutputForViewTarget) | %.1f | %.1f | %.1f |\n| after B-2 (BeginOutputForModifierPass) | %.1f | %.1f | %.1f |\n")
				TEXT("Difference (median): %+.1f ns per pass (%d passes accepted).\n"),
				*GetNameSafe(CameraManager->GetClass()), Calls, SOld.Median, OldNs[0], OldNs.Last(), SNew.Median, NewNs[0], NewNs.Last(), SNew.Median - SOld.Median, Sink);
		}

		// Phase 6B / B-4: cost of the health checks. (a) one check = what RunNodes adds per node (pose copy + validity);
		// (b) a director with N nodes, to see how the evaluation scales with the node count.
		{
			constexpr int32 Checks = 2000000;
			TArray<double> CheckNs;
			int32 Valid = 0;
			for (int32 Round = 0; Round < 9; ++Round)
			{
				FACFCameraPose Pose;
				Pose.Location = FVector(1.0, 2.0, 3.0);
				const uint64 T0 = FPlatformTime::Cycles64();
				for (int32 I = 0; I < Checks; ++I)
				{
					Pose.Location.X += 1.0e-3; // keep the work in the loop
					const FACFCameraPose Before = Pose;
					Valid += ACFCameraValidation::IsValidPose(Pose) && Before.FieldOfView > 0.f ? 1 : 0;
				}
				CheckNs.Add(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T0) * 1.0e6 / Checks);
			}
			const FACFCameraProfileStats SCheck = FACFCameraProfileStats::Compute(CheckNs);
			FString Scaling;
			for (const int32 NumNodes : { 1, 8, 24 })
			{
				TStrongObjectPtr<UACFCameraPreset> Preset(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
				for (int32 N = 0; N < NumNodes; ++N)
				{
					UACFCameraNode_FixedPose* Node = NewObject<UACFCameraNode_FixedPose>(Preset.Get());
					Node->Location = FVector(N, 0.0, 300.0);
					Preset->Nodes.Add(Node);
				}
				TStrongObjectPtr<UACFCameraDirector> D(NewObject<UACFCameraDirector>());
				D->AddRequest(Preset.Get(), FACFCameraRequestParams());
				FACFCameraEvaluationContext Context;
				Context.DeltaTime = 1.f / 60.f;
				TArray<double> EvalUs;
				for (int32 Round = 0; Round < 7; ++Round)
				{
					const uint64 T0 = FPlatformTime::Cycles64();
					for (int32 I = 0; I < 20000; ++I)
					{
						Context.FrameNumber = 80000000ull + Round * 100000ull + I;
						D->Evaluate(Context);
					}
					EvalUs.Add(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T0) * 1000.0 / 20000);
				}
				Scaling += FString::Printf(TEXT("| %d | %.3f |\n"), NumNodes, FACFCameraProfileStats::Compute(EvalUs).Median);
			}
			OverheadText += FString::Printf(TEXT("\nHealth checks (B-4): one per-node check (pose copy + validity) %.2f ns median (min %.2f, max %.2f; %d valid).\n")
				TEXT("Standalone director evaluation by node count (Fixed Pose nodes, includes the node work and every check), us per evaluation (median of 7 x 20000):\n| Nodes | us |\n|---|---|\n%s"),
				SCheck.Median, CheckNs[0], CheckNs.Last(), Valid, *Scaling);
		}
		{
			// Phase 7A-1: runtime tuning, same evaluation as above. Off / on (values unchanged) / on with a Set every evaluation.
			// The copy is made once by Start (not timed); allocations are counted over the timed evaluations.
			auto TuningBench = [&](int32 Mode, int32 Evaluations, uint64& OutAllocations) -> double
			{
				TStrongObjectPtr<UACFCameraDirector> D(NewObject<UACFCameraDirector>());
				FACFCameraRequestParams Params;
				Params.FollowTarget.Actor = BenchActors[0].Get();
				Params.LookAtTarget.Actor = BenchActors[1].Get();
				const FACFCameraRequestHandle H = D->AddRequest(BenchPreset.Get(), Params);
				FACFCameraEvaluationContext Context;
				Context.World = W;
				Context.DeltaTime = 1.f / 60.f;
				for (int32 I = 0; I < 120; ++I)
				{
					Context.FrameNumber = 70000000ull + I;
					D->Evaluate(Context);
				}
				if (Mode > 0)
				{
					D->StartTuning(H);
				}
				D->SetProfilerForced(true);
				D->GetProfiler().Start(Evaluations);
				FACFCameraAllocationCounter::SetCounting(true);
				const uint64 T0 = FPlatformTime::Cycles64();
				for (int32 I = 0; I < Evaluations; ++I)
				{
					if (Mode == 2)
					{
						D->SetTuningValue(H, EACFCameraTuningParameter::CameraOffset, FVector(0.0, (I & 1) ? 40.0 : 39.0, 0.0));
					}
					Context.FrameNumber = 71000000ull + I;
					D->Evaluate(Context);
				}
				const double Us = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T0) * 1000.0 / Evaluations;
				FACFCameraAllocationCounter::SetCounting(false);
				OutAllocations = 0;
				for (int32 F = 0; F < D->GetProfiler().Num(); ++F)
				{
					OutAllocations += D->GetProfiler().GetFrame(F).Allocations;
				}
				D->SetProfilerForced(false);
				return Us;
			};
			TArray<double> Times[3];
			uint64 Allocations[3] = {};
			for (int32 Round = 0; Round < 7; ++Round)
			{
				for (int32 Mode = 0; Mode < 3; ++Mode)
				{
					uint64 A = 0;
					Times[Mode].Add(TuningBench(Mode, 600, A));
					Allocations[Mode] = FMath::Max(Allocations[Mode], A);
				}
			}
			const TCHAR* Labels[3] = { TEXT("tuning off"), TEXT("tuning on (unchanged values)"), TEXT("tuning on, Set Camera Offset every evaluation") };
			FString Rows;
			for (int32 Mode = 0; Mode < 3; ++Mode)
			{
				const FACFCameraProfileStats S = FACFCameraProfileStats::Compute(Times[Mode]);
				Rows += FString::Printf(TEXT("| %s | %.3f | %.3f | %.3f | %llu |\n"), Labels[Mode], S.Median, Times[Mode][0], Times[Mode].Last(), Allocations[Mode]);
			}
			OverheadText += FString::Printf(TEXT("\nRuntime tuning (7A-1), same evaluation, us per evaluation (7 rounds x 600, median / min / max) and allocations in 600 evaluations (max of the rounds):\n")
				TEXT("| Mode | median | min | max | allocations |\n|---|---|---|---|---|\n%s"), *Rows);
		}
	}

	bool FRunner::Update()
	{
		if (GFrameCounter == LastEngineFrame)
		{
			return false;
		}
		LastEngineFrame = GFrameCounter;
		switch (Phase)
		{
		case EPhase::Init:
		{
			UWorld* Found = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.WorldType == EWorldType::Game && Context.World()->GetFirstPlayerController())
				{
					Found = Context.World();
				}
			}
			if (!Found)
			{
				return GetCurrentRunTime() > 30.0;
			}
			PC = Found->GetFirstPlayerController();
			Acf = PC->GetLocalPlayer()->GetSubsystem<UACFCameraSubsystem>();
			IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("ACF.Profile"));
			SavedProfileCVar = Var->GetInt();
			Var->Set(1, ECVF_SetByCode);
			bSavedFixedStep = FApp::UseFixedTimeStep();
			SavedFixedDt = FApp::GetFixedDeltaTime();
			FApp::SetFixedDeltaTime(FixedDt);
			FApp::SetUseFixedTimeStep(true);
			FACFCameraAllocationCounter::Install();
			BuildScenarios();
			// Overhead benchmark fixture far away from the map: player, occluded target, pillar.
			const FVector Base(-90000.f, -90000.f, 20000.f);
			auto Spawn = [&](const FVector& At, const FVector* Extent) -> AActor*
			{
				FActorSpawnParameters SP;
				SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AActor* Actor = Found->SpawnActor<AActor>(AActor::StaticClass(), FTransform(At), SP);
				if (Extent)
				{
					UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Box"));
					Box->SetBoxExtent(*Extent);
					Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
					Actor->SetRootComponent(Box);
					Box->RegisterComponent();
				}
				else
				{
					USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
					Actor->SetRootComponent(Root);
					Root->RegisterComponent();
				}
				Actor->SetActorLocation(At);
				return Actor;
			};
			const FVector PillarExtent(25.f, 25.f, 300.f);
			BenchActors = { Spawn(Base, nullptr), Spawn(Base + FVector(600.f, 0.f, 0.f), nullptr), Spawn(Base + FVector(300.f, 0.f, 0.f), &PillarExtent) };
			UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
			UACFCameraNode_OrbitalFollow* Orbit = NewObject<UACFCameraNode_OrbitalFollow>(Preset);
			Orbit->InputSource = EACFCameraOrbitInputSource::None;
			Orbit->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
			Orbit->InitialYaw = 0.f;
			Orbit->InitialPitch = 0.f;
			Orbit->PivotOffset = FVector::ZeroVector;
			Orbit->Distance = 600.f;
			UACFCameraNode_LookAt* Look = NewObject<UACFCameraNode_LookAt>(Preset);
			Look->TargetSource = EACFCameraLookAtSource::LookAtTarget;
			UACFCameraNode_Collision* Collision = NewObject<UACFCameraNode_Collision>(Preset);
			Collision->Deoccluder.bEnabled = true;
			Collision->Deoccluder.bCheckLookAtTarget = true;
			Preset->Nodes = { Orbit, Look, Collision };
			Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
			BenchPreset.Reset(Preset);
			Phase = EPhase::OverheadRun;
			return false;
		}
		case EPhase::OverheadRun:
			RunOverhead();
			Phase = EPhase::Setup;
			return false;
		case EPhase::Setup:
			Test->AddInfo(FString::Printf(TEXT("Scenario %s"), *Scenarios[ScenarioIndex].Name));
			Scenarios[ScenarioIndex].Setup(*this);
			FrameIndex = 0;
			RunIndex = 0;
			Phase = EPhase::Warmup;
			return false;
		case EPhase::Warmup:
			Scenarios[ScenarioIndex].Tick(*this, FrameIndex++);
			if (FrameIndex >= WarmupFrames && (!Scenarios[ScenarioIndex].bTwoPlayers || (Director(1) && Director(1)->GetNumRequests() > 0)))
			{
				ClearProfilers();
				Phase = EPhase::Record;
			}
			else if (FrameIndex > WarmupFrames + 600)
			{
				Test->AddError(FString::Printf(TEXT("%s: never became ready."), *Scenarios[ScenarioIndex].Name));
				Phase = EPhase::Teardown;
			}
			return false;
		case EPhase::Record:
			Scenarios[ScenarioIndex].Tick(*this, FrameIndex++);
			if (const UACFCameraDirector* D = Director(0))
			{
				if (D->GetLastDebug().bCollisionHit) // collision frequency of the recorded frames (debug data of the last evaluation)
				{
					++CollisionHits;
				}
			}
			if (CountQualifying() >= RecordFrames)
			{
				const bool bAllocationRun = RunIndex == TimingRuns;
				if (bAllocationRun)
				{
					FACFCameraAllocationCounter::SetCounting(false);
				}
				CollectRun();
				Results[ScenarioIndex].Last().CollisionHitFrames = CollisionHits;
				CollisionHits = 0;
				if (bAllocationRun)
				{
					TArray<FACFCameraAllocationCounter::FSite> Sites;
					uint32 Overflow = 0;
					FACFCameraAllocationCounter::GetSites(Sites, Overflow);
					Sites.Sort([](const FACFCameraAllocationCounter::FSite& A, const FACFCameraAllocationCounter::FSite& B) { return A.Count > B.Count; });
					for (int32 I = 0; I < FMath::Min(8, Sites.Num()); ++I)
					{
						AllocationSites[ScenarioIndex].Add(FString::Printf(TEXT("%u allocations, %llu bytes, section %s:\n%s"), Sites[I].Count, Sites[I].Bytes,
							Sites[I].Section >= 0 ? LexToString(static_cast<EACFCameraProfileSection>(Sites[I].Section)) : TEXT("(outside sections)"),
							*FACFCameraAllocationCounter::DescribeSite(Sites[I], 8)));
					}
					if (Overflow > 0)
					{
						AllocationSites[ScenarioIndex].Add(FString::Printf(TEXT("%u allocations at further call stacks (table full)"), Overflow));
					}
					Phase = EPhase::Teardown;
					return false;
				}
				++RunIndex;
				if (Scenarios[ScenarioIndex].bBackgroundOnly)
				{
					Exec(TEXT("ACF.Sample.Sequencer Stop")); // next run starts a fresh playback (re-played by Tick)
				}
				if (RunIndex == TimingRuns)
				{
					FACFCameraAllocationCounter::ResetSites();
					FACFCameraAllocationCounter::SetCounting(true);
				}
				ClearProfilers();
			}
			else if (FrameIndex > WarmupFrames + (TimingRuns + 1) * RecordFrames * 3)
			{
				Test->AddError(FString::Printf(TEXT("%s: could not record %d qualifying frames."), *Scenarios[ScenarioIndex].Name, RecordFrames));
				FACFCameraAllocationCounter::SetCounting(false);
				Phase = EPhase::Teardown;
			}
			return false;
		case EPhase::Teardown:
			Teardown();
			if (++ScenarioIndex >= Scenarios.Num())
			{
				Phase = EPhase::Finish;
			}
			else
			{
				Phase = EPhase::Setup;
			}
			return false;
		case EPhase::Finish:
		default:
			WriteResults();
			IConsoleManager::Get().FindConsoleVariable(TEXT("ACF.Profile"))->Set(SavedProfileCVar, ECVF_SetByCode);
			FApp::SetFixedDeltaTime(SavedFixedDt);
			FApp::SetUseFixedTimeStep(bSavedFixedStep);
			return true;
		}
	}

	void FRunner::WriteResults()
	{
		FString Out;
		Out += TEXT("# ACF Phase 6B / B-1 measurement results (generated)\n\n");
		Out += FString::Printf(TEXT("- Engine %s, build %s, %s, CPU: %s (%d cores / %d logical)\n"), *FEngineVersion::Current().ToString(),
			LexToString(FApp::GetBuildConfiguration()), FApp::CanEverRender() ? TEXT("rendering") : TEXT("-NullRHI"), *FPlatformMisc::GetCPUBrand().TrimStartAndEnd(),
			FPlatformMisc::NumberOfCores(), FPlatformMisc::NumberOfCoresIncludingHyperthreads());
		Out += FString::Printf(TEXT("- Fixed time step %.4f s, frames not rate limited. Per scenario: %d warm-up frames, %d timing runs x %d frames, 1 allocation run x %d frames.\n"),
			FixedDt, WarmupFrames, TimingRuns, RecordFrames, RecordFrames);
		Out += TEXT("- Times in microseconds per frame of one local player (ACF total = Evaluate + PreEvaluate + BackgroundUpstream + OutputGuard). Queries = collision queries issued by ACF.\n\n");
		Out += TEXT("## Recorder overhead\n\n") + OverheadText + TEXT("\n");

		Out += TEXT("## Summary (timing runs combined)\n\n| Scenario | ACF total us: mean | median | P95 | max | Evaluate us P95 | queries/frame: mean | P95 | max | run means (us) | collision-hit frames |\n|---|---|---|---|---|---|---|---|---|---|---|\n");
		for (int32 S = 0; S < Scenarios.Num(); ++S)
		{
			TArray<const FACFCameraProfileFrame*> All;
			FString RunMeans;
			int32 Hits = 0;
			for (int32 R = 0; R < FMath::Min(TimingRuns, Results[S].Num()); ++R)
			{
				const TArray<const FACFCameraProfileFrame*> RunFrames = Ptrs(Results[S][R].P1);
				All.Append(RunFrames);
				RunMeans += FString::Printf(TEXT("%s%.2f"), R > 0 ? TEXT(" / ") : TEXT(""), FACFCameraProfileSummary::Summarize(RunFrames).TotalUs.Mean);
				Hits += Results[S][R].CollisionHitFrames;
			}
			const FACFCameraProfileSummary Sum = FACFCameraProfileSummary::Summarize(All);
			Out += FString::Printf(TEXT("| %s | %s | %.2f | %s | %s | %d / %d |\n"), *Scenarios[S].Name, *Row(Sum.TotalUs),
				Sum.SectionUs[static_cast<int32>(EACFCameraProfileSection::Evaluate)].P95, *RowQ(Sum.QueriesPerFrame), *RunMeans, Hits, Sum.NumFrames);
		}

		for (int32 S = 0; S < Scenarios.Num(); ++S)
		{
			Out += FString::Printf(TEXT("\n## %s\n\n%s\n\n"), *Scenarios[S].Name, *Scenarios[S].Description);
			TArray<const FACFCameraProfileFrame*> All;
			TArray<const FACFCameraProfileFrame*> All2;
			for (int32 R = 0; R < FMath::Min(TimingRuns, Results[S].Num()); ++R)
			{
				All.Append(Ptrs(Results[S][R].P1));
				All2.Append(Ptrs(Results[S][R].P2));
			}
			const FACFCameraProfileSummary Sum = FACFCameraProfileSummary::Summarize(All);
			Out += TEXT("| Section (player 1) | mean | median | P95 | max |\n|---|---|---|---|---|\n");
			Out += FString::Printf(TEXT("| ACF total | %s |\n"), *Row(Sum.TotalUs));
			for (int32 Sec = 0; Sec < FACFCameraProfileFrame::NumSections; ++Sec)
			{
				if (Sum.SectionUs[Sec].Max > 0.0)
				{
					Out += FString::Printf(TEXT("| %s | %s |\n"), LexToString(static_cast<EACFCameraProfileSection>(Sec)), *Row(Sum.SectionUs[Sec]));
				}
			}
			for (const TPair<FName, FACFCameraProfileStats>& Node : Sum.NodeUs)
			{
				Out += FString::Printf(TEXT("| node %s | %s |\n"), *Node.Key.ToString(), *Row(Node.Value));
			}
			Out += FString::Printf(TEXT("\nFrames %d (evaluations %d, background %d). Queries per frame %s; per evaluation %s.\n"), Sum.NumFrames, Sum.NumEvaluations, Sum.NumBackgroundFrames,
				*Sum.QueriesPerFrame.ToString(), *Sum.QueriesPerEvaluation.ToString());
			Out += TEXT("\n| Query purpose | mean | P95 | max |\n|---|---|---|---|\n");
			for (int32 P = 0; P < FACFCameraProfileFrame::NumPurposes; ++P)
			{
				Out += FString::Printf(TEXT("| %s | %s |\n"), LexToString(static_cast<EACFCameraQueryPurpose>(P)), *RowQ(Sum.QueriesByPurpose[P]));
			}
			Out += FString::Printf(TEXT("| (shape) Sweep | %s |\n| (shape) Line | %s |\n"), *RowQ(Sum.QueriesByShape[0]), *RowQ(Sum.QueriesByShape[1]));
			if (All2.Num() > 0)
			{
				const FACFCameraProfileSummary Sum2 = FACFCameraProfileSummary::Summarize(All2);
				TMap<uint64, double> Combined;
				TMap<uint64, double> CombinedQ;
				for (const FACFCameraProfileFrame* F : All)
				{
					Combined.FindOrAdd(F->FrameNumber) += F->GetTotalMicroseconds();
					CombinedQ.FindOrAdd(F->FrameNumber) += F->GetFrameQueries();
				}
				for (const FACFCameraProfileFrame* F : All2)
				{
					Combined.FindOrAdd(F->FrameNumber) += F->GetTotalMicroseconds();
					CombinedQ.FindOrAdd(F->FrameNumber) += F->GetFrameQueries();
				}
				TArray<double> V;
				TArray<double> Q;
				Combined.GenerateValueArray(V);
				CombinedQ.GenerateValueArray(Q);
				Out += FString::Printf(TEXT("\nPlayer 2: ACF total %s; queries per frame %s.\nBoth players per frame: ACF total %s; queries %s.\n"),
					*Sum2.TotalUs.ToString(), *Sum2.QueriesPerFrame.ToString(), *FACFCameraProfileStats::Compute(V).ToString(), *FACFCameraProfileStats::Compute(Q).ToString());
			}
			if (Results[S].Num() > TimingRuns)
			{
				const FRunData& AllocRun = Results[S][TimingRuns];
				TArray<const FACFCameraProfileFrame*> AF = Ptrs(AllocRun.P1);
				AF.Append(Ptrs(AllocRun.P2));
				const FACFCameraProfileSummary A = FACFCameraProfileSummary::Summarize(AF);
				Out += FString::Printf(TEXT("\nAllocation run (%d frames incl. player 2): %llu allocations, %llu bytes, %d frames with at least one allocation.\n"),
					A.NumFrames, A.Allocations, A.AllocatedBytes, A.FramesWithAllocations);
				FString BySection;
				for (int32 Sec = 0; Sec <= FACFCameraProfileFrame::NumSections; ++Sec)
				{
					if (A.AllocationsBySection[Sec] > 0)
					{
						BySection += FString::Printf(TEXT(" %s=%llu"), Sec < FACFCameraProfileFrame::NumSections ? LexToString(static_cast<EACFCameraProfileSection>(Sec)) : TEXT("(none)"), A.AllocationsBySection[Sec]);
					}
				}
				if (!BySection.IsEmpty())
				{
					Out += TEXT("By innermost section:") + BySection + TEXT("\n");
				}
				for (const FString& Site : AllocationSites[S])
				{
					Out += TEXT("\n```\n") + Site + TEXT("```\n");
				}
			}
		}
		const FString Path = FPaths::ProjectSavedDir() / TEXT("ACFProfile") / TEXT("B1_Results.md");
		FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		Test->AddInfo(FString::Printf(TEXT("Results written to %s"), *Path));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFProfileScenariosTest, "ActionCameraFramework.Profile.Scenarios",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFProfileScenariosTest::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("ACFProfile")))
	{
		AddInfo(TEXT("Skipped: performance baseline, run with -ACFProfile on /ActionCameraFramework/Samples/L_ACFCameraTest."));
		return true;
	}
	const UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		World = Context.WorldType == EWorldType::Game ? Context.World() : World;
	}
	if (!World || !World->GetMapName().Contains(TEXT("L_ACFCameraTest")))
	{
		AddWarning(TEXT("Skipped: run on /ActionCameraFramework/Samples/L_ACFCameraTest."));
		return true;
	}
	// The second local player's controller comes from the host project's GameMode (may log an unrelated UMG error).
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this); // B-9: only for a host Blueprint PlayerController
	ADD_LATENT_AUTOMATION_COMMAND(ACFProfileScenarios::FRunner(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && ACF_WITH_PROFILER
