#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestEnvironment.h"
#include "Core/ACFCameraTestUtils.h"
#include "ACFCameraTuning.h"
#include "ACFCameraZone.h"
#include "ACFCameraZoneComponent.h"
#include "ACFCameraZoneSettings.h"
#include "ACFCameraZoneSubsystem.h"
#include "Components/BoxComponent.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 7C-1B: zone camera requests in the real game World (README 32 章): the frame driver requests the camera of the
 * pawn's zone through the player's ACF camera subsystem (also when ACF had no request before), the real camera shows it,
 * and the request goes when the zone is disabled, destroyed, streamed out or the zone requests are released (World end);
 * a request released by the game comes back after the retry delay; two local players are independent; leaving to a Control
 * Rotation exploration camera gives the control rotation the zone's view.
 */
namespace ACFZoneWorldRequestTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	struct FFlow
	{
		FAutomationTestBase* Test = nullptr;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UACFCameraZoneSubsystem> Zones;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<UACFCameraSubsystem> Acf;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TArray<TStrongObjectPtr<UObject>> Keep;
		ACFTestEnvironment::FGlobalStateGuard Guard;
		float SavedRetryDelay = 1.f;
		float SavedRetryMaxDelay = 8.f;
		FRotator SavedControlRotation = FRotator::ZeroRotator;
		bool bStarted = false;

		uint64 Mark = 0;
		double TimeMark = 0.0;
		int32 RequestsAtStart = 0;
		FVector Origin = FVector::ZeroVector;
		TWeakObjectPtr<AACFCameraZone> A;
		TWeakObjectPtr<AACFCameraZone> B;
		FACFCameraRequestHandle Handle;
		FACFCameraRequestHandle Explore;
		TWeakObjectPtr<ULocalPlayer> Second;
		TWeakObjectPtr<AACFCameraZone> Second1;
		TWeakObjectPtr<AACFCameraZone> Second2;

		ULocalPlayer* Player() const { return Controller.IsValid() ? Controller->GetLocalPlayer() : nullptr; }
		UACFCameraDirector* Director() const { return Acf.IsValid() ? Acf->GetDirector() : nullptr; }
		APawn* Pawn() const { return Controller.IsValid() ? Controller->GetPawn() : nullptr; }
		APlayerCameraManager* PCM() const { return Controller.IsValid() ? Controller->PlayerCameraManager.Get() : nullptr; }

		/** Starts the scenario on the first player. False (with a warning) if this is not a game World with a pawn. */
		bool Start(FAutomationTestBase& InTest)
		{
			Test = &InTest;
			World = ACFTestEnvironment::FindGameWorld();
			Zones = World.IsValid() ? World->GetSubsystem<UACFCameraZoneSubsystem>() : nullptr;
			Controller = World.IsValid() ? World->GetFirstPlayerController() : nullptr;
			Acf = Player() ? Player()->GetSubsystem<UACFCameraSubsystem>() : nullptr;
			if (!World.IsValid() || !World->HasBegunPlay() || !Zones.IsValid() || !Acf.IsValid() || !Pawn() || !PCM())
			{
				InTest.AddWarning(TEXT("Skipped: needs a game World with the first player's pawn and camera (run with -game)."));
				return false;
			}
			UACFCameraZoneSettings* Settings = GetMutableDefault<UACFCameraZoneSettings>();
			Guard.Capture(World.Get(), { Settings });
			SavedRetryDelay = Settings->RequestRetryDelay;
			SavedRetryMaxDelay = Settings->RequestRetryMaxDelay;
			SavedControlRotation = Controller->GetControlRotation();
			ACFTestEnvironment::BeginScenario(*Acf);
			RequestsAtStart = Director()->GetNumRequests();
			Origin = Pawn()->GetActorLocation();
			bStarted = true;
			return true;
		}

		UACFCameraPreset* Fixed(const FVector& Location)
		{
			UACFCameraPreset* Preset = ACFTests::MakeFixedPreset(Location, 70.f);
			Keep.Emplace(Preset);
			return Preset;
		}

		UACFCameraPreset* ControlRotationOrbit()
		{
			UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
			Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
			Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
			UACFCameraNode_OrbitalFollow* Node = NewObject<UACFCameraNode_OrbitalFollow>(Preset);
			Node->Distance = 400.f;
			Node->InputSource = EACFCameraOrbitInputSource::ControlRotation;
			Node->InitialRotation = EACFCameraOrbitInitialRotation::ControlRotation;
			Node->bEnableMovementAssistance = false;
			Node->bRecenterWhenIdle = false;
			Preset->Nodes.Add(Node);
			Keep.Emplace(Preset);
			return Preset;
		}

		/** A box zone around Center (no rotation) with the preset. */
		AACFCameraZone* Zone(const FVector& Center, const FVector& Extent, int32 Priority, UACFCameraPreset* Preset)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AACFCameraZone* Zone = World->SpawnActor<AACFCameraZone>(Center, FRotator::ZeroRotator, Params);
			Zone->GetBox()->SetBoxExtent(Extent, false);
			Zone->GetZone()->ZonePriority = Priority;
			Zone->GetZone()->Preset = Preset;
			Spawned.Add(Zone);
			return Zone;
		}

		AActor* Actor(const FVector& Location)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Spawn = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
			Spawned.Add(Spawn);
			return Spawn;
		}

		/** Live requests of the player at the zone priority (there must never be more than one). */
		int32 NumZonePriorityRequests(UACFCameraDirector* InDirector = nullptr) const
		{
			UACFCameraDirector* D = InDirector ? InDirector : Director();
			if (!D)
			{
				return 0;
			}
			TArray<FACFCameraRequestInfo> Infos;
			D->GetRequestInfos(Infos);
			const int32 Priority = GetDefault<UACFCameraZoneSettings>()->ZoneRequestPriority;
			return Infos.FilterByPredicate([Priority](const FACFCameraRequestInfo& Info) { return Info.Priority == Priority; }).Num();
		}

		FACFCameraRequestInfo Info(FACFCameraRequestHandle InHandle) const
		{
			TArray<FACFCameraRequestInfo> Infos;
			if (UACFCameraDirector* D = Director())
			{
				D->GetRequestInfos(Infos);
			}
			const FACFCameraRequestInfo* Found = Infos.FindByPredicate([InHandle](const FACFCameraRequestInfo& Entry) { return Entry.Handle == InHandle; });
			return Found ? *Found : FACFCameraRequestInfo();
		}

		bool Waited(uint64 Frames) const { return GFrameCounter >= Mark + Frames; }

		void Cleanup()
		{
			if (!bStarted)
			{
				return;
			}
			bStarted = false;
			for (const TWeakObjectPtr<AActor>& Spawn : Spawned)
			{
				if (Spawn.IsValid() && !Spawn->IsActorBeingDestroyed())
				{
					Spawn->Destroy();
				}
			}
			Spawned.Reset();
			if (Acf.IsValid() && Explore.IsValid())
			{
				Acf->ReleaseCamera(Explore);
			}
			if (Second.IsValid())
			{
				ACFTestEnvironment::RemoveLocalPlayer(Second.Get());
			}
			UACFCameraZoneSettings* Settings = GetMutableDefault<UACFCameraZoneSettings>();
			Settings->RequestRetryDelay = SavedRetryDelay;
			Settings->RequestRetryMaxDelay = SavedRetryMaxDelay;
			if (Controller.IsValid())
			{
				Controller->SetControlRotation(SavedControlRotation);
			}
			if (Test)
			{
				Test->TestEqual(TEXT("End: no zone request left"), NumZonePriorityRequests(), 0);
				ACFTestEnvironment::EndScenario(*Test, Acf.Get(), PCM(), Guard);
			}
		}
	};

	/** A step returns true when it is done (the next one runs on a later frame). */
	using FStep = TFunction<bool(FFlow&)>;

	class FFlowCommand : public IAutomationLatentCommand
	{
	public:
		FFlowCommand(TSharedRef<FFlow> InFlow, TArray<FStep> InSteps) : Flow(InFlow), Steps(MoveTemp(InSteps)) {}
		virtual bool Update() override
		{
			if (++Frames > 1200)
			{
				Flow->Test->AddError(FString::Printf(TEXT("Zone request flow: step %d timed out."), Index));
				Flow->Cleanup();
				return true;
			}
			if (!Flow->World.IsValid() || !Flow->Acf.IsValid() || !Flow->Zones.IsValid())
			{
				Flow->Test->AddError(TEXT("World, camera subsystem or zone subsystem lost during the flow."));
				Flow->Cleanup();
				return true;
			}
			if (Index < Steps.Num() && Steps[Index](*Flow))
			{
				Flow->Mark = GFrameCounter;
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
		int32 Frames = 0;
	};

	inline bool Near(const FVector& A, const FVector& B, double Tolerance = 1.0) { return A.Equals(B, Tolerance); }
}

// One player: from no ACF request to the zone's camera on screen; priority / overlap; disable, enable, destroy, streaming
// removal; release by the game and the delayed retry; the World-end release; invalid / valid Look-At targets.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneWorldRequestLifecycleTest, "ActionCameraFramework.Zone.World.RequestLifecycle", ACFZoneWorldRequestTests::Flags)
bool FACFZoneWorldRequestLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneWorldRequestTests;
	TSharedRef<FFlow> Flow = MakeShared<FFlow>();
	if (!Flow->Start(*this))
	{
		return true;
	}
	TArray<FStep> Steps;
	Steps.Add([](FFlow& F)
	{
		if (F.RequestsAtStart == 0)
		{
			F.Test->AddInfo(TEXT("No ACF request before the zone: the zone starts the ACF camera."));
		}
		GetMutableDefault<UACFCameraZoneSettings>()->RequestRetryDelay = 0.25f;
		GetMutableDefault<UACFCameraZoneSettings>()->RequestRetryMaxDelay = 0.5f;
		// A around the pawn, its Look-At target already destroyed (an invalid target: no Look-At, no failure).
		AActor* Gone = F.Actor(F.Origin + FVector(0.0, 0.0, 1000.0));
		AACFCameraZone* A = F.Zone(F.Origin, FVector(300.0, 300.0, 300.0), 0, F.Fixed(F.Origin + FVector(-600.0, 0.0, 300.0)));
		A->GetZone()->LookAtTarget = Gone;
		Gone->Destroy();
		F.A = A;
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(3); });
	Steps.Add([](FFlow& F)
	{
		F.Handle = F.Zones->GetZoneRequest(F.Player());
		F.Test->TestTrue(TEXT("A's request alive"), F.Handle.IsValid() && F.Director()->HasRequest(F.Handle));
		F.Test->TestTrue(TEXT("A's request in control"), F.Director()->GetActiveRequest() == F.Handle);
		F.Test->TestEqual(TEXT("One zone request"), F.NumZonePriorityRequests(), 1);
		F.Test->TestTrue(TEXT("Destroyed Look-At target: none in the request"), F.Info(F.Handle).LookAtTarget.IsEmpty());
		F.Test->TestFalse(TEXT("Follow target: the pawn"), F.Info(F.Handle).FollowTarget.IsEmpty());
		if (F.RequestsAtStart == 0)
		{
			const FVector Expected = F.Origin + FVector(-600.0, 0.0, 300.0);
			F.Test->TestTrue(FString::Printf(TEXT("The real camera shows A (%s == %s)"), *F.PCM()->GetCameraLocation().ToString(), *Expected.ToString()), Near(F.PCM()->GetCameraLocation(), Expected));
		}
		// B: higher priority, smaller, around the pawn, with a valid Look-At target.
		AACFCameraZone* B = F.Zone(F.Origin, FVector(100.0, 100.0, 300.0), 1, F.Fixed(F.Origin + FVector(0.0, -600.0, 300.0)));
		B->GetZone()->LookAtTarget = F.Actor(F.Origin + FVector(0.0, 0.0, 1000.0));
		F.B = B;
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(3); });
	Steps.Add([](FFlow& F)
	{
		const FACFCameraRequestHandle HandleB = F.Zones->GetZoneRequest(F.Player());
		F.Test->TestTrue(TEXT("Overlap: B (priority 1) selected"), F.Zones->GetSelectedZone(F.Player()) == F.B->GetZone());
		F.Test->TestTrue(TEXT("B's request replaced A's"), HandleB.IsValid() && HandleB != F.Handle && !F.Director()->HasRequest(F.Handle));
		F.Test->TestEqual(TEXT("Still one zone request"), F.NumZonePriorityRequests(), 1);
		F.Test->TestFalse(TEXT("Valid Look-At target in the request"), F.Info(HandleB).LookAtTarget.IsEmpty());
		if (F.RequestsAtStart == 0)
		{
			F.Test->TestTrue(TEXT("The real camera shows B"), Near(F.PCM()->GetCameraLocation(), F.Origin + FVector(0.0, -600.0, 300.0)));
		}
		// Disable B: A at once (no frame). Enable: B at once. Destroy B: A at once.
		F.B->GetZone()->SetZoneEnabled(false);
		const FACFCameraRequestHandle AfterDisable = F.Zones->GetZoneRequest(F.Player());
		F.Test->TestTrue(TEXT("B disabled: B's request released at once"), !F.Director()->HasRequest(HandleB));
		F.Test->TestTrue(TEXT("B disabled: A requested at once"), AfterDisable.IsValid() && F.Zones->GetSelectedZone(F.Player()) == F.A->GetZone());
		F.B->GetZone()->SetZoneEnabled(true);
		const FACFCameraRequestHandle AfterEnable = F.Zones->GetZoneRequest(F.Player());
		F.Test->TestTrue(TEXT("B enabled: B again at once, A released"), AfterEnable.IsValid() && !F.Director()->HasRequest(AfterDisable) && F.Zones->GetSelectedZone(F.Player()) == F.B->GetZone());
		F.B->Destroy();
		F.Handle = F.Zones->GetZoneRequest(F.Player());
		F.Test->TestTrue(TEXT("B destroyed: its request released at once, A requested"), !F.Director()->HasRequest(AfterEnable) && F.Handle.IsValid() && F.Director()->HasRequest(F.Handle));
		F.Test->TestEqual(TEXT("One zone request after the switches"), F.NumZonePriorityRequests(), 1);
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(3); });
	Steps.Add([](FFlow& F)
	{
		// Released by the game through the public API: requested again after the retry delay (0.25 s here), not at once.
		FACFCameraRequestHandle External = F.Handle;
		F.Test->TestTrue(TEXT("External release"), F.Acf->ReleaseCamera(External));
		F.TimeMark = F.World->GetRealTimeSeconds();
		return true;
	});
	Steps.Add([](FFlow& F)
	{
		const FACFCameraZoneRequestState* State = F.Zones->GetRequestState(F.Player());
		const FACFCameraRequestHandle Now = F.Zones->GetZoneRequest(F.Player());
		if (!Now.IsValid() || !F.Director()->HasRequest(Now))
		{
			return false; // waiting for the retry
		}
		const double Elapsed = F.World->GetRealTimeSeconds() - F.TimeMark;
		F.Test->TestTrue(FString::Printf(TEXT("Requested again after the retry delay (%.3f s >= 0.25 s)"), Elapsed), Elapsed >= 0.24);
		F.Test->TestTrue(TEXT("One loss counted"), State && State->GetNumLost() == 1);
		F.Test->TestTrue(TEXT("A new request"), Now != F.Handle);
		F.Test->TestEqual(TEXT("One zone request"), F.NumZonePriorityRequests(), 1);
		F.Handle = Now;
		// A removed with its streamed level: released at once.
		F.A->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
		F.Test->TestFalse(TEXT("Streamed out: released at once"), F.Director()->HasRequest(F.Handle));
		F.Test->TestFalse(TEXT("Streamed out: no zone request"), F.Zones->GetZoneRequest(F.Player()).IsValid());
		F.Test->TestEqual(TEXT("Streamed out: no zone-priority request"), F.NumZonePriorityRequests(), 0);
		F.A = F.Zone(F.Origin, FVector(300.0, 300.0, 300.0), 0, F.Fixed(F.Origin + FVector(-600.0, 0.0, 300.0)));
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(3); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("New zone: requested"), F.Zones->GetZoneRequest(F.Player()).IsValid());
		// The World-end release: every zone request goes, the selections are cleared.
		F.Zones->ReleaseAllZoneRequests();
		F.Test->TestEqual(TEXT("World end: no zone request"), F.NumZonePriorityRequests(), 0);
		F.Test->TestTrue(TEXT("World end: no selection"), F.Zones->GetSelectedZone(F.Player()) == nullptr);
		F.A->Destroy();
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(3); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestEqual(TEXT("No zone: no zone request on the next frames"), F.NumZonePriorityRequests(), 0);
		F.Test->TestEqual(TEXT("Requests as before the test"), F.Director()->GetNumRequests(), F.RequestsAtStart);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FFlowCommand(Flow, MoveTemp(Steps)));
	return true;
}

