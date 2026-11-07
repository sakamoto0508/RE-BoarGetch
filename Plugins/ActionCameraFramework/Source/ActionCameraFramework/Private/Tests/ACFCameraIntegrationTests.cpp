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
#include "Components/BoxComponent.h"
#include "ACFCameraSettings.h"
#include "ACFCameraTestShake.h"
#include "Camera/CameraActor.h"
#include "Effects/ACFCameraBuiltinEffects.h"
#include "UnrealClient.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "Nodes/ACFCameraNode_LockOn.h"
#include "Nodes/ACFCameraNode_GroupFraming.h"
#include "Nodes/ACFCameraFraming.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"

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
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TWeakObjectPtr<USceneComponent> Component;
		TArray<TStrongObjectPtr<UACFCameraEffectPreset>> EffectPresets;
		TStrongObjectPtr<UACFCameraPreset> PresetB;
		FACFCameraRequestHandle HandleB;
		FVector Pivot = FVector::ZeroVector;
		bool bFlag = true;
		double Value = 0.0;
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
			State->Spawned.Add(State->ActorA);
			State->Spawned.Add(State->ActorB);
			for (TWeakObjectPtr<AActor> Actor : State->Spawned)
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


// =================================================================================================
// Phase 2: third person camera against real collision and the real SpringArm.

namespace ACFIntegration
{
	/** Far from the lobby geometry so only the test's own blockers matter. */
	static const FVector TestArea(20000.f, 20000.f, 6000.f);

