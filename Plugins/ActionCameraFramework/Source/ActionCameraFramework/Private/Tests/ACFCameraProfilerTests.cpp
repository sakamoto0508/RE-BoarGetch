#include "Misc/AutomationTest.h"
#include "ACFCameraProfiler.h" // defines ACF_WITH_PROFILER

#if WITH_DEV_AUTOMATION_TESTS && ACF_WITH_PROFILER

#include "ACFCameraTestUtils.h"
#include "ACFCameraSubsystem.h"
#include "ACFCameraSequence.h"
#include "Effects/ACFCameraEffect.h"
#include "Effects/ACFCameraBuiltinEffects.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "Components/BoxComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 6B / B-1: the profiler must never change what ACF outputs, must count ACF's own queries exactly once per
 * frame and player, and must leave nothing behind. Recording is forced per director (not via the cvar) so the tests
 * do not depend on the console state, except where the cvar path itself is under test.
 */
namespace ACFProfilerTests
{
	using namespace ACFTests;

	struct FCVarGuard
	{
		IConsoleVariable* Var;
		int32 Saved;
		explicit FCVarGuard(const TCHAR* Name, int32 Value) : Var(IConsoleManager::Get().FindConsoleVariable(Name)), Saved(Var ? Var->GetInt() : 0)
		{
			if (Var)
			{
				Var->Set(Value, ECVF_SetByCode);
			}
		}
		~FCVarGuard()
		{
			if (Var)
			{
				Var->Set(Saved, ECVF_SetByCode);
			}
		}
	};

	static bool SameResult(const FACFCameraEvaluationResult& A, const FACFCameraEvaluationResult& B)
	{
		auto SamePose = [](const FACFCameraPose& X, const FACFCameraPose& Y)
		{
			return X.Location == Y.Location && X.Rotation == Y.Rotation && X.FieldOfView == Y.FieldOfView; // bit-exact
		};
		return SamePose(A.Pose, B.Pose) && SamePose(A.AimPose, B.AimPose) && A.Weight == B.Weight && A.RequestWeight == B.RequestWeight
			&& A.BlendAlpha == B.BlendAlpha && A.bIsBlending == B.bIsBlending && A.NumRequests == B.NumRequests
			&& A.NumOccludedTargets == B.NumOccludedTargets && A.bPlayerOccluded == B.bPlayerOccluded && A.bFramingLimited == B.bFramingLimited;
	}

	static UACFCameraEffectPreset* MakePunch(float Fov, float Sustain)
	{
		UACFCameraEffectPreset* Preset = NewObject<UACFCameraEffectPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraEffect_Punch* Layer = NewObject<UACFCameraEffect_Punch>(Preset);
		Layer->Location = FVector(-20.f, 5.f, 3.f);
		Layer->FOV = Fov;
		Layer->Envelope.Attack = 0.05f;
		Layer->Envelope.Sustain = Sustain;
		Layer->Envelope.Release = 0.2f;
		Preset->Layers.Add(Layer);
		return Preset;
	}
}


// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFProfilerLifecycleTest, "ActionCameraFramework.Profiler.RecordingLifecycle", ACFTests::Flags)
bool FACFProfilerLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace ACFProfilerTests; // not at file scope: it would leak ACFTests::Flags into unity builds
	FCVarGuard Off(TEXT("ACF.Profile"), 0);
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeFixedPreset(FVector(100.f, 0.f, 50.f), 80.f));
	Director->AddRequest(Preset.Get(), FACFCameraRequestParams());
	FFrameDriver Driver;

	// Disabled (cvar 0, not forced): nothing is allocated or recorded, no profiler is ever active.
	for (int32 I = 0; I < 60; ++I)
	{
		Driver.Step(Director.Get(), 1.f / 60.f);
	}
	TestFalse(TEXT("Disabled: not recording"), Director->GetProfiler().IsRecording());
	TestEqual(TEXT("Disabled: no buffer"), Director->GetProfiler().Capacity(), 0);
	TestEqual(TEXT("Disabled: no frames"), Director->GetProfiler().Num(), 0);
	TestNull(TEXT("Disabled: no active profiler after evaluating"), FACFCameraProfiler::GetActive());

	// Forced on: one record per evaluated frame; cached repeats of a frame add nothing.
	Director->SetProfilerForced(true);
	Director->GetProfiler().Start(16); // small ring to check wrapping (SyncProfiler keeps an existing buffer)
	for (int32 I = 0; I < 40; ++I)
	{
		Driver.Step(Director.Get(), 1.f / 60.f);
		Driver.Repeat(Director.Get(), 1.f / 60.f);
	}
	const FACFCameraProfiler& Profiler = Director->GetProfiler();
	TestTrue(TEXT("Forced: recording"), Profiler.IsRecording());
	TestEqual(TEXT("Ring keeps the last 16 frames"), Profiler.Num(), 16);
	TestEqual(TEXT("Oldest kept frame"), static_cast<int64>(Profiler.GetFrame(0).FrameNumber), static_cast<int64>(Driver.Frame - 15));
	TestEqual(TEXT("Newest frame"), static_cast<int64>(Profiler.GetLatestFrame()->FrameNumber), static_cast<int64>(Driver.Frame));
	bool bOneEvaluationEach = true;
	for (int32 I = 0; I < Profiler.Num(); ++I)
	{
		const FACFCameraProfileFrame& Frame = Profiler.GetFrame(I);
		bOneEvaluationEach &= Frame.NumEvaluations == 1 && Frame.SectionCycles[static_cast<int32>(EACFCameraProfileSection::Evaluate)] > 0;
	}
	TestTrue(TEXT("Cached repeats are not evaluations: exactly one per frame"), bOneEvaluationEach);
	TestEqual(TEXT("Fixed Pose node sampled once per frame"), Profiler.GetLatestFrame()->NumNodeSamples, 1);
	TestNull(TEXT("Forced: no active profiler outside the evaluation"), FACFCameraProfiler::GetActive());

	// A zero-delta evaluation upgraded by a real one in the same frame (InitializeFor) shares the frame's record.
	FACFCameraEvaluationContext Context;
	Context.FrameNumber = ++Driver.Frame;
	Context.DeltaTime = 0.f;
	Director->Evaluate(Context);
	Context.DeltaTime = 1.f / 60.f;
	Director->Evaluate(Context);
	TestEqual(TEXT("Upgraded zero-delta frame: two evaluations in one record"), static_cast<int32>(Profiler.GetLatestFrame()->NumEvaluations), 2);

	// Back to disabled: the next evaluation frees the buffer.
	Director->SetProfilerForced(false);
	Driver.Step(Director.Get(), 1.f / 60.f);
	TestEqual(TEXT("Stopped: buffer freed"), Director->GetProfiler().Capacity(), 0);

	// The console variable drives every director that is not forced.
	{
		FCVarGuard On(TEXT("ACF.Profile"), 1);
		Driver.Step(Director.Get(), 1.f / 60.f);
		TestTrue(TEXT("cvar 1: recording"), Director->GetProfiler().IsRecording());
	}
	Driver.Step(Director.Get(), 1.f / 60.f);
	TestFalse(TEXT("cvar back to 0: stopped"), Director->GetProfiler().IsRecording());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFProfilerOutputUnchangedTest, "ActionCameraFramework.Profiler.OutputUnchangedWithoutWorld", ACFTests::Flags)