// The Control Rotation exploration camera after a zone, and two local players with their own zone requests.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneWorldRequestPlayersTest, "ActionCameraFramework.Zone.World.ControlRotationAndTwoPlayers", ACFZoneWorldRequestTests::Flags)
bool FACFZoneWorldRequestPlayersTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneWorldRequestTests;
	TSharedRef<FFlow> Flow = MakeShared<FFlow>();
	if (!Flow->Start(*this))
	{
		return true;
	}
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this);
	TArray<FStep> Steps;
	Steps.Add([](FFlow& F)
	{
		// The game's exploration camera follows the control rotation (yaw 0, pitch -15).
		F.Controller->SetControlRotation(FRotator(-15.0, 0.0, 0.0));
		FACFCameraRequestParams Params;
		Params.Priority = 0;
		Params.FollowTarget.Actor = F.Pawn();
		Params.DebugName = TEXT("Explore");
		F.Explore = F.Acf->RequestCamera(F.ControlRotationOrbit(), Params);
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(3); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(FString::Printf(TEXT("Exploration camera at the control yaw 0 (%s)"), *F.PCM()->GetCameraRotation().ToString()), FMath::Abs(FRotator::NormalizeAxis(F.PCM()->GetCameraRotation().Yaw)) < 1.0);
		F.A = F.Zone(F.Origin, FVector(300.0, 300.0, 300.0), 0, F.Fixed(F.Origin + FVector(-600.0, 0.0, 300.0))); // view (-10, 45)
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(3); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Zone camera on screen"), F.Director()->GetActiveRequest() == F.Zones->GetZoneRequest(F.Player()));
		const FRotator Control = F.Controller->GetControlRotation().GetNormalized();
		F.Test->TestTrue(FString::Printf(TEXT("Entering the zone does not change the control rotation (%s)"), *Control.ToString()), FMath::Abs(Control.Yaw) < 0.1 && FMath::IsNearlyEqual(Control.Pitch, -15.0, 0.1));
		F.A->SetActorLocation(F.Origin + FVector(5000.0, 0.0, 0.0)); // the player leaves the zone
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(3); });
	Steps.Add([](FFlow& F)
	{
		F.Test->TestTrue(TEXT("Back to the exploration camera"), F.Director()->GetActiveRequest() == F.Explore);
		const FRotator Control = F.Controller->GetControlRotation().GetNormalized();
		F.Test->TestTrue(FString::Printf(TEXT("Leaving: the control rotation takes the zone's view (%s == (-10, 45))"), *Control.ToString()), FMath::IsNearlyEqual(Control.Yaw, 45.0, 0.5) && FMath::IsNearlyEqual(Control.Pitch, -10.0, 0.5));
		const FRotator Camera = F.PCM()->GetCameraRotation().GetNormalized();
		F.Test->TestTrue(FString::Printf(TEXT("The Control Rotation camera keeps the view (%s)"), *Camera.ToString()), FMath::IsNearlyEqual(Camera.Yaw, 45.0, 1.0));
		F.Acf->ReleaseCamera(F.Explore);
		F.A->Destroy();
		// A second local player.
		FString Error;
		F.Second = F.World->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(1), Error, true);
		if (!F.Second.IsValid())
		{
			F.Test->AddError(FString::Printf(TEXT("Could not create a second local player: %s"), *Error));
		}
		return true;
	});
	Steps.Add([](FFlow& F) { return !F.Second.IsValid() || F.Waited(5); });
	Steps.Add([](FFlow& F)
	{
		APawn* Pawn2 = F.Second.IsValid() && F.Second->PlayerController ? F.Second->PlayerController->GetPawn() : nullptr;
		if (!Pawn2)
		{
			F.Test->AddWarning(TEXT("Skipped the two-player part: the game mode spawned no pawn for the second player."));
			return true;
		}
		Pawn2->SetActorLocation(F.Origin + FVector(4000.0, 0.0, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
		F.Second1 = F.Zone(F.Origin, FVector(200.0, 200.0, 300.0), 0, F.Fixed(F.Origin + FVector(-600.0, 0.0, 300.0)));
		F.Second2 = F.Zone(F.Origin + FVector(4000.0, 0.0, 0.0), FVector(200.0, 200.0, 300.0), 0, F.Fixed(F.Origin + FVector(4000.0, -600.0, 300.0)));
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(4); });
	Steps.Add([](FFlow& F)
	{
		if (!F.Second2.IsValid())
		{
			return true;
		}
		ULocalPlayer* P2 = F.Second.Get();
		UACFCameraSubsystem* Acf2 = P2 ? P2->GetSubsystem<UACFCameraSubsystem>() : nullptr;
		if (!F.Test->TestNotNull(TEXT("Second player's camera"), Acf2))
		{
			return true;
		}
		const FACFCameraRequestHandle H1 = F.Zones->GetZoneRequest(F.Player());
		const FACFCameraRequestHandle H2 = F.Zones->GetZoneRequest(P2);
		F.Test->TestTrue(TEXT("Player 1: its zone"), F.Zones->GetSelectedZone(F.Player()) == F.Second1->GetZone());
		F.Test->TestTrue(TEXT("Player 2: its zone"), F.Zones->GetSelectedZone(P2) == F.Second2->GetZone());
		F.Test->TestTrue(TEXT("Player 1's request in player 1's camera only"), F.Director()->HasRequest(H1) && !Acf2->GetDirector()->HasRequest(H1));
		F.Test->TestTrue(TEXT("Player 2's request in player 2's camera only"), Acf2->GetDirector()->HasRequest(H2) && !F.Director()->HasRequest(H2));
		F.Test->TestEqual(TEXT("Player 2: one zone request"), F.NumZonePriorityRequests(Acf2->GetDirector()), 1);
		// Disabling player 2's zone leaves player 1 alone.
		F.Second2->GetZone()->SetZoneEnabled(false);
		F.Test->TestFalse(TEXT("Player 2's zone disabled: its request released"), Acf2->GetDirector()->HasRequest(H2));
		F.Test->TestTrue(TEXT("Player 1 unaffected"), F.Director()->HasRequest(H1));
		F.Second2->GetZone()->SetZoneEnabled(true);
		F.Test->TestTrue(TEXT("Player 2's zone enabled: requested again"), Acf2->GetDirector()->HasRequest(F.Zones->GetZoneRequest(P2)));
		// The second player goes (its pawn with it): its zone state is released on the next frame; player 1 keeps its request.
		ACFTestEnvironment::RemoveLocalPlayer(P2);
		return true;
	});
	Steps.Add([](FFlow& F) { return F.Waited(2); });
	Steps.Add([](FFlow& F)
	{
		if (F.Second.IsValid())
		{
			const FACFCameraZoneRequestState* State = F.Zones->GetRequestState(F.Second.Get());
			F.Test->TestTrue(TEXT("Removed player: no zone request kept"), !State || !State->GetHandle().IsValid());
		}
		F.Second.Reset();
		if (F.Second1.IsValid())
		{
			F.Test->TestTrue(TEXT("Player 1 keeps its zone request"), F.Director()->HasRequest(F.Zones->GetZoneRequest(F.Player())));
		}
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FFlowCommand(Flow, MoveTemp(Steps)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