	static AActor* SpawnBlocker(FScenario& S, const FVector& Center, const FVector& Extent)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = S.World()->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Center), Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Blocker"));
		Box->SetBoxExtent(Extent);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		Actor->SetActorLocation(Center);
		S.Spawned.Add(Actor);
		return Actor;
	}

	/** Orbit (fixed rotation, no input) + Collision, following ActorA. Camera desired at pivot - X * Distance. */
	static UACFCameraPreset* MakeOrbitCollisionPreset(float Distance, float ReturnDamping, const FACFCameraBlendSettings& BlendIn)
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_OrbitalFollow* Orbit = NewObject<UACFCameraNode_OrbitalFollow>(Preset);
		Orbit->InputSource = EACFCameraOrbitInputSource::None;
		Orbit->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
		Orbit->InitialYaw = 0.f;
		Orbit->InitialPitch = 0.f;
		Orbit->PivotOffset = FVector::ZeroVector;
		Orbit->Distance = Distance;
		UACFCameraNode_Collision* Collision = NewObject<UACFCameraNode_Collision>(Preset);
		Collision->ReturnDampingTime = ReturnDamping;
		Preset->Nodes = { Collision, Orbit }; // deliberately listed first: the PostBlend stage still runs last
		Preset->DefaultBlendIn = BlendIn;
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		return Preset;
	}

	static bool CameraCloseTo(FScenario& S, const FVector& Expected, double Tolerance, const TCHAR* What)
	{
		S.LogCamera(What);
		const bool bOk = S.PCM()->GetCameraLocation().Equals(Expected, Tolerance);
		S.Test->TestTrue(FString::Printf(TEXT("%s (expected %s)"), What, *Expected.ToString()), bOk);
		return bOk;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationSpringArmTest, "ActionCameraFramework.Integration.OrbitMatchesSpringArm",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationSpringArmTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		APawn* Pawn = S.Controller->GetPawn();
		USpringArmComponent* Arm = Pawn ? Pawn->FindComponentByClass<USpringArmComponent>() : nullptr;
		if (!S.Test->TestNotNull(TEXT("Player pawn with a SpringArm"), Arm))
		{
			return false;
		}
		S.Component = Arm;
		S.Test->AddInfo(FString::Printf(TEXT("SpringArm %s: length %.1f socket %s target %s pawnRot %d lag %d(%.1f) rotLag %d collision %d probe %.1f"),
			*Arm->GetPathName(), Arm->TargetArmLength, *Arm->SocketOffset.ToString(), *Arm->TargetOffset.ToString(), Arm->bUsePawnControlRotation ? 1 : 0,
			Arm->bEnableCameraLag ? 1 : 0, Arm->CameraLagSpeed, Arm->bEnableCameraRotationLag ? 1 : 0, Arm->bDoCollisionTest ? 1 : 0, Arm->ProbeSize));

		// The same camera expressed as an ACF preset (Orbital Follow + Collision), following the arm component.
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_OrbitalFollow* Orbit = NewObject<UACFCameraNode_OrbitalFollow>(Preset);
		Orbit->InputSource = EACFCameraOrbitInputSource::ControlRotation;
		Orbit->InitialRotation = EACFCameraOrbitInitialRotation::ControlRotation;
		Orbit->PivotOffset = Arm->TargetOffset;
		Orbit->PivotOffsetSpace = EACFCameraOffsetSpace::World;
		Orbit->Distance = Arm->TargetArmLength;
		Orbit->ShoulderOffset = Arm->SocketOffset;
		Orbit->PitchMin = -89.f;
		Orbit->PitchMax = 89.f;
		Orbit->TargetMotion.TeleportDistance = 0.f;
		Preset->Nodes.Add(Orbit);
		if (Arm->bDoCollisionTest)
		{
			UACFCameraNode_Collision* Collision = NewObject<UACFCameraNode_Collision>(Preset);
			Collision->ProbeRadius = Arm->ProbeSize;
			Collision->ProbeChannel = Arm->ProbeChannel;
			Collision->MinDistance = 0.f;
			Collision->ReturnDampingTime = 0.f;
			Preset->Nodes.Add(Collision);
		}
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		S.Preset.Reset(Preset);

		S.Controller->SetControlRotation(FRotator(-20.f, 30.f, 0.f));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Component = Arm;
		Params.DebugName = TEXT("SpringArmEquivalent");
		S.Handle = S.Subsystem->RequestCamera(Preset, Params);
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	auto Compare = [](FScenario& S, const TCHAR* What)
	{
		// Compare with the SpringArm socket itself (what a camera attached to it would see). The level's view
		// target camera is not necessarily attached to this arm, so the PCM upstream pose is only logged.
		const FACFCameraEvaluationResult R = S.Subsystem->GetLastEvaluationResult();
		const FTransform Socket = S.Component->GetSocketTransform(USpringArmComponent::SocketName);
		const double LocError = FVector::Dist(R.Pose.Location, Socket.GetLocation());
		const double RotError = FMath::RadiansToDegrees(FQuat(R.Pose.Rotation).AngularDistance(Socket.GetRotation()));
		S.Test->AddInfo(FString::Printf(TEXT("%s: SpringArm socket %s %s (PCM upstream %s)"), What, *Socket.GetLocation().ToString(), *Socket.Rotator().ToString(), *R.UpstreamPose.Location.ToString()));
		S.Test->AddInfo(FString::Printf(TEXT("%s: ACF %s %s  SpringArm %s %s  error %.3f cm %.3f deg"), What,
			*R.Pose.Location.ToString(), *R.Pose.Rotation.ToString(), *R.UpstreamPose.Location.ToString(), *R.UpstreamPose.Rotation.ToString(), LocError, RotError));
		S.Test->TestTrue(FString::Printf(TEXT("%s: position matches the SpringArm (< 1 cm)"), What), LocError < 1.0);
		S.Test->TestTrue(FString::Printf(TEXT("%s: rotation matches the SpringArm (< 0.1 deg)"), What), RotError < 0.1);
	};
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.5; return true; }); // SpringArm lag settles
	Steps.Add([Compare](FScenario& S)
	{
		Compare(S, TEXT("Control rotation (-20, 30)"));
		S.Controller->SetControlRotation(FRotator(-45.f, 160.f, 0.f));
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.5; return true; });
	Steps.Add([Compare](FScenario& S)
	{
		Compare(S, TEXT("Control rotation (-45, 160)"));
		// Obstacle on the arm: both the SpringArm probe and ACF Collision must pull in to the same point.
		const FVector Pivot = S.Component->GetComponentLocation() + CastChecked<USpringArmComponent>(S.Component.Get())->TargetOffset;
		const FVector Socket = S.Component->GetSocketLocation(USpringArmComponent::SocketName);
		S.ActorB = SpawnBlocker(S, Pivot + (Socket - Pivot) * 0.6, FVector(30, 30, 30));
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.0; return true; });
	Steps.Add([Compare](FScenario& S)
	{
		Compare(S, TEXT("Obstacle on the arm (both collide)"));
		const FVector Pivot = S.Component->GetComponentLocation();
		S.Test->TestTrue(TEXT("The obstacle actually shortened the arm"), FVector::Dist(Pivot, S.Component->GetSocketLocation(USpringArmComponent::SocketName)) < CastChecked<USpringArmComponent>(S.Component.Get())->TargetArmLength * 0.7);
		return S.Test->TestTrue(TEXT("Release"), S.Subsystem->ReleaseCamera(S.Handle));
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationCollisionTest, "ActionCameraFramework.Integration.CollisionWallAndReturn",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationCollisionTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	static constexpr float Distance = 600.f;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Pivot = TestArea;
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		S.Preset.Reset(MakeOrbitCollisionPreset(Distance, 0.3f, FACFCameraBlendSettings::MakeCut()));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, S.Pivot - FVector(Distance, 0, 0), 0.1, TEXT("No obstacle: full distance"));
		S.ActorB = SpawnBlocker(S, S.Pivot - FVector(300, 0, 0), FVector(10, 300, 300)); // wall face at pivot.X - 290
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const double CameraX = S.PCM()->GetCameraLocation().X;
		const double FaceX = S.Pivot.X - 290.0;
		S.LogCamera(TEXT("Wall between pivot and camera"));
		S.Test->TestTrue(TEXT("Camera stays in front of the wall (probe radius clearance)"), CameraX >= FaceX + 12.0 - 0.5);
		S.Test->TestTrue(TEXT("Camera pulled in close to the wall"), CameraX <= FaceX + 12.0 + 2.0);
		S.ActorB->Destroy();
		S.Value = S.Pivot.X - CameraX; // distance while blocked
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const double Dist = S.Pivot.X - S.PCM()->GetCameraLocation().X;
		S.Test->AddInfo(FString::Printf(TEXT("Return after %.3fs: distance %.1f (blocked %.1f, full %.1f)"), S.World()->GetTimeSeconds() - S.StartTime, Dist, S.Value, Distance));
		S.Test->TestTrue(TEXT("Return is smoothed (not an instant pop to full distance)"), Dist > S.Value + 1.0 && Dist < Distance - 5.0);
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 2.5; return true; });
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, S.Pivot - FVector(Distance, 0, 0), 0.5, TEXT("Returned to full distance"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationBlendCollisionTest, "ActionCameraFramework.Integration.CollisionDuringBlend",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationBlendCollisionTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Pivot = TestArea + FVector(0, 3000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		SpawnBlocker(S, S.Pivot - FVector(300, 0, 0), FVector(10, 600, 600));
		// A: fixed camera behind the wall (no collision of its own). B: orbit + collision, 1s live blend.
		S.Preset.Reset(MakeFixedPreset(S.Pivot + FVector(-900, 200, 150), FRotator(-5, 0, 0), 80.f));
		FACFCameraBlendSettings Blend;
		Blend.Curve = EACFCameraBlendCurve::Linear;
		Blend.Duration = 1.f;
		S.PresetB.Reset(MakeOrbitCollisionPreset(600.f, 0.3f, Blend));
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), FACFCameraRequestParams());
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		S.HandleB = S.Subsystem->RequestCamera(S.PresetB.Get(), Params);
		S.StartTime = S.World()->GetTimeSeconds();
		S.Value = 0.0;
		S.bFlag = true;
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		// Every frame of the blend: the final camera must be in front of the wall.
		const FACFCameraEvaluationResult R = S.Subsystem->GetLastEvaluationResult();
		const double CameraX = S.PCM()->GetCameraLocation().X;
		if (R.bIsBlending)
		{
			S.Value += 1.0;
			S.bFlag &= CameraX >= S.Pivot.X - 290.0 + 12.0 - 0.5;
		}
		S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.2;
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->AddInfo(FString::Printf(TEXT("Checked %.0f blend frames"), S.Value));
		S.Test->TestTrue(TEXT("Blend frames were observed"), S.Value >= 5.0);
		S.Test->TestTrue(TEXT("Camera never behind the wall during the blend"), S.bFlag);
		S.LogCamera(TEXT("After blend"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationCollisionEdgeCasesTest, "ActionCameraFramework.Integration.CollisionIgnoreTargetAndStartInside",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationCollisionEdgeCasesTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		// Follow target that itself has blocking collision (like a character capsule).
		S.Pivot = TestArea + FVector(0, -3000, 0);
		S.ActorA = SpawnBlocker(S, S.Pivot, FVector(50, 50, 90));
		S.Preset.Reset(MakeOrbitCollisionPreset(600.f, 0.f, FACFCameraBlendSettings::MakeCut()));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, S.Pivot - FVector(600, 0, 0), 0.5, TEXT("Follow target collision is ignored"));
		// Pivot inside some other geometry (character pressed into a wall): no collapse onto the pivot.
		SpawnBlocker(S, S.Pivot, FVector(150, 150, 150));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, S.Pivot - FVector(600, 0, 0), 0.5, TEXT("Pivot starts inside geometry: camera not collapsed"));
		// Desired camera position inside a thick wall: placed in front of the wall.
		SpawnBlocker(S, S.Pivot - FVector(600, 0, 0), FVector(100, 400, 400)); // faces at X-500 / X-700
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const double CameraX = S.PCM()->GetCameraLocation().X;
		S.LogCamera(TEXT("Desired camera inside a wall"));
		S.Test->TestTrue(TEXT("Camera placed in front of the wall that contained it"), CameraX >= S.Pivot.X - 500.0 + 12.0 - 0.5 && CameraX <= S.Pivot.X - 500.0 + 14.0);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationBlueprintOrbitTest, "ActionCameraFramework.Integration.BlueprintOrbitInput",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationBlueprintOrbitTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	// Scripts/CreateVerificationAssets.py: BeginPlay -> Make Target Request Params(Follow = player pawn)
	// -> Request Camera For Player(DA_ACFVerify_Orbit) -> Add Camera Input (90, 0) -> Delay 1 -> Release.
	static const TCHAR* ClassPath = TEXT("/ActionCameraFramework/Verification/BP_ACFOrbitInputTest.BP_ACFOrbitInputTest_C");
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		UClass* BlueprintClass = LoadClass<AActor>(nullptr, ClassPath);
		if (!S.Test->TestNotNull(TEXT("Verification Blueprint exists (run Scripts/CreateVerificationAssets.py)"), BlueprintClass)
			|| !S.Test->TestTrue(TEXT("Player pawn"), S.Controller->GetPawn() != nullptr))
		{
			return false;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		S.ActorA = S.World()->SpawnActor<AActor>(BlueprintClass, FTransform::Identity, Params);
		S.StartTime = S.World()->GetTimeSeconds();
		return S.Test->TestTrue(TEXT("Spawned"), S.ActorA.IsValid());
	});
	Steps.Add([](FScenario& S)
	{
		const float PawnYaw = S.Controller->GetPawn()->GetActorRotation().Yaw;
		const FRotator Camera = S.PCM()->GetCameraRotation();
		S.Test->AddInfo(FString::Printf(TEXT("Pawn yaw %.2f, camera %s, %.3fs after spawn"), PawnYaw, *Camera.ToString(), S.World()->GetTimeSeconds() - S.StartTime));
		S.Test->TestEqual(TEXT("Blueprint request active"), S.Subsystem->GetLastEvaluationResult().NumRequests, 1);
		S.Test->TestTrue(TEXT("Blueprint camera input rotated the orbit by 90 deg from behind the pawn"),
			FMath::IsNearlyEqual(FRotator::NormalizeAxis(Camera.Yaw - (PawnYaw + 90.f)), 0.f, 0.5f));
		S.Test->TestTrue(TEXT("Initial pitch from the preset"), FMath::IsNearlyEqual(Camera.Pitch, -15.f, 0.5f));
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.3; return true; });
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Blueprint released its request"), S.Subsystem->GetLastEvaluationResult().NumRequests, 0);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}