bool FACFProfilerOutputUnchangedTest::RunTest(const FString& Parameters)
{
	using namespace ACFProfilerTests; // not at file scope: it would leak ACFTests::Flags into unity builds
	// Two directors fed identically; only one records. Requests, blends (interrupted too), effects and a camera
	// sequence must give bit-identical results every frame.
	FCVarGuard Off(TEXT("ACF.Profile"), 0);
	TStrongObjectPtr<UACFCameraDirector> Plain(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraDirector> Profiled(NewObject<UACFCameraDirector>());
	Profiled->SetProfilerForced(true);
	TArray<TStrongObjectPtr<UObject>> Keep;
	auto Fixed = [&Keep](const FVector& Location, float Fov, float Blend)
	{
		UACFCameraPreset* Preset = MakeFixedPreset(Location, Fov, Blend > 0.f ? Linear(Blend) : FACFCameraBlendSettings::MakeCut());
		Keep.Emplace(Preset);
		return Preset;
	};
	UACFCameraEffectPreset* Punch = MakePunch(-6.f, 0.1f);
	Keep.Emplace(Punch);
	UACFCameraSequence* Sequence = NewObject<UACFCameraSequence>(GetTransientPackage(), NAME_None, RF_Transient);
	Keep.Emplace(Sequence);
	for (int32 I = 0; I < 2; ++I)
	{
		FACFCameraShot& Shot = Sequence->Shots.AddDefaulted_GetRef();
		Shot.Preset = Fixed(FVector(300.f * I, 900.f, 200.f), 50.f, 0.f);
		Shot.Duration = 0.4f;
		Shot.BlendIn = I == 0 ? Linear(0.2f) : FACFCameraBlendSettings::MakeCut();
		Shot.Effects.Add({ Punch, 1.f, EACFCameraShotEffectLifetime::Shot });
	}
	Sequence->ReturnBlend = Linear(0.3f);

	FFrameDriver DriverA;
	FFrameDriver DriverB;
	int32 Mismatches = 0;
	int32 FirstMismatch = INDEX_NONE;
	FACFCameraRequestHandle HandlesA[3];
	FACFCameraRequestHandle HandlesB[3];
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		// Identical commands on both directors.
		auto Both = [&](TFunctionRef<void(UACFCameraDirector&, FACFCameraRequestHandle*)> Command)
		{
			Command(*Plain, HandlesA);
			Command(*Profiled, HandlesB);
		};
		if (Frame == 0)
		{
			Both([&](UACFCameraDirector& D, FACFCameraRequestHandle* H) { H[0] = D.AddRequest(Fixed(FVector(0, 0, 100), 90.f, 0.f), FACFCameraRequestParams()); });
		}
		if (Frame == 20)
		{
			FACFCameraRequestParams High;
			High.Priority = 5;
			Both([&](UACFCameraDirector& D, FACFCameraRequestHandle* H) { H[1] = D.AddRequest(Fixed(FVector(500, 0, 300), 70.f, 0.5f), High); });
		}
		if (Frame == 35) // interrupts the running blend
		{
			FACFCameraRequestParams Higher;
			Higher.Priority = 9;
			Both([&](UACFCameraDirector& D, FACFCameraRequestHandle* H) { H[2] = D.AddRequest(Fixed(FVector(-200, 400, 150), 60.f, 0.3f), Higher); });
		}
		if (Frame == 80)
		{
			Both([&](UACFCameraDirector& D, FACFCameraRequestHandle* H) { D.RemoveRequest(H[2]); });
		}
		if (Frame % 25 == 5)
		{
			Both([&](UACFCameraDirector& D, FACFCameraRequestHandle*) { D.PlayEffect(Punch, FACFCameraEffectParams()); });
		}
		if (Frame == 120)
		{
			Both([&](UACFCameraDirector& D, FACFCameraRequestHandle*) { D.PlaySequence(Sequence, FACFCameraSequenceParams()); });
		}
		const FACFCameraEvaluationResult A = DriverA.Step(Plain.Get(), 1.f / 60.f);
		const FACFCameraEvaluationResult B = DriverB.Step(Profiled.Get(), 1.f / 60.f);
		if (!SameResult(A, B) || (A.ActiveRequest.IsValid() != B.ActiveRequest.IsValid()))
		{
			++Mismatches;
			FirstMismatch = FirstMismatch == INDEX_NONE ? Frame : FirstMismatch;
		}
	}
	TestEqual(FString::Printf(TEXT("Profiled and plain results are bit-identical (first mismatch at frame %d)"), FirstMismatch), Mismatches, 0);
	TestEqual(TEXT("The plain director recorded nothing"), Plain->GetProfiler().Num(), 0);
	TestEqual(TEXT("The profiled director recorded every frame (capacity permitting)"), Profiled->GetProfiler().Num(), FMath::Min(240, Profiled->GetProfiler().Capacity()));
	TestEqual(TEXT("No world: no collision queries"), Profiled->GetProfiler().GetLatestFrame()->GetFrameQueries(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFProfilerAllocationCounterTest, "ActionCameraFramework.Profiler.AllocationCounter", ACFTests::Flags)
bool FACFProfilerAllocationCounterTest::RunTest(const FString& Parameters)
{
	using namespace ACFProfilerTests; // not at file scope: it would leak ACFTests::Flags into unity builds
	// Validates the measuring instrument itself: it counts game-thread allocations made inside an active ACF scope
	// (size and call stack) and nothing outside one.
	if (!FACFCameraAllocationCounter::Install())
	{
		AddWarning(TEXT("Allocation counter not available on this platform."));
		return true;
	}
	FCVarGuard Off(TEXT("ACF.Profile"), 0);
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	FACFCameraProfiler& Profiler = Director->GetProfiler();
	Profiler.Start(8);
	FACFCameraAllocationCounter::ResetSites();
	FACFCameraAllocationCounter::SetCounting(true);
	{
		FACFCameraProfiler::FActiveScope Active(&Profiler, 1, 1.f / 60.f, true, false);
		ACF_PROFILE_SCOPE(Effects);
		TArray<int32> Values;
		Values.SetNum(1000); // 4000 bytes
	}
	const FACFCameraProfileFrame Inside = *Profiler.GetLatestFrame();
	{
		TArray<int32> Outside;
		Outside.SetNum(1000); // no active scope: not counted
	}
	FACFCameraAllocationCounter::SetCounting(false);
	TArray<FACFCameraAllocationCounter::FSite> Sites;
	uint32 Overflow = 0;
	FACFCameraAllocationCounter::GetSites(Sites, Overflow);

	TestEqual(TEXT("One allocation counted inside the scope"), static_cast<int32>(Inside.Allocations), 1);
	TestTrue(FString::Printf(TEXT("Its size is recorded (%llu bytes)"), Inside.AllocatedBytes), Inside.AllocatedBytes >= 4000);
	TestEqual(TEXT("Attributed to the section that was running"), static_cast<int32>(Inside.AllocationsBySection[static_cast<int32>(EACFCameraProfileSection::Effects)]), 1);
	TestEqual(TEXT("Nothing counted outside an active scope"), static_cast<int32>(Profiler.GetLatestFrame()->Allocations), 1);
	TestEqual(TEXT("One call stack captured"), Sites.Num(), 1);
	if (Sites.Num() == 1)
	{
		const FString Stack = FACFCameraAllocationCounter::DescribeSite(Sites[0], 6);
		AddInfo(FString::Printf(TEXT("Captured site:\n%s"), *Stack));
		TestTrue(TEXT("The call stack points at this test"), Stack.Contains(TEXT("ACFCameraProfilerTests")));
	}
	FACFCameraAllocationCounter::ResetSites();
	Profiler.Stop();
	return true;
}

// -------------------------------------------------------------------------------------------------
// Tests in a game world (ClientContext): real collision queries, the real PlayerCameraManager path.

namespace ACFProfilerTests
{
	static UWorld* FindGameWorld()
	{
		for (const FWorldContext& Context : GEngine ? GEngine->GetWorldContexts() : TIndirectArray<FWorldContext>())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World()->GetFirstPlayerController())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	using FStep = TFunction<bool(FAutomationTestBase&)>; // true = step done, false = run again next frame

	/** Runs one step per engine frame; a step may repeat (return false) up to a time limit. */
	class FStepsCommand : public IAutomationLatentCommand
	{
	public:
		FStepsCommand(FAutomationTestBase* InTest, TArray<FStep> InSteps) : Test(InTest), Steps(MoveTemp(InSteps)) {}
		virtual bool Update() override
		{
			if (GFrameCounter == LastFrame)
			{
				return false;
			}
			LastFrame = GFrameCounter;
			if (GetCurrentRunTime() > 60.0)
			{
				Test->AddError(FString::Printf(TEXT("Step %d timed out."), Index));
				return true;
			}
			if (Steps[Index](*Test))
			{
				++Index;
			}
			return Index >= Steps.Num();
		}
	private:
		FAutomationTestBase* Test;
		TArray<FStep> Steps;
		int32 Index = 0;
		uint64 LastFrame = 0;
	};

	/** Orbit (fixed, no input) + Look At + Collision with the deoccluder checking the look-at target. */
	static UACFCameraPreset* MakeOccludedPreset()
	{
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
		return Preset;
	}

	static AActor* SpawnPoint(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}

	static AActor* SpawnBox(UWorld* World, const FVector& Center, const FVector& Extent)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Center), Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Box"));
		Box->SetBoxExtent(Extent);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		Actor->SetActorLocation(Center);
		return Actor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFProfilerWorldQueriesTest, "ActionCameraFramework.Profiler.QueryCountsAndOutputInWorld",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFProfilerWorldQueriesTest::RunTest(const FString& Parameters)
{
	using namespace ACFProfilerTests;
	// Standalone directors evaluated with the game world (not attached to a camera manager), so the inputs are
	// identical for the profiled and the plain one. A pillar hides the look-at target: the deoccluder checks every
	// frame and searches once the occlusion is confirmed.
	struct FState
	{
		TStrongObjectPtr<UACFCameraDirector> Plain;
		TStrongObjectPtr<UACFCameraDirector> Profiled;
		TStrongObjectPtr<UACFCameraDirector> Other; // profiled, no collision node: must stay at zero queries
		TStrongObjectPtr<UACFCameraPreset> Preset;
		TStrongObjectPtr<UACFCameraPreset> Fixed;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TSharedPtr<FCVarGuard> Off;
	};
	TSharedRef<FState> S = MakeShared<FState>();
	TArray<FStep> Steps;
	Steps.Add([S](FAutomationTestBase& Test)
	{
		UWorld* World = FindGameWorld();
		if (!World)
		{
			return false;
		}
		S->Off = MakeShared<FCVarGuard>(TEXT("ACF.Profile"), 0);
		const FVector Base(-80000.f, -80000.f, 20000.f);
		AActor* Player = SpawnPoint(World, Base);
		AActor* Target = SpawnPoint(World, Base + FVector(600.f, 0.f, 0.f));
		S->Spawned = { Player, Target, SpawnBox(World, Base + FVector(300.f, 0.f, 0.f), FVector(25.f, 25.f, 300.f)) };
		S->Preset.Reset(MakeOccludedPreset());
		S->Fixed.Reset(ACFTests::MakeFixedPreset(Base + FVector(0, 0, 500), 70.f));
		S->Plain.Reset(NewObject<UACFCameraDirector>());
		S->Profiled.Reset(NewObject<UACFCameraDirector>());
		S->Other.Reset(NewObject<UACFCameraDirector>());
		S->Profiled->SetProfilerForced(true);
		S->Other->SetProfilerForced(true);
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = Player;
		Params.LookAtTarget.Actor = Target;
		S->Plain->AddRequest(S->Preset.Get(), Params);
		S->Profiled->AddRequest(S->Preset.Get(), Params);
		S->Other->AddRequest(S->Fixed.Get(), FACFCameraRequestParams());
		return true;
	});
	Steps.Add([S](FAutomationTestBase& Test)
	{
		UWorld* World = FindGameWorld();
		int32 Mismatches = 0;
		int32 CountMismatches = 0;
		int32 SearchFrames = 0;
		int32 SearchQueries = 0;
		int32 OtherQueries = 0;
		int32 WrongCollisionCount = 0;
		for (int32 Frame = 0; Frame < 180; ++Frame)
		{
			FACFCameraEvaluationContext Context;
			Context.World = World;
			Context.DeltaTime = 1.f / 60.f;
			Context.FrameNumber = 50000000ull + Frame;
			const FACFCameraEvaluationResult A = S->Plain->Evaluate(Context);
			const FACFCameraEvaluationResult B = S->Profiled->Evaluate(Context);
			S->Profiled->Evaluate(Context); // same-frame repeat: cached, must not count again
			S->Other->Evaluate(Context);
			Mismatches += SameResult(A, B) ? 0 : 1;

			const FACFCameraProfileFrame& Rec = *S->Profiled->GetProfiler().GetLatestFrame();
			const FACFCameraSolverReport& Report = S->Profiled->GetLastSolverReport();
			const int32 Deocc = Rec.GetQueries(EACFCameraQueryPurpose::DeoccluderCheck) + Rec.GetQueries(EACFCameraQueryPurpose::DeoccluderSearch);
			// The solver's own report counts one query per visibility test and per collided candidate (no start-inside retry here).
			CountMismatches += (Rec.FrameNumber == Context.FrameNumber && Deocc == Report.NumTraces) ? 0 : 1;
			WrongCollisionCount += Rec.Queries[static_cast<int32>(EACFCameraQueryPurpose::Collision)][static_cast<int32>(EACFCameraQueryShape::Sweep)] == 1 ? 0 : 1;
			if (Report.bSearched)
			{
				++SearchFrames;
				SearchQueries += Rec.GetQueries(EACFCameraQueryPurpose::DeoccluderSearch);
			}
			else
			{
				SearchQueries += Rec.GetQueries(EACFCameraQueryPurpose::DeoccluderSearch) * 1000; // must be 0 when not searching
			}
			OtherQueries += S->Other->GetProfiler().GetLatestFrame()->GetFrameQueries();
		}
		Test.TestEqual(TEXT("Profiled and plain results are bit-identical with real queries"), Mismatches, 0);
		Test.TestEqual(TEXT("Deoccluder queries per frame == the solver's own count (no carry-over between evaluations)"), CountMismatches, 0);
		Test.TestEqual(TEXT("Exactly one collision sweep per evaluation (repeats of a frame not counted)"), WrongCollisionCount, 0);
		Test.TestTrue(FString::Printf(TEXT("The deoccluder searched (%d frames)"), SearchFrames), SearchFrames > 0);
		Test.TestTrue(TEXT("Search queries only in searching frames"), SearchQueries > 0 && SearchQueries < 1000);
		Test.TestEqual(TEXT("A director without collision (another player) records no queries"), OtherQueries, 0);
		Test.TestEqual(TEXT("Each profiled director keeps its own records"), S->Other->GetProfiler().Num(), FMath::Min(180, S->Other->GetProfiler().Capacity()));
		Test.TestNull(TEXT("No active profiler after the evaluations"), FACFCameraProfiler::GetActive());
		for (const TWeakObjectPtr<AActor>& Actor : S->Spawned)
		{
			if (Actor.IsValid())
			{
				Actor->Destroy();
			}
		}
		S->Off.Reset();
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FStepsCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFProfilerPauseAndPlayersTest, "ActionCameraFramework.Profiler.PauseAndLocalPlayers",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFProfilerPauseAndPlayersTest::RunTest(const FString& Parameters)
{
	using namespace ACFProfilerTests;
	// Real camera path (PlayerCameraManager -> output modifier). The second local player's controller is created by
	// the host project's GameMode; its BeginPlay may log a UMG error unrelated to ACF (see the Sequencer tests).
	AddExpectedErrorPlain(TEXT("CreateWidget cannot be used on Player Controller with no attached player"), EAutomationExpectedErrorFlags::Contains, -1);
	struct FState
	{
		TSharedPtr<FCVarGuard> On;
		TStrongObjectPtr<UACFCameraPreset> PresetA;
		TStrongObjectPtr<UACFCameraPreset> PresetB;
		TWeakObjectPtr<UACFCameraSubsystem> Acf;
		TWeakObjectPtr<ULocalPlayer> Second;
		FACFCameraRequestHandle Handle;
		int32 Mark = 0;
		uint64 MarkFrame = 0;
		bool bPaused = false;
	};
	TSharedRef<FState> S = MakeShared<FState>();
	auto Director = [S]() { return S->Acf.IsValid() ? S->Acf->GetDirector() : nullptr; };
	TArray<FStep> Steps;
	Steps.Add([S](FAutomationTestBase& Test)
	{
		UWorld* World = FindGameWorld();
		if (!World)
		{
			return false;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		S->Acf = PC->GetLocalPlayer()->GetSubsystem<UACFCameraSubsystem>();
		S->On = MakeShared<FCVarGuard>(TEXT("ACF.Profile"), 1);
		S->PresetA.Reset(ACFTests::MakeFixedPreset(FVector(-70000.f, -70000.f, 20000.f), 75.f));
		S->Handle = S->Acf->RequestCamera(S->PresetA.Get(), FACFCameraRequestParams());
		S->MarkFrame = GFrameCounter;
		return true;
	});
	Steps.Add([S, Director](FAutomationTestBase& Test) { return GFrameCounter - S->MarkFrame >= 10; });
	Steps.Add([S, Director](FAutomationTestBase& Test)
	{
		const FACFCameraProfiler& Profiler = Director()->GetProfiler();
		Test.TestTrue(FString::Printf(TEXT("cvar on: recording through the camera manager (%d frames)"), Profiler.Num()), Profiler.Num() >= 8);
		Test.TestTrue(TEXT("Each frame has one evaluation"), Profiler.GetLatestFrame() && Profiler.GetLatestFrame()->NumEvaluations == 1);
		S->bPaused = UGameplayStatics::SetGamePaused(FindGameWorld(), true);
		S->Mark = Profiler.Num();
		S->MarkFrame = GFrameCounter;
		return true;
	});
	Steps.Add([S](FAutomationTestBase& Test) { return GFrameCounter - S->MarkFrame >= 10; });
	Steps.Add([S, Director](FAutomationTestBase& Test)
	{
		if (S->bPaused)
		{
			// No camera update while paused (UE skips it): nothing may be recorded or accumulated.
			Test.TestEqual(TEXT("Paused: no new frames recorded"), Director()->GetProfiler().Num(), S->Mark);
			UGameplayStatics::SetGamePaused(FindGameWorld(), false);
		}
		else
		{
			Test.AddWarning(TEXT("The game mode refused to pause: pause check skipped."));
		}
		// Second local player with its own camera.
		FString Error;
		S->Second = FindGameWorld()->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(1), Error, true);
		if (!S->Second.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("Could not create a second local player: %s"), *Error));
			return true;
		}
		S->MarkFrame = GFrameCounter;
		return true;
	});
	Steps.Add([S](FAutomationTestBase& Test) { return !S->Second.IsValid() || GFrameCounter - S->MarkFrame >= 5; });
	Steps.Add([S](FAutomationTestBase& Test)
	{
		if (UACFCameraSubsystem* Acf2 = S->Second.IsValid() ? S->Second->GetSubsystem<UACFCameraSubsystem>() : nullptr)
		{
			S->PresetB.Reset(ACFTests::MakeFixedPreset(FVector(-60000.f, -70000.f, 20000.f), 65.f));
			Acf2->RequestCamera(S->PresetB.Get(), FACFCameraRequestParams());
		}
		S->MarkFrame = GFrameCounter;
		return true;
	});
	Steps.Add([S](FAutomationTestBase& Test) { return GFrameCounter - S->MarkFrame >= 10; });
	Steps.Add([S, Director](FAutomationTestBase& Test)
	{
		UACFCameraSubsystem* Acf2 = S->Second.IsValid() ? S->Second->GetSubsystem<UACFCameraSubsystem>() : nullptr;
		if (!Acf2)
		{
			Test.AddError(TEXT("Second player's ACF subsystem missing."));
			return true;
		}
		const FACFCameraProfiler& P1 = Director()->GetProfiler();
		const FACFCameraProfiler& P2 = Acf2->GetDirector()->GetProfiler();
		Test.TestTrue(TEXT("Player 2 records on its own profiler"), P2.Num() > 0 && &P1 != &P2);
		// Same engine frames, separate records: one evaluation each, never two (no double counting across players).
		const FACFCameraProfileFrame* L1 = P1.GetLatestFrame();
		const FACFCameraProfileFrame* L2 = P2.GetLatestFrame();
		Test.TestTrue(TEXT("Both players recorded the same engine frame"), L1 && L2 && L1->FrameNumber == L2->FrameNumber);
		Test.TestTrue(TEXT("One evaluation per player per frame"), L1 && L2 && L1->NumEvaluations == 1 && L2->NumEvaluations == 1);
		// Removing the player destroys its subsystem and director: nothing may stay active.
		FindGameWorld()->GetGameInstance()->RemoveLocalPlayer(S->Second.Get());
		S->MarkFrame = GFrameCounter;
		return true;
	});
	Steps.Add([S](FAutomationTestBase& Test) { return GFrameCounter - S->MarkFrame >= 5; });
	Steps.Add([S, Director](FAutomationTestBase& Test)
	{
		Test.TestNull(TEXT("No profiler left active after the player was removed"), FACFCameraProfiler::GetActive());
		Test.TestTrue(TEXT("Player 1 keeps recording"), Director()->GetProfiler().GetLatestFrame()->FrameNumber >= S->MarkFrame);
		S->Acf->ReleaseCamera(S->Handle);
		S->On.Reset(); // cvar back to its previous value
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FStepsCommand(this, MoveTemp(Steps)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && ACF_WITH_PROFILER
