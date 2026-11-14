#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestEnvironment.h"
#include "ACFCameraTuning.h"
#include "ACFCameraZoneComponent.h"
#include "ACFCameraZoneSubsystem.h"
#include "ACFCameraZoneValidation.h"
#include "EngineUtils.h"

/**
 * Phase 7C-3A: the Camera Zone sample of L_ACFCameraTest in the real game (samples / samples-host contexts; skipped on other
 * maps). The pawn WALKS (moves a little every frame, never a teleport) from the entrance through the plaza, the corridor
 * (higher priority, Enter Delay) and back, under the sample Lock-On, through the sample Level Sequence, into the alcove (priority
 * 2 inside the plaza, Keep View); the real camera follows the zone requests. README 32 章 "サンプル".
 */
namespace ACFZoneSampleTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
	const FVector Entrance(-1800.0, 2900.0, 0.0);
	const FVector PlazaPoint(-2600.0, 3300.0, 0.0);
	const FVector CorridorPoint(-3000.0, 2400.0, 0.0);
	// Around the corridor's north wall (x -3800..-2400 at y 2570): through its open east end.
	const FVector CorridorGateNorth(-2250.0, 3000.0, 0.0);
	const FVector CorridorGate(-2250.0, 2400.0, 0.0);
	const FVector AlcovePoint(-3450.0, 3400.0, 0.0);

	struct FFlow
	{
		FAutomationTestBase* Test = nullptr;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<UACFCameraSubsystem> Acf;
		TWeakObjectPtr<UACFCameraZoneSubsystem> Zones;
		ACFTestEnvironment::FGlobalStateGuard Guard;
		FVector StartLocation = FVector::ZeroVector;
		FRotator StartRotation = FRotator::ZeroRotator;
		FRotator SavedControlRotation = FRotator::ZeroRotator;
		TMap<FString, TWeakObjectPtr<UACFCameraZoneComponent>> Sample;
		double TimeMark = 0.0;
		bool bStarted = false;
		bool bSawBlend = false;
		bool bSawPending = false;
		bool bSawBypass = false;
		int32 MaxZoneRequests = 0;
		/** The rendered view while the plaza camera was in control (the last one seen). */
		FRotator LastPlazaView = FRotator::ZeroRotator;
		double Yaw() const { return Controller->PlayerCameraManager->GetCameraRotation().Yaw; }

		APawn* Pawn() const { return Controller.IsValid() ? Controller->GetPawn() : nullptr; }
		ULocalPlayer* Player() const { return Controller.IsValid() ? Controller->GetLocalPlayer() : nullptr; }
		UACFCameraZoneComponent* Selected() const { return Zones->GetSelectedZone(Player()); }
		FACFCameraRequestHandle ZoneRequest() const { return Zones->GetZoneRequest(Player()); }
		FACFCameraEvaluationResult Result() const { return Acf->GetLastEvaluationResult(); }
		bool Is(const TCHAR* Name) const { return Selected() && Selected() == Sample.FindRef(Name).Get(); }
		double Now() const { return World->GetTimeSeconds(); }

		int32 NumZoneRequests() const
		{
			TArray<FACFCameraRequestInfo> Infos;
			Acf->GetDirector()->GetRequestInfos(Infos);
			return Infos.FilterByPredicate([](const FACFCameraRequestInfo& Info) { return Info.DebugName.ToString().StartsWith(TEXT("Zone ")); }).Num();
		}

		/** Walks the pawn towards Target (XY, at 500 cm/s of game time) and watches the zone state; true when arrived. */
		bool WalkTo(const FVector& Target)
		{
			APawn* P = Pawn();
			const FVector From = P->GetActorLocation();
			const FVector Goal(Target.X, Target.Y, From.Z);
			const double Step = 500.0 * FMath::Max(World->GetDeltaSeconds(), 1e-3f);
			const FVector Delta = Goal - From;
			P->SetActorLocation(Delta.Size() <= Step ? Goal : From + Delta.GetSafeNormal() * Step);
			Watch();
			return FVector::Dist2D(P->GetActorLocation(), Goal) < 1.0;
		}

		void Watch()
		{
			bSawBlend |= Result().bIsBlending;
			UACFCameraZoneComponent* Pending = nullptr;
			float Elapsed = 0.f;
			bSawPending |= Zones->GetPendingZone(Player(), Pending, Elapsed);
			bSawBypass |= Result().bOutputBypassed;
			MaxZoneRequests = FMath::Max(MaxZoneRequests, NumZoneRequests());
			if (Is(TEXT("Plaza")) && Result().ActiveRequest == ZoneRequest() && !Result().bIsBlending && Controller->PlayerCameraManager)
			{
				LastPlazaView = Controller->PlayerCameraManager->GetCameraRotation();
			}
		}

		bool Wait(double Seconds)
		{
			Watch();
			return Now() >= TimeMark + Seconds;
		}

		bool Start(FAutomationTestBase& InTest)
		{
			Test = &InTest;
			World = ACFTestEnvironment::FindGameWorld();
			if (!World.IsValid() || !World->GetMapName().Contains(TEXT("L_ACFCameraTest")))
			{
				InTest.AddWarning(FString::Printf(TEXT("Skipped: run on /ActionCameraFramework/Samples/L_ACFCameraTest (current %s)."), World.IsValid() ? *World->GetMapName() : TEXT("none")));
				return false;
			}
			Controller = World->GetFirstPlayerController();
			Acf = Player() ? Player()->GetSubsystem<UACFCameraSubsystem>() : nullptr;
			Zones = World->GetSubsystem<UACFCameraZoneSubsystem>();
			if (!Pawn() || !Acf.IsValid() || !Zones.IsValid())
			{
				InTest.AddError(TEXT("The test map has no pawn / camera / zone subsystem."));
				return false;
			}
			for (TActorIterator<AActor> It(World.Get()); It; ++It)
			{
				for (const TCHAR* Name : { TEXT("Plaza"), TEXT("Corridor"), TEXT("Alcove") })
				{
					if (It->ActorHasTag(FName(*FString::Printf(TEXT("ACFSampleZone_%s"), Name))))
					{
						Sample.Add(Name, It->FindComponentByClass<UACFCameraZoneComponent>());
					}
				}
			}
			if (!InTest.TestEqual(TEXT("The three sample zones (Scripts/CreateZoneSampleAssets.py)"), Sample.Num(), 3))
			{
				return false;
			}
			Guard.Capture(World.Get());
			ACFTestEnvironment::BeginScenario(*Acf);
			StartLocation = Pawn()->GetActorLocation();
			StartRotation = Pawn()->GetActorRotation();
			SavedControlRotation = Controller->GetControlRotation();
			bStarted = true;
			return true;
		}

		void Cleanup()
		{
			if (!bStarted)
			{
				return;
			}
			bStarted = false;
			GEngine->Exec(World.Get(), TEXT("ACF.Sample.LockOff 0"));
			GEngine->Exec(World.Get(), TEXT("ACF.Sample.Sequencer Stop"));
			if (Controller.IsValid())
			{
				Controller->SetControlRotation(SavedControlRotation);
			}
			Test->TestEqual(TEXT("End: no zone request left"), NumZoneRequests(), 0);
			ACFTestEnvironment::EndScenario(*Test, Acf.Get(), Controller.IsValid() ? Controller->PlayerCameraManager.Get() : nullptr, Guard);
		}
	};

	using FStep = TFunction<bool(FFlow&)>;

	class FFlowCommand : public IAutomationLatentCommand
	{
	public:
		FFlowCommand(TSharedRef<FFlow> InFlow, TArray<FStep> InSteps) : Flow(InFlow), Steps(MoveTemp(InSteps)) {}
		virtual bool Update() override
		{
			if (GetCurrentRunTime() > 120.0)
			{
				Flow->Test->AddError(FString::Printf(TEXT("Zone sample flow: step %d timed out."), Index));
				Flow->Cleanup();
				return true;
			}
			if (!Flow->World.IsValid() || !Flow->Pawn() || !Flow->Acf.IsValid())
			{
				Flow->Test->AddError(TEXT("World / pawn / camera lost during the flow."));
				Flow->Cleanup();
				return true;
			}
			if (Index < Steps.Num() && Steps[Index](*Flow))
			{
				Flow->TimeMark = Flow->Now();
				++Index;
			}
			if (Index >= Steps.Num())
			{
				Flow->Cleanup();
				return true;
			}
			return false;
		}
	private:
		TSharedRef<FFlow> Flow;
		TArray<FStep> Steps;
		int32 Index = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneSampleTest, "ActionCameraFramework.Samples.ZonesOnTestMap", ACFZoneSampleTests::Flags)