// =================================================================================================
// Phase 2 audit: final camera position after the Weight blend with the UE camera and after camera shakes.
// Each test first REPRODUCES the problem with the output collision guard disabled, then checks the fix.

namespace ACFIntegration
{
	static void SetGuardEnabled(bool bEnabled)
	{
		GetMutableDefault<UACFCameraSettings>()->bEnableOutputCollisionGuard = bEnabled;
	}

	static bool IsInsideBox(const FVector& Point, const FVector& Center, const FVector& Extent)
	{
		return FBox(Center - Extent, Center + Extent).IsInside(Point);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationWeightBlendPillarTest, "ActionCameraFramework.Integration.WeightBlendDoesNotCrossGeometry",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationWeightBlendPillarTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	static const FVector PillarExtent(40.f, 40.f, 400.f);
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		// UE camera (a camera actor view target) left of a pillar, ACF camera right of it. Both see the
		// pivot, but the straight interpolation between them passes through the pillar.
		S.Pivot = TestArea + FVector(0, 6000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		SpawnBlocker(S, S.Pivot + FVector(-600, 0, 0), PillarExtent);
		const FVector UeCamera = S.Pivot + FVector(-600, -300, 0);
		ACameraActor* ViewCamera = S.World()->SpawnActor<ACameraActor>(UeCamera, (S.Pivot - UeCamera).Rotation());
		S.Spawned.Add(ViewCamera);
		S.Controller->SetViewTarget(ViewCamera);

		UACFCameraPreset* Preset = MakeOrbitCollisionPreset(FVector(600, -300, 0).Size(), 0.f, FACFCameraBlendSettings());
		Cast<UACFCameraNode_OrbitalFollow>(Preset->Nodes[1])->InitialYaw = FVector(600, -300, 0).Rotation().Yaw; // camera at pivot + (-600, +300)
		FACFCameraBlendSettings Blend;
		Blend.Curve = EACFCameraBlendCurve::Linear;
		Blend.Duration = 1.5f;
		Preset->DefaultBlendIn = Blend;
		S.Preset.Reset(Preset);
		SetGuardEnabled(false);
		return true;
	});
	auto Request = [](FScenario& S)
	{
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		S.StartTime = S.World()->GetTimeSeconds();
		S.Value = 0.0;
		S.bFlag = false;
		return true;
	};
	auto Watch = [](FScenario& S)
	{
		const FVector Camera = S.PCM()->GetCameraLocation();
		const float Weight = S.Subsystem->GetLastEvaluationResult().Weight;
		if (Weight > 0.f && Weight < 1.f)
		{
			S.Value += 1.0;
			S.bFlag |= IsInsideBox(Camera, S.Pivot + FVector(-600, 0, 0), PillarExtent);
		}
		S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.7;
		return true;
	};
	Steps.Add(Request);
	Steps.Add(Watch);
	Steps.Add([](FScenario& S)
	{
		S.Test->AddInfo(FString::Printf(TEXT("Guard OFF: %.0f weight-blend frames, camera inside pillar: %d"), S.Value, S.bFlag ? 1 : 0));
		S.Test->TestTrue(TEXT("Reproduced: without the guard the Weight blend passes through the pillar"), S.bFlag);
		S.Subsystem->ReleaseAllCameras(true);
		SetGuardEnabled(true);
		return true;
	});
	Steps.Add(Request);
	Steps.Add(Watch);
	Steps.Add([](FScenario& S)
	{
		S.Test->AddInfo(FString::Printf(TEXT("Guard ON: %.0f weight-blend frames, camera inside pillar: %d"), S.Value, S.bFlag ? 1 : 0));
		S.Test->TestTrue(TEXT("Weight blend frames observed"), S.Value >= 5.0);
		S.Test->TestFalse(TEXT("Fixed: with the guard the camera never enters the pillar"), S.bFlag);
		S.Controller->SetViewTarget(S.Controller->GetPawn() ? static_cast<AActor*>(S.Controller->GetPawn()) : S.Controller.Get());
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationShakeCollisionTest, "ActionCameraFramework.Integration.ShakeVersusCollision",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationShakeCollisionTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Pivot = TestArea + FVector(0, -6000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		SpawnBlocker(S, S.Pivot - FVector(300, 0, 0), FVector(10, 300, 300)); // face at pivot.X - 290
		S.Preset.Reset(MakeOrbitCollisionPreset(600.f, 0.f, FACFCameraBlendSettings::MakeCut()));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.LogCamera(TEXT("Against the wall, no shake"));
		S.Test->TestTrue(TEXT("Collision keeps the camera in front of the wall"), S.PCM()->GetCameraLocation().X >= S.Pivot.X - 290.0 + 12.0 - 0.5);
		SetGuardEnabled(false);
		S.PCM()->StartCameraShake(UACFTestCameraShake::StaticClass(), 1.f); // 60 cm backwards, into the wall
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const double CameraX = S.PCM()->GetCameraLocation().X;
		S.LogCamera(TEXT("Shake, guard OFF"));
		S.Test->TestTrue(TEXT("Reproduced: a UE camera shake after ACF pushes the camera through the wall face"), CameraX < S.Pivot.X - 290.0);
		SetGuardEnabled(true);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const double CameraX = S.PCM()->GetCameraLocation().X;
		S.LogCamera(TEXT("Shake, guard ON"));
		S.Test->TestTrue(TEXT("Fixed: the guard keeps the shaken camera in front of the wall"), CameraX >= S.Pivot.X - 290.0 + 12.0 - 0.5);
		S.Test->TestTrue(TEXT("Guard reports a correction"), S.Subsystem->GetGuardCorrection(GFrameCounter - 1) > 0.f || S.Subsystem->GetGuardCorrection(GFrameCounter) > 0.f);
		S.PCM()->StopAllCameraShakes(true);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Without shake the camera is back at the collision position"), FMath::IsNearlyEqual(S.PCM()->GetCameraLocation().X, S.Pivot.X - 278.0, 1.0));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}


// =================================================================================================
// Manual-verification content smoke test. Run on the sample map:
//   UnrealEditor-Cmd <project> /ActionCameraFramework/Samples/L_ACFCameraTest -game ... "Automation RunTests ActionCameraFramework.Samples"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSampleMapSmokeTest, "ActionCameraFramework.Samples.TestMapSmoke",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFSampleMapSmokeTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	static const TCHAR* Samples[] = { TEXT("Follow"), TEXT("ThirdPerson"), TEXT("FreeLook"), TEXT("Composer") };
	static const FVector CorridorCenter(0.f, 1500.f, 0.f);
	// Inner wall faces at Y = 1500 +- 135, ceiling bottom at Z = 250 (Scripts/CreateManualTestAssets.py); sample probe radius 15.
	static constexpr double CorridorHalfWidth = 135.0;
	static constexpr double CeilingBottom = 250.0;
	static constexpr double Probe = 15.0;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		if (!S.World()->GetMapName().Contains(TEXT("L_ACFCameraTest")))
		{
			S.Test->AddWarning(FString::Printf(TEXT("Skipped: run on /ActionCameraFramework/Samples/L_ACFCameraTest (current %s)."), *S.World()->GetMapName()));
			return false;
		}
		return S.Test->TestTrue(TEXT("Pawn spawned by the project's default game mode"), S.Controller->GetPawn() != nullptr);
	});
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Samples); ++Index)
	{
		Steps.Add([Index](FScenario& S)
		{
			GEngine->Exec(S.World(), *FString::Printf(TEXT("ACF.Sample.Use %s 0"), Samples[Index]));
			S.StartTime = S.World()->GetTimeSeconds();
			return true;
		});
		Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 0.5; return true; });
		Steps.Add([Index](FScenario& S)
		{
			const FACFCameraEvaluationResult R = S.Subsystem->GetLastEvaluationResult();
			const FVector Camera = S.PCM()->GetCameraLocation();
			const double ToPawn = FVector::Dist(Camera, S.Controller->GetPawn()->GetActorLocation());
			S.Test->AddInfo(FString::Printf(TEXT("Sample %s: active %s weight %.2f camera %s (%.0f cm from pawn)"), Samples[Index], *R.ActiveRequest.ToString(), R.Weight, *Camera.ToString(), ToPawn));
			S.Test->TestTrue(FString::Printf(TEXT("%s active with full weight"), Samples[Index]), R.ActiveRequest.IsValid() && FMath::IsNearlyEqual(R.Weight, 1.f));
			S.Test->TestTrue(FString::Printf(TEXT("%s camera near the pawn"), Samples[Index]), ToPawn > 50.0 && ToPawn < 1500.0);
			return true;
		});
	}
	Steps.Add([](FScenario& S)
	{
		// Real level geometry: put the pawn in the corridor under the low ceiling and look along it from above.
		S.Controller->GetPawn()->TeleportTo(CorridorCenter + FVector(500.f, 0.f, 100.f), FRotator(0.f, 0.f, 0.f));
		S.Controller->SetControlRotation(FRotator(-35.f, 90.f, 0.f)); // camera wants to go sideways into a corridor wall
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Use ThirdPerson 0"));
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 0.8; return true; });
	auto InsideCorridor = [](FScenario& S, const TCHAR* What)
	{
		const FVector Camera = S.PCM()->GetCameraLocation();
		S.LogCamera(What);
		S.Test->TestTrue(FString::Printf(TEXT("%s: camera between the corridor walls"), What), FMath::Abs(Camera.Y - CorridorCenter.Y) <= CorridorHalfWidth - Probe + 0.5);
		S.Test->TestTrue(FString::Printf(TEXT("%s: camera below the corridor ceiling"), What), Camera.Z <= CeilingBottom - Probe + 0.5);
	};
	Steps.Add([InsideCorridor](FScenario& S)
	{
		InsideCorridor(S, TEXT("Corridor"));
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Shake 1"));
		S.StartTime = S.World()->GetTimeSeconds();
		S.bFlag = true;
		S.Value = -1000.0;
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const FVector Camera = S.PCM()->GetCameraLocation();
		S.bFlag &= FMath::Abs(Camera.Y - CorridorCenter.Y) <= CorridorHalfWidth - Probe + 0.5 && Camera.Z <= CeilingBottom - Probe + 0.5;
		S.Value = FMath::Max(S.Value, FMath::Max(FMath::Abs(Camera.Y - CorridorCenter.Y) - (CorridorHalfWidth - Probe), Camera.Z - (CeilingBottom - Probe)));
		S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.5;
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->AddInfo(FString::Printf(TEXT("Shake in corridor: worst overshoot past the probe clearance %.2f cm (negative = always clear)"), S.Value));
		S.Test->TestTrue(TEXT("Debug shake never pushes the camera out of the corridor (output guard)"), S.bFlag);
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Release 0"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 2));
	return true;
}


// =================================================================================================
// Phase 3: camera effects in the real pipeline.

namespace ACFIntegration
{
	static FACFCameraEffectEnvelope HoldEnvelope(float Sustain)
	{
		FACFCameraEffectEnvelope Envelope;
		Envelope.Attack = 0.05f;
		Envelope.Sustain = Sustain;
		Envelope.Release = 0.2f;
		return Envelope;
	}

	static UACFCameraEffectPreset* MakeEffectPreset(TFunctionRef<void(UACFCameraEffectPreset*)> Build)
	{
		UACFCameraEffectPreset* Preset = NewObject<UACFCameraEffectPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		Build(Preset);
		return Preset;
	}

	static UACFCameraEffect_Punch* AddPunch(UACFCameraEffectPreset* Preset, const FVector& Location, float Sustain)
	{
		UACFCameraEffect_Punch* Punch = NewObject<UACFCameraEffect_Punch>(Preset);
		Punch->Location = Location;
		Punch->Envelope = HoldEnvelope(Sustain);
		Preset->Layers.Add(Punch);
		return Punch;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationEffectsCollisionTest, "ActionCameraFramework.Integration.EffectsRespectCollision",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationEffectsCollisionTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Pivot = TestArea + FVector(0, 9000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		S.Preset.Reset(MakeOrbitCollisionPreset(600.f, 0.f, FACFCameraBlendSettings::MakeCut()));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		S.EffectPresets.Reset();
		S.EffectPresets.Emplace(MakeEffectPreset([](UACFCameraEffectPreset* P) { AddPunch(P, FVector(-150, 0, 0), 1.5f); }));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		// Free space: the punch really moves the camera backwards.
		S.Subsystem->PlayCameraEffect(S.EffectPresets[0].Get(), FACFCameraEffectParams());
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 0.2; return true; });
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, S.Pivot - FVector(750, 0, 0), 1.0, TEXT("Punch in free space moves the camera 150 cm back"));
		S.Subsystem->StopAllCameraEffects(true);
		SpawnBlocker(S, S.Pivot - FVector(300, 0, 0), FVector(10, 400, 400)); // face at pivot.X - 290
		SetGuardEnabled(false); // prove ACF's own Collision handles effects (not the output guard)
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		// Punch + recoil x10 + impulse pushing backwards, all at once.
		S.EffectPresets.Emplace(MakeEffectPreset([](UACFCameraEffectPreset* P)
		{
			UACFCameraEffect_Recoil* Recoil = NewObject<UACFCameraEffect_Recoil>(P);
			Recoil->KickBack = 120.f;
			Recoil->Envelope = HoldEnvelope(1.f);
			P->Layers.Add(Recoil);
			UACFCameraEffect_Impulse* Impulse = NewObject<UACFCameraEffect_Impulse>(P);
			Impulse->Amplitude = 200.f;
			Impulse->Frequency = 3.f;
			Impulse->Envelope = HoldEnvelope(1.f);
			P->Layers.Add(Impulse);
		}));
		S.Subsystem->PlayCameraEffect(S.EffectPresets[0].Get(), FACFCameraEffectParams());
		FACFCameraEffectParams ImpulseParams;
		ImpulseParams.bUseSourceLocation = true;
		ImpulseParams.SourceLocation = S.PCM()->GetCameraLocation() + FVector(150, 0, 0); // in front -> pushes back
		S.Subsystem->PlayCameraEffect(S.EffectPresets[1].Get(), ImpulseParams);
		S.StartTime = S.World()->GetTimeSeconds();
		S.bFlag = true;
		S.Value = -1000.0;
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const double CameraX = S.PCM()->GetCameraLocation().X;
		const double Clearance = S.Pivot.X - 290.0 + 12.0;
		S.bFlag &= CameraX >= Clearance - 0.5;
		S.Value = FMath::Max(S.Value, Clearance - CameraX);
		S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.2;
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->AddInfo(FString::Printf(TEXT("Effects against a wall (guard OFF): worst intrusion past the probe clearance %.2f cm (<= 0 is clear)"), S.Value));
		S.Test->TestTrue(TEXT("Punch + Recoil + Impulse together never push the camera through the wall"), S.bFlag);
		SetGuardEnabled(true);
		S.Subsystem->StopAllCameraEffects(true);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationGuardIdleTest, "ActionCameraFramework.Integration.GuardLeavesUECameraAlone",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationGuardIdleTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	static const FVector UeCameraOffset(-600.f, 0.f, 0.f);
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		// A UE camera that the ACF collision constraint would definitely move: inside a box, behind a wall.
		S.Pivot = TestArea + FVector(0, -9000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		SpawnBlocker(S, S.Pivot - FVector(300, 0, 0), FVector(10, 400, 400));
		SpawnBlocker(S, S.Pivot + UeCameraOffset, FVector(50, 50, 50));
		ACameraActor* ViewCamera = S.World()->SpawnActor<ACameraActor>(S.Pivot + UeCameraOffset, FRotator::ZeroRotator);
		S.Spawned.Add(ViewCamera);
		S.ActorB = ViewCamera;
		S.Controller->SetViewTarget(ViewCamera);
		S.Preset.Reset(MakeOrbitCollisionPreset(600.f, 0.f, FACFCameraBlendSettings::MakeCut()));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("ACF active: collided camera in front of the wall"), S.PCM()->GetCameraLocation().X > S.Pivot.X - 290.0);
		S.Subsystem->ReleaseAllCameras(true);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, S.Pivot + UeCameraOffset, 0.01, TEXT("ACF released: the guard leaves the UE camera exactly where it is"));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		S.Subsystem->SuppressOutput(S.ActorA.Get(), 0.f);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, S.Pivot + UeCameraOffset, 0.01, TEXT("Output suppressed: the guard leaves the UE camera exactly where it is"));
		S.Subsystem->ReleaseOutputSuppression(S.ActorA.Get(), 0.f);
		S.Subsystem->ReleaseAllCameras(true);
		S.Controller->SetViewTarget(S.Controller->GetPawn() ? static_cast<AActor*>(S.Controller->GetPawn()) : S.Controller.Get());
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationAcfShakeTest, "ActionCameraFramework.Integration.AcfShakeAppliedOnceAndCollided",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationAcfShakeTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Pivot = TestArea + FVector(3000, 9000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		S.Preset.Reset(MakeOrbitCollisionPreset(600.f, 0.f, FACFCameraBlendSettings::MakeCut()));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Value = static_cast<double>(S.Subsystem->PlayCameraShake(UACFTestCameraShake::StaticClass(), 1.f).GetId());
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, S.Pivot - FVector(660, 0, 0), 0.01, TEXT("ACF shake: 60 cm back exactly once (not 120)"));
		TArray<FActiveCameraShakeInfo> UeShakes;
		if (const UCameraModifier_CameraShake* ShakeModifier = Cast<UCameraModifier_CameraShake>(S.PCM()->FindCameraModifierByClass(UCameraModifier_CameraShake::StaticClass())))
		{
			ShakeModifier->GetActiveCameraShakes(UeShakes);
		}
		S.Test->TestEqual(TEXT("The PlayerCameraManager does not run the ACF shake"), UeShakes.Num(), 0);
		SpawnBlocker(S, S.Pivot - FVector(300, 0, 0), FVector(10, 400, 400));
		SetGuardEnabled(false);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const double CameraX = S.PCM()->GetCameraLocation().X;
		S.LogCamera(TEXT("ACF shake against the wall (guard OFF)"));
		S.Test->TestTrue(TEXT("ACF shake location is collided (stays in front of the wall without the guard)"), CameraX >= S.Pivot.X - 290.0 + 12.0 - 0.5);
		SetGuardEnabled(true);
		S.Subsystem->StopAllCameraEffects(false);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Shake effect ended after stop"), S.Subsystem->GetActiveCameraEffects().Num(), 0);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationSpeedFovRollTest, "ActionCameraFramework.Integration.SpeedFovAndRoll",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationSpeedFovRollTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Pivot = TestArea + FVector(-3000, 9000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		S.Preset.Reset(MakeFixedPreset(S.Pivot - FVector(800, 0, 0), FRotator::ZeroRotator, 90.f));
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), FACFCameraRequestParams());
		S.EffectPresets.Reset();
		S.EffectPresets.Emplace(MakeEffectPreset([](UACFCameraEffectPreset* P)
		{
			UACFCameraEffect_SpeedFOV* Fov = NewObject<UACFCameraEffect_SpeedFOV>(P);
			Fov->MinSpeed = 100.f;
			Fov->MaxSpeed = 1000.f;
			Fov->FOVAtMaxSpeed = 20.f;
			Fov->MaxFOV = 105.f;
			Fov->RiseTime = 0.1f;
			Fov->FallTime = 0.1f;
			P->Layers.Add(Fov);
			UACFCameraEffect_Roll* Roll = NewObject<UACFCameraEffect_Roll>(P);
			Roll->RollPerYawRate = 0.f;
			Roll->RollPerLateralSpeed = 0.01f;
			Roll->MaxRoll = 5.f;
			Roll->ResponseTime = 0.1f;
			Roll->ReturnTime = 0.1f;
			P->Layers.Add(Roll);
		}));
		FACFCameraEffectParams Params;
		Params.TargetActor = S.ActorA.Get();
		S.Subsystem->PlayCameraEffect(S.EffectPresets[0].Get(), Params);
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		// Target runs sideways (camera right) at 1000 cm/s for 1.2 s.
		const double T = S.World()->GetTimeSeconds() - S.StartTime;
		S.ActorA->SetActorLocation(S.Pivot + FVector(0, 1000.0 * T, 0));
		S.bRetry = T < 1.2;
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const FMinimalViewInfo& View = S.PCM()->GetCameraCacheView();
		S.Test->AddInfo(FString::Printf(TEXT("Running: FOV %.2f roll %.2f"), View.FOV, View.Rotation.Roll));
		S.Test->TestTrue(TEXT("Speed FOV widened up to its MaxFOV (105)"), FMath::IsNearlyEqual(View.FOV, 105.f, 0.5f));
		S.Test->TestTrue(TEXT("Roll banks to MaxRoll (5)"), FMath::IsNearlyEqual(FMath::Abs(View.Rotation.Roll), 5.f, 0.25f));
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.0; return true; }); // target stopped
	Steps.Add([](FScenario& S)
	{
		const FMinimalViewInfo& View = S.PCM()->GetCameraCacheView();
		S.Test->AddInfo(FString::Printf(TEXT("Stopped: FOV %.2f roll %.2f"), View.FOV, View.Rotation.Roll));
		S.Test->TestTrue(TEXT("FOV returns to base when the target stops"), FMath::IsNearlyEqual(View.FOV, 90.f, 0.5f));
		S.Test->TestTrue(TEXT("Roll returns to level"), FMath::Abs(View.Rotation.Roll) < 0.25f);
		S.ActorA->Destroy(); // motion target destroyed: no crash, effect keeps running at rest
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Effect still active after its target was destroyed"), S.Subsystem->GetActiveCameraEffects().Num(), 1);
		S.Test->TestTrue(TEXT("FOV stays at base"), FMath::IsNearlyEqual(S.PCM()->GetFOVAngle(), 90.f, 0.5f));
		S.Subsystem->StopAllCameraEffects(true);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationEffectLifetimeTest, "ActionCameraFramework.Integration.EffectsSurvivePcmAndActorLoss",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationEffectLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Pivot = TestArea + FVector(-3000, -9000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		S.Preset.Reset(MakeOrbitCollisionPreset(600.f, 0.f, FACFCameraBlendSettings::MakeCut()));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		S.EffectPresets.Reset();
		S.EffectPresets.Emplace(MakeEffectPreset([](UACFCameraEffectPreset* P) { AddPunch(P, FVector(-100, 0, 0), -1.f); })); // infinite
		S.Subsystem->PlayCameraEffect(S.EffectPresets[0].Get(), FACFCameraEffectParams());
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 0.2; return true; }); // past the 0.05 s attack
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, S.Pivot - FVector(700, 0, 0), 0.01, TEXT("Infinite punch active"));
		APlayerCameraManager* Old = S.PCM();
		S.Controller->SpawnPlayerCameraManager();
		if (S.PCM() != Old)
		{
			Old->Destroy();
		}
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, S.Pivot - FVector(700, 0, 0), 0.01, TEXT("Effect still applied after PCM recreation"));
		S.Subsystem->StopAllCameraEffects(true);
		// Impulse with a source actor and an instigator actor, both destroyed while it plays.
		S.ActorB = SpawnMovablePoint(S.World(), S.Pivot + FVector(-300, 0, 0));
		AActor* Instigator = SpawnMovablePoint(S.World(), S.Pivot + FVector(0, 500, 0));
		S.Spawned.Add(Instigator);
		S.EffectPresets.Emplace(MakeEffectPreset([](UACFCameraEffectPreset* P)
		{
			UACFCameraEffect_Impulse* Impulse = NewObject<UACFCameraEffect_Impulse>(P);
			Impulse->Envelope = HoldEnvelope(-1.f);
			P->Layers.Add(Impulse);
		}));
		FACFCameraEffectParams Params;
		Params.SourceActor = S.ActorB.Get();
		Params.Instigator = Instigator;
		S.Handle = FACFCameraRequestHandle(S.Subsystem->PlayCameraEffect(S.EffectPresets[1].Get(), Params).GetId());
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.ActorB->Destroy(); // source gone: keeps its last location
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Impulse survives the loss of its source actor"), S.Subsystem->GetActiveCameraEffects().Num(), 1);
		S.Spawned.Last()->Destroy(); // instigator gone: effect stops
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Instigator destroyed -> effect removed"), S.Subsystem->GetActiveCameraEffects().Num(), 0);
		CameraCloseTo(S, S.Pivot - FVector(600, 0, 0), 0.01, TEXT("No effect offset left"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationBlueprintEffectsTest, "ActionCameraFramework.Integration.BlueprintEffects",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationBlueprintEffectsTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	// Scripts/CreateVerificationAssets.py: BeginPlay -> request orbit camera -> Play ACF Camera Recoil / Impulse / Shake
	// -> Delay 0.5 -> Stop ACF Camera Effect(shake handle) -> Delay 0.5 -> Stop All ACF Camera Effects -> release.
	static const TCHAR* ClassPath = TEXT("/ActionCameraFramework/Verification/BP_ACFEffectTest.BP_ACFEffectTest_C");
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		UClass* BlueprintClass = LoadClass<AActor>(nullptr, ClassPath);
		if (!S.Test->TestNotNull(TEXT("Verification Blueprint exists (run Scripts/CreateVerificationAssets.py)"), BlueprintClass))
		{
			return false;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		S.ActorA = S.World()->SpawnActor<AActor>(BlueprintClass, FTransform::Identity, Params);
		S.StartTime = S.World()->GetTimeSeconds();
		return S.Test->TestTrue(TEXT("Spawned"), S.ActorA.IsValid());
	});
	Steps.Add([](FScenario& S)
	{
		const TArray<FACFCameraEffectInfo> Effects = S.Subsystem->GetActiveCameraEffects();
		FString Names;
		for (const FACFCameraEffectInfo& Info : Effects)
		{
			Names += FString::Printf(TEXT("%s(%s w=%.2f) "), *Info.Handle.ToString(), *Info.PresetName.ToString(), Info.Weight);
		}
		S.Test->AddInfo(FString::Printf(TEXT("%.3fs after spawn: %s"), S.World()->GetTimeSeconds() - S.StartTime, *Names));
		S.Test->TestEqual(TEXT("Recoil + Impulse + Shake playing from Blueprint"), Effects.Num(), 3);
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 0.75; return true; });
	Steps.Add([](FScenario& S)
	{
		// Recoil (0.33 s) and impulse (0.6 s) have ended; the 2 s shake only ends because Blueprint stopped its handle.
		S.Test->TestEqual(TEXT("Stop ACF Camera Effect stopped the shake by handle"), S.Subsystem->GetActiveCameraEffects().Num(), 0);
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.3; return true; });
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Blueprint released its camera"), S.Subsystem->GetLastEvaluationResult().NumRequests, 0);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSampleMapScreenshotTest, "ActionCameraFramework.Samples.TestMapScreenshot",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFSampleMapScreenshotTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	// Visual check helper (needs a real RHI, i.e. no -NullRHI): saves Saved/Screenshots/<Platform>/ACF_TestMap_<n>.png.
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		if (!S.World()->GetMapName().Contains(TEXT("L_ACFCameraTest")))
		{
			S.Test->AddWarning(TEXT("Skipped: run on /ActionCameraFramework/Samples/L_ACFCameraTest."));
			return false;
		}
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 3.0; return true; }); // let exposure settle
	Steps.Add([](FScenario& S)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("ACF_TestMap_0"), false, false);
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Use ThirdPerson 0"));
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.0; return true; });
	Steps.Add([](FScenario& S)
	{
		S.Controller->SetControlRotation(FRotator(-25.f, 150.f, 0.f));
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.0; return true; });
	Steps.Add([](FScenario& S)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("ACF_TestMap_1"), false, false);
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.0; return true; });
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Phase 4A: Lock-On with Collision, Effects, target destruction and Target Group in the real camera pipeline.

