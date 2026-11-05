#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraOutputModifier.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSubsystem.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "UObject/Package.h"

#include "UObject/StrongObjectPtr.h"

/**
 * End-to-end checks against the real PlayerCameraManager. ClientContext only: run in a game process:
 *   UnrealEditor-Cmd <project> <map> -game -NullRHI -ExecCmds="Automation RunTests ActionCameraFramework.Integration"
 */
namespace ACFIntegration
{
	static APlayerController* FindLocalPlayerController()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				APlayerController* PC = World->GetFirstPlayerController();
				if (PC && PC->GetLocalPlayer() && PC->PlayerCameraManager)
				{
					return PC;
				}
			}
		}
		return nullptr;
	}

	/** Shared state of one scenario; steps run on successive engine frames. */
	struct FScenario
	{
		FAutomationTestBase* Test = nullptr;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<UACFCameraSubsystem> Subsystem;
		TStrongObjectPtr<UACFCameraPreset> Preset;
		FACFCameraRequestHandle Handle;
		TWeakObjectPtr<AActor> ActorA;
		TWeakObjectPtr<AActor> ActorB;
		TWeakObjectPtr<APlayerCameraManager> OldCameraManager;
		FVector SavedLocation = FVector::ZeroVector;
		FRotator SavedRotation = FRotator::ZeroRotator;
		double StartTime = 0.0;
		/** Set by a step to be called again next frame (polling). */
		bool bRetry = false;

		APlayerCameraManager* PCM() const { return Controller.IsValid() ? Controller->PlayerCameraManager.Get() : nullptr; }
		UWorld* World() const { return Controller.IsValid() ? Controller->GetWorld() : nullptr; }

		void LogCamera(const TCHAR* Label) const
		{
			if (const APlayerCameraManager* M = PCM())
			{
				Test->AddInfo(FString::Printf(TEXT("%s: PCM=%s loc=%s rot=%s fov=%.3f"), Label, *M->GetName(),
					*M->GetCameraLocation().ToString(), *M->GetCameraRotation().ToString(), M->GetFOVAngle()));
			}
		}
	};

	using FStep = TFunction<bool(FScenario&)>; // return false to abort the scenario

	/** Runs Steps one per Update, waiting WaitFrames engine frames between steps. */
	class FScenarioCommand : public IAutomationLatentCommand
	{
	public:
		FScenarioCommand(FAutomationTestBase* InTest, TArray<FStep> InSteps, uint64 InWaitFrames = 3)
			: Steps(MoveTemp(InSteps)), WaitFrames(InWaitFrames)
		{
			State->Test = InTest;
		}

		virtual bool Update() override
		{
			if (!bHavePlayer)
			{
				APlayerController* PC = FindLocalPlayerController();
				if (!PC)
				{
					if (GetCurrentRunTime() > 30.0)
					{
						State->Test->AddError(TEXT("No local PlayerController with a PlayerCameraManager within 30s."));
						return true;
					}
					return false;
				}
				State->Controller = PC;
				State->Subsystem = PC->GetLocalPlayer()->GetSubsystem<UACFCameraSubsystem>();
				if (!State->Subsystem.IsValid())
				{
					State->Test->AddError(TEXT("UACFCameraSubsystem missing on local player."));
					return true;
				}
				bHavePlayer = true;
			}
			if (GFrameCounter < ResumeFrame)
			{
				return false;
			}
			if (!State->PCM() || !State->Subsystem.IsValid())
			{
				State->Test->AddError(TEXT("Player, camera manager or subsystem lost during the scenario."));
				return true;
			}
			State->bRetry = false;
			if (!Steps[Index](*State))
			{
				Cleanup();
				return true;
			}
			if (State->bRetry)
			{
				if (GetCurrentRunTime() > 60.0)
				{
					State->Test->AddError(TEXT("Scenario step timed out."));
					Cleanup();
					return true;
				}
				return false; // same step again next frame
			}
			ResumeFrame = GFrameCounter + WaitFrames;
			if (++Index >= Steps.Num())
			{
				Cleanup();
				return true;
			}
			return false;
		}

	private:
		void Cleanup()
		{
			if (State->Subsystem.IsValid())
			{
				State->Subsystem->ReleaseAllCameras(true);
			}
			for (TWeakObjectPtr<AActor> Actor : { State->ActorA, State->ActorB })
			{
				if (Actor.IsValid())
				{
					Actor->Destroy();
				}
			}
		}

		TSharedRef<FScenario> State = MakeShared<FScenario>();
		TArray<FStep> Steps;
		uint64 WaitFrames;
		uint64 ResumeFrame = 0;
		int32 Index = 0;
		bool bHavePlayer = false;
	};

	static AActor* SpawnMovablePoint(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
		if (!Actor)
		{
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}

	static UACFCameraPreset* MakeFixedPreset(const FVector& Location, const FRotator& Rotation, float FOV)
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_FixedPose* Node = NewObject<UACFCameraNode_FixedPose>(Preset);
		Node->Location = Location;
		Node->Rotation = Rotation;
		Node->FieldOfView = FOV;
		Preset->Nodes.Add(Node);
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		return Preset;
	}

	static bool CameraIs(FScenario& S, const FVector& Location, const FRotator& Rotation, float FOV, const TCHAR* What)
	{
		const APlayerCameraManager* M = S.PCM();
		const bool bLoc = M->GetCameraLocation().Equals(Location, 0.1);
		const bool bRot = M->GetCameraRotation().Equals(Rotation, 0.05f);
		const bool bFov = FOV <= 0.f || FMath::IsNearlyEqual(M->GetFOVAngle(), FOV, 0.01f);
		S.LogCamera(What);
		S.Test->TestTrue(FString::Printf(TEXT("%s: location"), What), bLoc);
		S.Test->TestTrue(FString::Printf(TEXT("%s: rotation"), What), bRot);
		S.Test->TestTrue(FString::Printf(TEXT("%s: fov"), What), bFov);
		return bLoc && bRot && bFov;
	}

	static void CheckModifierAttached(FScenario& S, const TCHAR* What)
	{
		APlayerCameraManager* M = S.PCM();
		const UCameraModifier* Acf = M->FindCameraModifierByClass(UACFCameraOutputModifier::StaticClass());
		const UCameraModifier* Shake = M->FindCameraModifierByClass(UCameraModifier_CameraShake::StaticClass());
		S.Test->TestNotNull(FString::Printf(TEXT("%s: ACF output modifier attached"), What), Acf);
		if (Acf && Shake)
		{
			S.Test->TestTrue(FString::Printf(TEXT("%s: ACF runs before camera shakes"), What), Acf->Priority < Shake->Priority);
		}
	}

	static const FVector FixedLocation(1234.f, -567.f, 890.f);
	static const FRotator FixedRotation(-20.f, 135.f, 0.f);
	static constexpr float FixedFOV = 47.5f;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationFixedOutputTest, "ActionCameraFramework.Integration.FixedCameraOutput",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationFixedOutputTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.LogCamera(TEXT("Baseline UE camera"));
		S.SavedLocation = S.PCM()->GetCameraLocation();
		S.Preset.Reset(MakeFixedPreset(FixedLocation, FixedRotation, FixedFOV));
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), FACFCameraRequestParams());
		return S.Test->TestTrue(TEXT("Request accepted"), S.Handle.IsValid());
	});
	Steps.Add([](FScenario& S)
	{
		CheckModifierAttached(S, TEXT("Fixed"));
		CameraIs(S, FixedLocation, FixedRotation, FixedFOV, TEXT("With ACF fixed camera"));
		S.Test->TestTrue(TEXT("Request active"), S.Subsystem->IsActiveCamera(S.Handle));
		S.Test->TestTrue(TEXT("Release succeeds"), S.Subsystem->ReleaseCamera(S.Handle));
		S.Test->TestFalse(TEXT("Handle invalidated by release"), S.Handle.IsValid());
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.LogCamera(TEXT("After release"));
		S.Test->TestEqual(TEXT("ACF weight back to 0"), S.Subsystem->GetLastEvaluationResult().Weight, 0.f);
		S.Test->TestFalse(TEXT("UE camera no longer at fixed pose"), S.PCM()->GetCameraLocation().Equals(FixedLocation, 0.1));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationFollowLookAtTest, "ActionCameraFramework.Integration.FollowLookAtActors",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationFollowLookAtTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	static const FVector Offset(0.f, 0.f, 200.f);
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.ActorA = SpawnMovablePoint(S.World(), FVector(0, 0, 300));     // follow target
		S.ActorB = SpawnMovablePoint(S.World(), FVector(800, 300, 100)); // look-at target
		if (!S.Test->TestTrue(TEXT("Spawned targets"), S.ActorA.IsValid() && S.ActorB.IsValid()))
		{
			return false;
		}
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_Follow* Follow = NewObject<UACFCameraNode_Follow>(Preset);
		Follow->Offset = Offset;
		Follow->OffsetSpace = EACFCameraOffsetSpace::World;
		Preset->Nodes.Add(Follow);
		Preset->Nodes.Add(NewObject<UACFCameraNode_LookAt>(Preset));
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		S.Preset.Reset(Preset);

		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		Params.LookAtTarget.Actor = S.ActorB.Get();
		Params.DebugName = TEXT("IntegrationFollowLookAt");
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		return S.Test->TestTrue(TEXT("Request accepted"), S.Handle.IsValid());
	});
	Steps.Add([](FScenario& S)
	{
		const FVector Cam = S.ActorA->GetActorLocation() + Offset;
		CameraIs(S, Cam, (S.ActorB->GetActorLocation() - Cam).Rotation(), 0.f, TEXT("Follow+LookAt initial"));
		S.ActorA->SetActorLocation(FVector(400, -200, 300));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const FVector Cam = S.ActorA->GetActorLocation() + Offset;
		CameraIs(S, Cam, (S.ActorB->GetActorLocation() - Cam).Rotation(), 0.f, TEXT("Follow target moved"));
		S.ActorB->SetActorLocation(FVector(-600, 0, 500));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const FVector Cam = S.ActorA->GetActorLocation() + Offset;
		CameraIs(S, Cam, (S.ActorB->GetActorLocation() - Cam).Rotation(), 0.f, TEXT("LookAt target moved independently"));
		S.SavedLocation = S.PCM()->GetCameraLocation();
		S.SavedRotation = S.PCM()->GetCameraRotation();
		S.ActorB->Destroy();
		S.ActorA->Destroy();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		// Both targets destroyed: no crash, camera holds the last known framing.
		CameraIs(S, S.SavedLocation, S.SavedRotation, 0.f, TEXT("Targets destroyed (HoldLastKnown)"));
		S.Test->TestTrue(TEXT("Request still active after target loss"), S.Subsystem->IsActiveCamera(S.Handle));
		S.Test->TestTrue(TEXT("Release"), S.Subsystem->ReleaseCamera(S.Handle));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationCameraManagerRecreatedTest, "ActionCameraFramework.Integration.CameraManagerRecreated",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationCameraManagerRecreatedTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Preset.Reset(MakeFixedPreset(FixedLocation, FixedRotation, FixedFOV));
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), FACFCameraRequestParams());
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		CameraIs(S, FixedLocation, FixedRotation, FixedFOV, TEXT("Before PCM recreation"));
		// Same PlayerController, new PlayerCameraManager (no PlayerControllerChanged notification).
		APlayerCameraManager* Old = S.PCM();
		S.OldCameraManager = Old;
		S.Controller->SpawnPlayerCameraManager();
		if (S.PCM() != Old)
		{
			Old->Destroy();
		}
		return S.Test->TestTrue(TEXT("A new PCM was spawned"), S.PCM() != S.OldCameraManager.Get());
	});
	Steps.Add([](FScenario& S)
	{
		CheckModifierAttached(S, TEXT("After PCM recreation"));
		CameraIs(S, FixedLocation, FixedRotation, FixedFOV, TEXT("After PCM recreation"));
		S.Test->TestTrue(TEXT("Release"), S.Subsystem->ReleaseCamera(S.Handle));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.LogCamera(TEXT("After release on new PCM"));
		S.Test->TestFalse(TEXT("New PCM returns to the UE camera"), S.PCM()->GetCameraLocation().Equals(FixedLocation, 0.1));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationBlueprintApiTest, "ActionCameraFramework.Integration.BlueprintRequestRelease",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationBlueprintApiTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	// Assets authored in the editor by Scripts/CreateVerificationAssets.py (preset edited there: FOV 55 -> 65).
	// Graph: BeginPlay -> Request Camera For Player -> Delay 1.0 -> Release Camera For Player(returned handle).
	static const TCHAR* BlueprintClassPath = TEXT("/ActionCameraFramework/Verification/BP_ACFBlueprintApiTest.BP_ACFBlueprintApiTest_C");
	static const FVector PresetLocation(111.f, 222.f, 333.f);
	static const FRotator PresetRotation(-15.f, 60.f, 0.f);
	static constexpr float PresetFOV = 65.f;

	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		UClass* BlueprintClass = LoadClass<AActor>(nullptr, BlueprintClassPath);
		if (!S.Test->TestNotNull(TEXT("Verification Blueprint exists (run Scripts/CreateVerificationAssets.py)"), BlueprintClass))
		{
			return false;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		S.ActorA = S.World()->SpawnActor<AActor>(BlueprintClass, FTransform::Identity, Params); // BeginPlay -> Request Camera -> Delay 1s -> Release
		S.StartTime = S.World()->GetTimeSeconds();
		return S.Test->TestTrue(TEXT("Spawned verification Blueprint"), S.ActorA.IsValid());
	});
	Steps.Add([](FScenario& S)
	{
		CameraIs(S, PresetLocation, PresetRotation, PresetFOV, TEXT("Blueprint requested preset"));
		S.Test->TestEqual(TEXT("One request from Blueprint"), S.Subsystem->GetLastEvaluationResult().NumRequests, 1);
		S.Test->AddInfo(FString::Printf(TEXT("Checked %.3fs after spawn (BP releases at 1.0s)"), S.World()->GetTimeSeconds() - S.StartTime));
		return S.Test->TestTrue(TEXT("Checked before the Blueprint Delay elapsed"), S.World()->GetTimeSeconds() - S.StartTime < 1.0);
	});
	Steps.Add([](FScenario& S)
	{
		S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.3; // wait for Delay -> Release Camera For Player
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.LogCamera(TEXT("After Blueprint release"));
		const FACFCameraEvaluationResult Result = S.Subsystem->GetLastEvaluationResult();
		S.Test->TestEqual(TEXT("Blueprint released its request"), Result.NumRequests, 0);
		S.Test->TestEqual(TEXT("Weight back to 0"), Result.Weight, 0.f);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