bool FACFZoneSampleTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneSampleTests;
	TSharedRef<FFlow> Flow = MakeShared<FFlow>();
	if (!Flow->Start(*this))
	{
		return true;
	}
#if WITH_EDITOR
	for (const TPair<FString, TWeakObjectPtr<UACFCameraZoneComponent>>& Entry : Flow->Sample)
	{
		TArray<FACFCameraZoneIssue> Issues;
		ACFCameraZoneValidation::Validate(*Entry.Value.Get(), Issues);
		TestTrue(FString::Printf(TEXT("Sample zone %s: no error / warning"), *Entry.Key), ACFCameraZoneValidation::MaxSeverity(Issues) == EACFCameraPresetIssueSeverity::Info);
	}
#endif
	TArray<FStep> Steps;
	Steps.Add([](FFlow& F)
	{
		GEngine->Exec(F.World.Get(), TEXT("ACF.Sample.Zone Go")); // the entrance, outside the zones (a teleport)
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Wait(0.2); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Entrance: no zone"), F.Selected() == nullptr && !F.ZoneRequest().IsValid());
		F.bSawBlend = false;
		return true;
	});
	// Walk into the plaza: its camera blends in (1 s).
	Steps.Add([](FFlow& F) { return F.WalkTo(PlazaPoint); });
	Steps.Add([](FFlow& F) { return F.Wait(1.3); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Plaza selected"), F.Is(TEXT("Plaza")));
		F.Test->TestTrue(TEXT("Plaza camera in control"), F.Result().ActiveRequest == F.ZoneRequest() && F.ZoneRequest().IsValid());
		F.Test->TestTrue(TEXT("Entering blended (not a cut)"), F.bSawBlend);
		F.bSawPending = false;
		F.bSawBlend = false;
		return true;
	});
	// Into the corridor (priority 1 inside the plaza, Enter Delay 0.2 s), through its open east end.
	Steps.Add([](FFlow& F) { return F.WalkTo(CorridorGateNorth); });
	Steps.Add([](FFlow& F) { return F.WalkTo(CorridorGate); });
	Steps.Add([](FFlow& F) { return F.WalkTo(CorridorPoint); });
	Steps.Add([](FFlow& F) { return F.Wait(1.0); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Corridor selected (higher priority inside the plaza)"), F.Is(TEXT("Corridor")));
		F.Test->TestTrue(TEXT("The change waited for the Enter Delay (pending seen)"), F.bSawPending);
		F.Test->TestTrue(TEXT("Corridor camera in control, blended"), F.Result().ActiveRequest == F.ZoneRequest() && F.bSawBlend);
		// The sample Lock-On on the zone enemy, then walk back to the plaza under it.
		GEngine->Exec(F.World.Get(), TEXT("ACF.Sample.LockOn 7"));
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Wait(0.5); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Lock-On above the zone"), F.Result().ActiveRequest.IsValid() && F.Result().ActiveRequest != F.ZoneRequest());
		return true;
	});
	Steps.Add([](FFlow& F) { return F.WalkTo(CorridorGate); });
	Steps.Add([](FFlow& F) { return F.WalkTo(CorridorGateNorth); });
	Steps.Add([](FFlow& F) { return F.WalkTo(PlazaPoint); });
	Steps.Add([](FFlow& F) { return F.Wait(0.6); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Under the Lock-On: the plaza selected and requested"), F.Is(TEXT("Plaza")) && F.ZoneRequest().IsValid());
		F.Test->TestTrue(TEXT("... the Lock-On keeps the screen"), F.Result().ActiveRequest != F.ZoneRequest());
		GEngine->Exec(F.World.Get(), TEXT("ACF.Sample.LockOff 0.5"));
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Wait(1.0); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("After the Lock-On: the plaza camera in control"), F.Result().ActiveRequest == F.ZoneRequest());
		// The sample Level Sequence (camera cuts take the screen), then back to the zone camera.
		F.bSawBypass = false;
		GEngine->Exec(F.World.Get(), TEXT("ACF.Sample.Sequencer Play Blend 2"));
		return true;
	});
	Steps.Add([](FFlow& F) { F.Watch(); return F.bSawBypass || F.Wait(3.0); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("The Level Sequence took the screen"), F.bSawBypass);
		return true;
	});
	Steps.Add([](FFlow& F) { F.Watch(); return (!F.Result().bOutputBypassed && F.Wait(1.5)) || F.Wait(20.0); });
	Steps.Add([](FFlow& F)
	{
		const FACFCameraEvaluationResult R = F.Result();
		F.Test->TestFalse(TEXT("The sequence ended"), R.bOutputBypassed);
		F.Test->TestTrue(TEXT("After the sequence: the plaza camera in control again"), F.Is(TEXT("Plaza")) && R.ActiveRequest == F.ZoneRequest());
		F.Test->TestTrue(TEXT("... on screen with full weight"), FMath::IsNearlyEqual(R.Weight, 1.f, 1e-3f));
		return true;
	});
	// The alcove: priority 2 inside the plaza; Enter Delay 0.5 s.
	Steps.Add([](FFlow& F) { return F.WalkTo(AlcovePoint); });
	Steps.Add([](FFlow& F) { return F.Wait(1.5); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Alcove selected (higher priority inside the plaza)"), F.Is(TEXT("Alcove")));
		F.Test->TestTrue(TEXT("Alcove camera in control"), F.Result().ActiveRequest == F.ZoneRequest());
		F.Test->TestEqual(TEXT("Never more than one zone request"), F.MaxZoneRequests, 1);
		// Keep View (the sample alcove): its close orbit starts from the plaza camera's view.
		F.Test->TestTrue(FString::Printf(TEXT("Keep View: alcove yaw %.1f == plaza view yaw %.1f"), F.Yaw(), F.LastPlazaView.Yaw), FMath::Abs(FRotator::NormalizeAxis(F.Yaw() - F.LastPlazaView.Yaw)) < 3.0);
		return true;
	});
	// Use Camera Setting: the alcove orbit starts from its preset's Initial Rotation (the UE camera, here yaw 0).
	Steps.Add([](FFlow& F) { return F.WalkTo(PlazaPoint); });
	Steps.Add([](FFlow& F) { return F.Wait(1.2); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Back in the plaza"), F.Is(TEXT("Plaza")));
		GEngine->Exec(F.World.Get(), TEXT("ACF.Sample.Zone KeepView Off"));
		F.Controller->SetControlRotation(FRotator(-15.0, 0.0, 0.0));
		return true;
	});
	Steps.Add([](FFlow& F) { return F.WalkTo(AlcovePoint); });
	Steps.Add([](FFlow& F) { return F.Wait(1.5); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Alcove again"), F.Is(TEXT("Alcove")));
		F.Test->TestTrue(FString::Printf(TEXT("Use Camera Setting: alcove yaw %.1f is not the plaza view yaw %.1f"), F.Yaw(), F.LastPlazaView.Yaw), FMath::Abs(FRotator::NormalizeAxis(F.Yaw() - F.LastPlazaView.Yaw)) > 20.0);
		GEngine->Exec(F.World.Get(), TEXT("ACF.Sample.Zone KeepView On"));
		// Teleport (ACF.Sample.Zone Go: NotifyTeleport) into the corridor: selected at once, a cut.
		GEngine->Exec(F.World.Get(), TEXT("ACF.Sample.Zone Go Corridor"));
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Wait(0.1); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Teleport: corridor at once (no Enter Delay)"), F.Is(TEXT("Corridor")) && F.Result().ActiveRequest == F.ZoneRequest());
		F.Test->TestFalse(TEXT("Teleport: a cut (not the corridor's 0.8 s blend)"), F.Result().bIsBlending);
		// Back to the start (a teleport out of the zones): the zone request goes.
		F.Pawn()->TeleportTo(F.StartLocation, F.StartRotation);
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Wait(0.3); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Back at the start: no zone"), F.Selected() == nullptr && !F.ZoneRequest().IsValid());
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FFlowCommand(Flow, MoveTemp(Steps)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