namespace ACFIntegration
{
	static UACFCameraPreset* MakeLockOnCollisionPreset()
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_LockOn* LockOn = NewObject<UACFCameraNode_LockOn>(Preset);
		LockOn->PivotOffset = FVector::ZeroVector;
		LockOn->ShoulderOffset = FVector::ZeroVector;
		LockOn->BasePitch = 0.f;
		LockOn->ElevationPitchScale = 0.f;
		LockOn->NearArmLength = LockOn->FarArmLength = 600.f;
		UACFCameraNode_GroupFraming* Framing = NewObject<UACFCameraNode_GroupFraming>(Preset);
		Framing->bFrameTargetGroup = false;
		Framing->FollowTargetSubject.bInclude = true;
		Framing->LookAtTargetSubject.bInclude = true;
		UACFCameraNode_Collision* Collision = NewObject<UACFCameraNode_Collision>(Preset);
		Collision->ReturnDampingTime = 0.f;
		Preset->Nodes = { LockOn, Framing, Collision };
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		return Preset;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationLockOnTest, "ActionCameraFramework.Integration.LockOnCollisionEffectsAndDestroy",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationLockOnTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		// Player and enemy on the X axis: the lock-on camera sits on -X behind the player.
		S.Pivot = TestArea + FVector(0, 12000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		S.ActorB = SpawnMovablePoint(S.World(), S.Pivot + FVector(1500, 0, 0));
		S.Preset.Reset(MakeLockOnCollisionPreset());
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		Params.LookAtTarget.Actor = S.ActorB.Get();
		Params.bReleaseWhenLookAtTargetLost = true;
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const FVector Camera = S.PCM()->GetCameraLocation();
		S.LogCamera(TEXT("Lock-on, free space"));
		S.Test->TestTrue(TEXT("Lock-on: camera behind the player, away from the enemy"), Camera.X < S.Pivot.X - 500.0 && FMath::Abs(Camera.Y - S.Pivot.Y) < 1.0);
		S.Test->TestTrue(TEXT("Lock-on: looking towards the enemy"), S.PCM()->GetCameraRotation().Vector().X > 0.95);
		// Wall behind the player: face at pivot.X - 290.
		SpawnBlocker(S, S.Pivot - FVector(300, 0, 0), FVector(10, 400, 400));
		SetGuardEnabled(false); // ACF's own Collision must handle it
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const double CameraX = S.PCM()->GetCameraLocation().X;
		const double Clearance = S.Pivot.X - 290.0 + 12.0;
		S.LogCamera(TEXT("Lock-on with a wall behind the player"));
		S.Test->TestTrue(TEXT("Lock-on + Collision: camera stays in front of the wall"), CameraX >= Clearance - 0.5);
		S.Value = S.PCM()->GetFOVAngle();
		// Effects on top of the lock-on camera: FOV punch + backwards punch into the wall.
		S.EffectPresets.Reset();
		S.EffectPresets.Emplace(MakeEffectPreset([](UACFCameraEffectPreset* P)
		{
			UACFCameraEffect_Punch* Punch = AddPunch(P, FVector(-200, 0, 0), 1.f);
			Punch->FOV = 10.f;
		}));
		S.Subsystem->PlayCameraEffect(S.EffectPresets[0].Get(), FACFCameraEffectParams());
		S.StartTime = S.World()->GetTimeSeconds();
		S.bFlag = true;
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.bFlag &= S.PCM()->GetCameraLocation().X >= S.Pivot.X - 290.0 + 12.0 - 0.5;
		S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 0.4;
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const float Fov = S.PCM()->GetFOVAngle();
		S.Test->AddInfo(FString::Printf(TEXT("Lock-on + punch: FOV %.2f (before %.2f)"), Fov, S.Value));
		S.Test->TestTrue(TEXT("Lock-on + Effects: FOV punch applied on top of the lock-on camera"), Fov > S.Value + 5.f);
		S.Test->TestTrue(TEXT("Lock-on + Effects: backwards punch never pushes through the wall"), S.bFlag);
		SetGuardEnabled(true);
		S.Subsystem->StopAllCameraEffects(true);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		// Target group on the same request: add an actor far to the side.
		S.SavedLocation = S.PCM()->GetCameraLocation(); // effects are gone now
		S.Spawned.Add(SpawnMovablePoint(S.World(), S.Pivot + FVector(800, 2500, 0)));
		S.Test->TestTrue(TEXT("Add group actor"), S.Subsystem->AddCameraGroupActor(S.Handle, S.Spawned.Last().Get(), 1.f, 60.f));
		S.Test->TestEqual(TEXT("Group has one member"), S.Subsystem->GetCameraGroupMembers(S.Handle).Num(), 1);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		// The preset frames Follow + Look At only (Frame Target Group off), so the extra member must not change anything yet.
		S.Test->TestTrue(TEXT("Group member does not affect a preset whose Group Framing has Frame Target Group off"), S.PCM()->GetCameraLocation().Equals(S.SavedLocation, 0.5));
		S.Spawned.Last()->Destroy();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Destroyed group actor removed automatically"), S.Subsystem->GetCameraGroupMembers(S.Handle).Num(), 0);
		// The locked enemy dies: the request releases itself (bReleaseWhenLookAtTargetLost).
		S.ActorB->Destroy();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestFalse(TEXT("Enemy destroyed: lock-on request released automatically"), S.Subsystem->IsCameraRequestAlive(S.Handle));
		S.Test->TestEqual(TEXT("Back on the UE camera"), S.Subsystem->GetLastEvaluationResult().Weight, 0.f);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 2));
	return true;
}

namespace ACFIntegration
{
	/** True if the sphere is fully on screen in the PlayerCameraManager's final camera. */
	static bool SphereOnScreen(FScenario& S, const FVector& Center, float Radius, FVector2D& OutScreen)
	{
		const APlayerCameraManager* M = S.PCM();
		const ACFFraming::FLens Lens = ACFFraming::MakeLens(M->GetFOVAngle(), 16.f / 9.f);
		const FVector Local = M->GetCameraRotation().UnrotateVector(Center - M->GetCameraLocation());
		double Depth = 0.0;
		ACFFraming::Project(M->GetCameraLocation(), M->GetCameraRotation(), Lens, Center, OutScreen, Depth);
		const double Fh = Lens.TanHalfH;
		const double Fv = Lens.TanHalfV;
		const double H = (Fh * Local.X - FMath::Abs(Local.Y)) / FMath::Sqrt(1.0 + Fh * Fh);
		const double V = (Fv * Local.X - FMath::Abs(Local.Z)) / FMath::Sqrt(1.0 + Fv * Fv);
		return FMath::Min3(H, V, Local.X) >= Radius - 0.5;
	}

	static AActor* FindEnemy(FScenario& S, const TCHAR* Label)
	{
		TArray<AActor*> Enemies;
		UGameplayStatics::GetAllActorsWithTag(S.World(), TEXT("ACFEnemy"), Enemies);
		AActor** Found = Enemies.FindByPredicate([Label](const AActor* A) { return A->GetActorNameOrLabel().StartsWith(Label); });
		return Found ? *Found : nullptr;
	}

	static void ExpectOnScreen(FScenario& S, AActor* Actor, float Radius, const TCHAR* What)
	{
		FVector2D Screen;
		const bool bOk = Actor && SphereOnScreen(S, Actor->GetActorLocation(), Radius, Screen);
		S.Test->AddInfo(FString::Printf(TEXT("%s: %s at screen (%.3f, %.3f)"), What, Actor ? *Actor->GetActorNameOrLabel() : TEXT("<none>"), Screen.X, Screen.Y));
		S.Test->TestTrue(FString::Printf(TEXT("%s: %s fully on screen"), What, Actor ? *Actor->GetActorNameOrLabel() : TEXT("<none>")), bOk);
	}

	static void Wait(TArray<FStep>& Steps, double Seconds)
	{
		Steps.Add([](FScenario& S) { S.StartTime = S.World()->GetTimeSeconds(); return true; });
		Steps.Add([Seconds](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < Seconds; return true; });
	}

	static void Shot(TArray<FStep>& Steps, const TCHAR* Name)
	{
		Steps.Add([Name](FScenario& S)
		{
			if (!FApp::CanEverRender())
			{
				return true;
			}
			FScreenshotRequest::RequestScreenshot(Name, false, false);
			return true;
		});
		Wait(Steps, 0.3);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSampleLockOnGroupTest, "ActionCameraFramework.Samples.LockOnAndGroupOnTestMap",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFSampleLockOnGroupTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	// Drives the README console commands on the real sample map (real level geometry, the project's pawn).
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		if (!S.World()->GetMapName().Contains(TEXT("L_ACFCameraTest")))
		{
			S.Test->AddWarning(TEXT("Skipped: run on /ActionCameraFramework/Samples/L_ACFCameraTest."));
			return false;
		}
		if (!FindEnemy(S, TEXT("Enemy_3")))
		{
			S.Test->AddError(TEXT("ACFEnemy actors missing: run Scripts/CreateManualTestAssets.py."));
			return false;
		}
		S.Controller->GetPawn()->TeleportTo(FVector(0.f, 0.f, 120.f), FRotator::ZeroRotator);
		S.Controller->SetControlRotation(FRotator(-15.f, 0.f, 0.f));
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Use ThirdPerson 0"));
		return true;
	});
	Wait(Steps, 0.5);
	Steps.Add([](FScenario& S) { GEngine->Exec(S.World(), TEXT("ACF.Sample.LockOn 0")); return true; });
	Wait(Steps, 1.5);
	Steps.Add([](FScenario& S)
	{
		const FACFCameraEvaluationResult R = S.Subsystem->GetLastEvaluationResult();
		S.Test->TestTrue(TEXT("Lock-on request in control"), R.NumRequests == 2 && !R.bIsBlending && FMath::IsNearlyEqual(R.Weight, 1.f));
		ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_0")), 50.f, TEXT("Lock-on near enemy"));
		ExpectOnScreen(S, S.Controller->GetPawn(), 50.f, TEXT("Lock-on near enemy (player)"));
		return true;
	});
	Shot(Steps, TEXT("ACF_LockOn_0_Near"));
	Steps.Add([](FScenario& S) { GEngine->Exec(S.World(), TEXT("ACF.Sample.LockOn 2")); return true; }); // switch to the boss
	Wait(Steps, 2.0);
	Steps.Add([](FScenario& S)
	{
		ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_2")), 150.f, TEXT("Switched to the boss"));
		ExpectOnScreen(S, S.Controller->GetPawn(), 50.f, TEXT("Switched to the boss (player)"));
		return true;
	});
	Shot(Steps, TEXT("ACF_LockOn_1_Boss"));
	Steps.Add([](FScenario& S) { GEngine->Exec(S.World(), TEXT("ACF.Sample.LockOn 1")); return true; }); // far enemy
	Wait(Steps, 2.0);
	Steps.Add([](FScenario& S)
	{
		ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_1")), 50.f, TEXT("Far enemy"));
		ExpectOnScreen(S, S.Controller->GetPawn(), 50.f, TEXT("Far enemy (player)"));
		S.Value = S.PCM()->GetFOVAngle();
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Enemy Teleport 1500"));
		return true;
	});
	Wait(Steps, 2.0);
	Steps.Add([](FScenario& S)
	{
		S.Test->AddInfo(FString::Printf(TEXT("Far enemy FOV %.2f"), S.Value));
		ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_1")), 50.f, TEXT("After the enemy teleported"));
		GEngine->Exec(S.World(), TEXT("ACF.Sample.LockOn 3"));
		return true;
	});
	Wait(Steps, 1.5);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Lock-on alive before the kill"), S.Subsystem->GetLastEvaluationResult().NumRequests == 2);
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Enemy Kill"));
		return true;
	});
	Wait(Steps, 0.2);
	Steps.Add([](FScenario& S)
	{
		const FACFCameraEvaluationResult R = S.Subsystem->GetLastEvaluationResult();
		S.Test->TestEqual(TEXT("Killing the locked enemy releases the lock-on camera"), R.NumRequests, 1);
		return true;
	});
	Wait(Steps, 1.0);
	// Target group: player + near + boss.
	Steps.Add([](FScenario& S)
	{
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Group Use"));
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Group Add 0"));
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Group Add 2"));
		return true;
	});
	Wait(Steps, 2.5);
	Steps.Add([](FScenario& S)
	{
		ExpectOnScreen(S, S.Controller->GetPawn(), 60.f, TEXT("Group"));
		ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_0")), 80.f, TEXT("Group"));
		ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_2")), 80.f, TEXT("Group"));
		S.SavedLocation = S.PCM()->GetCameraLocation();
		return true;
	});
	Shot(Steps, TEXT("ACF_Group_0"));
	Steps.Add([](FScenario& S) { GEngine->Exec(S.World(), TEXT("ACF.Sample.Group Add 1")); return true; });
	Wait(Steps, 2.5);
	Steps.Add([](FScenario& S)
	{
		ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_1")), 80.f, TEXT("Group + far"));
		ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_2")), 80.f, TEXT("Group + far"));
		ExpectOnScreen(S, S.Controller->GetPawn(), 60.f, TEXT("Group + far"));
		S.Test->AddInfo(FString::Printf(TEXT("Group camera moved %.0f cm after adding the far enemy"), FVector::Dist(S.SavedLocation, S.PCM()->GetCameraLocation())));
		return true;
	});
	Shot(Steps, TEXT("ACF_Group_1"));
	Steps.Add([](FScenario& S)
	{
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Group Release 0"));
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Release 0"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 1));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
