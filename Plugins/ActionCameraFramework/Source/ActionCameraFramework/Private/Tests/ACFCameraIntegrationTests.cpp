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
#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraDirector.h"
#include "HAL/IConsoleManager.h"
#include "Engine/GameViewportClient.h"
#include "ACFCameraAnimNotifies.h"
#include "Nodes/ACFCameraNode_ShotAnchor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimNotifyQueue.h"

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
		/** Phase 4C: assets kept alive for the scenario and the playback under test. */
		TArray<TStrongObjectPtr<UObject>> Keep;
		FACFCameraSequenceHandle Sequence;
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
				State->Subsystem->GetDirector()->StopAllSequences(true);
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
		FVector2D Screen = FVector2D::ZeroVector;
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

// -------------------------------------------------------------------------------------------------
// Phase 4B: constraint solver (Deoccluder, Collision vs Group Framing) with real level geometry.

namespace ACFIntegration
{
	static UACFCameraPreset* MakeSolverLockOnPreset(bool bDeoccluder, bool bRecovery)
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
		Framing->FollowTargetSubject.Radius = 50.f;
		Framing->LookAtTargetSubject.bInclude = true;
		Framing->LookAtTargetSubject.Radius = 80.f;
		UACFCameraNode_Collision* Collision = NewObject<UACFCameraNode_Collision>(Preset);
		Collision->ReturnDampingTime = 0.f;
		Collision->Deoccluder.bEnabled = bDeoccluder;
		Collision->FramingRecovery.bEnabled = bRecovery;
		Collision->FramingRecovery.MaxFieldOfView = 120.f;
		Collision->FramingRecovery.MaxFieldOfViewIncrease = 30.f;
		Preset->Nodes = { LockOn, Framing, Collision };
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		return Preset;
	}

	/** Aspect ratio the subsystem uses for this player's view. */
	static float ViewAspect(FScenario& S)
	{
		const ULocalPlayer* LP = S.Controller->GetLocalPlayer();
		FVector2D Size = FVector2D::ZeroVector;
		if (LP && LP->ViewportClient)
		{
			LP->ViewportClient->GetViewportSize(Size);
			Size *= LP->Size;
		}
		return Size.X > 0.0 && Size.Y > 0.0 ? static_cast<float>(Size.X / Size.Y) : 16.f / 9.f;
	}

	/** Signed clearance (cm) of a sphere inside the centred framing area of the PCM's final camera (>= 0 = inside). */
	static double ClearanceInFrame(FScenario& S, const FVector& Center, float Radius, const FVector2D& Size)
	{
		const APlayerCameraManager* M = S.PCM();
		const ACFFraming::FLens Lens = ACFFraming::MakeLens(M->GetFOVAngle(), ViewAspect(S));
		const FVector Local = M->GetCameraRotation().UnrotateVector(Center - M->GetCameraLocation());
		const double Fh = Lens.TanHalfH * Size.X;
		const double Fv = Lens.TanHalfV * Size.Y;
		const double H = (Fh * Local.X - FMath::Abs(Local.Y)) / FMath::Sqrt(1.0 + Fh * Fh) - Radius;
		const double V = (Fv * Local.X - FMath::Abs(Local.Z)) / FMath::Sqrt(1.0 + Fv * Fv) - Radius;
		return FMath::Min3(H, V, Local.X - Radius);
	}

	static bool LineOfSightClear(FScenario& S, const FVector& To, const AActor* Ignore1, const AActor* Ignore2)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ACFTestLos), false);
		Params.AddIgnoredActor(Ignore1);
		Params.AddIgnoredActor(Ignore2);
		FHitResult Hit;
		return !S.World()->LineTraceSingleByChannel(Hit, S.PCM()->GetCameraLocation(), To, ECC_Camera, Params);
	}

	static void SetSolverCVar(int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("ACF.Debug.Solver")))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	}

	static void WaitSeconds(TArray<FStep>& Steps, double Seconds)
	{
		Steps.Add([](FScenario& S) { S.StartTime = S.World()->GetTimeSeconds(); return true; });
		Steps.Add([Seconds](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < Seconds; return true; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationDeoccluderTest, "ActionCameraFramework.Integration.ConstraintsLockOnBehindPillar",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationDeoccluderTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Pivot = TestArea + FVector(0, 15000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		S.ActorB = SpawnMovablePoint(S.World(), S.Pivot + FVector(1500, 0, 0));
		SpawnBlocker(S, S.Pivot + FVector(600, 0, 0), FVector(40, 40, 300)); // pillar on the player -> enemy line
		S.Preset.Reset(MakeSolverLockOnPreset(true, true));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		Params.LookAtTarget.Actor = S.ActorB.Get();
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestFalse(TEXT("Activation: the pillar hides the enemy"), LineOfSightClear(S, S.ActorB->GetActorLocation(), S.ActorA.Get(), S.ActorB.Get()));
		return true;
	});
	WaitSeconds(Steps, 1.5);
	Steps.Add([](FScenario& S)
	{
		const FACFCameraSolverReport& Report = S.Subsystem->GetDirector()->GetLastSolverReport();
		S.LogCamera(TEXT("Deoccluded"));
		S.Test->AddInfo(FString::Printf(TEXT("Deoccluder correction yaw %.1f pitch %.1f pull %.2f, occluded %d, traces/frame %d"),
			Report.Correction.X, Report.Correction.Y, Report.Correction.Z, Report.NumOccluded, Report.NumTraces));
		S.Test->TestTrue(TEXT("Camera moved around the pillar"), FMath::Abs(Report.Correction.X) > 5.f || Report.Correction.Y > 5.f);
		S.Test->TestTrue(TEXT("Enemy visible (real line trace)"), LineOfSightClear(S, S.ActorB->GetActorLocation(), S.ActorA.Get(), S.ActorB.Get()));
		S.Test->TestTrue(TEXT("Player visible (real line trace)"), LineOfSightClear(S, S.Pivot, S.ActorA.Get(), S.ActorB.Get()));
		S.Test->TestEqual(TEXT("Nothing reported hidden"), S.Subsystem->GetLastEvaluationResult().NumOccludedTargets, 0);
		S.Test->TestTrue(TEXT("Steady state trace budget (1 sweep + 1 per subject, + base check)"), Report.NumTraces <= 6);

		// Second evaluation in the same frame: cached (no extra traces, same pose).
		FMinimalViewInfo View = S.PCM()->GetCameraCacheView();
		const FACFCameraEvaluationResult First = S.Subsystem->EvaluateForOutput(1.f / 60.f, View);
		const int32 Traces = S.Subsystem->GetDirector()->GetLastSolverReport().NumTraces;
		const FACFCameraEvaluationResult Second = S.Subsystem->EvaluateForOutput(1.f / 60.f, View);
		S.Test->TestTrue(TEXT("Same-frame re-evaluation is cached"), First.Pose.Location.Equals(Second.Pose.Location) && First.FrameNumber == Second.FrameNumber
			&& Traces == S.Subsystem->GetDirector()->GetLastSolverReport().NumTraces);

		// Effect on top of the deoccluded camera.
		S.EffectPresets.Reset();
		S.EffectPresets.Emplace(MakeEffectPreset([](UACFCameraEffectPreset* P) { AddPunch(P, FVector::ZeroVector, 0.5f)->FOV = 8.f; }));
		S.Value = S.PCM()->GetFOVAngle();
		S.Subsystem->PlayCameraEffect(S.EffectPresets[0].Get(), FACFCameraEffectParams());
		return true;
	});
	WaitSeconds(Steps, 0.2);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Effect FOV on top of the deoccluded camera"), S.PCM()->GetFOVAngle() > S.Value + 5.f);
		S.Test->TestTrue(TEXT("Enemy still visible during the effect"), LineOfSightClear(S, S.ActorB->GetActorLocation(), S.ActorA.Get(), S.ActorB.Get()));
		S.Subsystem->StopAllCameraEffects(true);
		// The pillar disappears: after Clear Delay the camera glides back behind the player.
		S.Spawned[0]->Destroy();
		return true;
	});
	WaitSeconds(Steps, 5.0);
	Steps.Add([](FScenario& S)
	{
		const FVector Correction = S.Subsystem->GetDirector()->GetLastSolverReport().Correction;
		S.Test->AddInfo(FString::Printf(TEXT("After the pillar was removed: correction %s"), *Correction.ToString()));
		S.Test->TestTrue(TEXT("Correction released"), Correction.IsNearlyZero(0.5));
		S.Test->TestTrue(TEXT("Back on the player -> enemy line"), FMath::Abs(S.PCM()->GetCameraLocation().Y - S.Pivot.Y) < 2.0);
		S.ActorB->Destroy(); // target destroyed while locked (no auto release requested): held, no crash
		return true;
	});
	WaitSeconds(Steps, 0.5);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Target destroyed: camera finite and request alive"), !S.PCM()->GetCameraLocation().ContainsNaN() && S.Subsystem->IsCameraRequestAlive(S.Handle));
		S.Test->TestEqual(TEXT("Target destroyed: nothing reported hidden"), S.Subsystem->GetLastEvaluationResult().NumOccludedTargets, 0);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationFramingConflictTest, "ActionCameraFramework.Integration.ConstraintsCollisionVsGroupFraming",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationFramingConflictTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	static const FVector2D Size(0.8, 0.8);
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		// Exploration camera framing a group (player + two enemies to the sides); a wall behind the player.
		S.Pivot = TestArea + FVector(0, 18000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		S.Spawned.Add(SpawnMovablePoint(S.World(), S.Pivot + FVector(400, 500, 0)));
		S.Spawned.Add(SpawnMovablePoint(S.World(), S.Pivot + FVector(400, -500, 0)));
		SpawnBlocker(S, S.Pivot - FVector(300, 0, 0), FVector(10, 900, 400)); // face at pivot.X - 290
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_OrbitalFollow* Orbit = NewObject<UACFCameraNode_OrbitalFollow>(Preset);
		Orbit->InputSource = EACFCameraOrbitInputSource::None;
		Orbit->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
		Orbit->InitialPitch = 0.f;
		Orbit->PivotOffset = FVector::ZeroVector;
		Orbit->Distance = 600.f;
		UACFCameraNode_GroupFraming* Framing = NewObject<UACFCameraNode_GroupFraming>(Preset);
		Framing->Mode = EACFCameraGroupFramingMode::Distance;
		Framing->FramingSize = Size;
		UACFCameraNode_Collision* Collision = NewObject<UACFCameraNode_Collision>(Preset);
		Collision->ReturnDampingTime = 0.f;
		Collision->FramingRecovery.bEnabled = true;
		Collision->FramingRecovery.MaxFieldOfView = 130.f;
		Collision->FramingRecovery.MaxFieldOfViewIncrease = 40.f;
		Preset->Nodes = { Orbit, Framing, Collision };
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		S.Preset.Reset(Preset);
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		Params.TargetGroup.Members.Add(UACFCameraBlueprintLibrary::MakeTargetGroupMember(S.ActorA.Get(), 1.f, 50.f));
		Params.TargetGroup.Members.Add(UACFCameraBlueprintLibrary::MakeTargetGroupMember(S.Spawned[0].Get(), 1.f, 80.f));
		Params.TargetGroup.Members.Add(UACFCameraBlueprintLibrary::MakeTargetGroupMember(S.Spawned[1].Get(), 1.f, 80.f));
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		SetSolverCVar(0); // first reproduce the Phase 4A conflict
		return true;
	});
	WaitSeconds(Steps, 0.5);
	auto Worst = [](FScenario& S)
	{
		return FMath::Min3(ClearanceInFrame(S, S.Pivot, 50.f, Size),
			ClearanceInFrame(S, S.Spawned[0]->GetActorLocation(), 80.f, Size), ClearanceInFrame(S, S.Spawned[1]->GetActorLocation(), 80.f, Size));
	};
	Steps.Add([Worst](FScenario& S)
	{
		const double Clearance = Worst(S);
		S.LogCamera(TEXT("Solver OFF"));
		S.Test->AddInfo(FString::Printf(TEXT("Solver OFF: camera X %.1f (wall face %.1f), worst clearance %.1f cm"), S.PCM()->GetCameraLocation().X - S.Pivot.X, -290.0, Clearance));
		S.Test->TestTrue(TEXT("Conflict reproduced: Collision pushed the camera in and subjects left the framing area"), Clearance < -5.0);
		SetSolverCVar(1);
		return true;
	});
	WaitSeconds(Steps, 1.0);
	Steps.Add([Worst](FScenario& S)
	{
		const double Clearance = Worst(S);
		const float Fov = S.PCM()->GetFOVAngle();
		S.LogCamera(TEXT("Solver ON"));
		S.Test->AddInfo(FString::Printf(TEXT("Solver ON: FOV %.2f, worst clearance %.2f cm"), Fov, Clearance));
		S.Test->TestTrue(TEXT("Camera still in front of the wall (priority 1)"), S.PCM()->GetCameraLocation().X >= S.Pivot.X - 290.0 + 12.0 - 0.5);
		S.Test->TestTrue(TEXT("FOV widened within the limits"), Fov > 90.5f && Fov <= 130.f);
		S.Test->TestTrue(TEXT("All subjects back inside the framing area"), Clearance >= -1.0);
		S.Test->TestFalse(TEXT("Not limited"), S.Subsystem->GetLastEvaluationResult().bFramingLimited);
		// FOV limit: allow only +4 deg.
		Cast<UACFCameraNode_Collision>(S.Preset->Nodes[2])->FramingRecovery.MaxFieldOfViewIncrease = 4.f;
		return true;
	});
	WaitSeconds(Steps, 4.0);
	Steps.Add([](FScenario& S)
	{
		const float Fov = S.PCM()->GetFOVAngle();
		S.Test->AddInfo(FString::Printf(TEXT("FOV limit +4: FOV %.2f"), Fov));
		S.Test->TestTrue(TEXT("FOV stops at the limit"), FMath::IsNearlyEqual(Fov, 94.f, 0.1f));
		S.Test->TestTrue(TEXT("Limit reported"), S.Subsystem->GetLastEvaluationResult().bFramingLimited);
		S.Test->TestTrue(TEXT("Player still inside the framing area (priority 2)"), ClearanceInFrame(S, S.Pivot, 50.f, Size) >= -1.0);
		// Lock-on against the same wall.
		S.Subsystem->ReleaseAllCameras(true);
		S.ActorB = SpawnMovablePoint(S.World(), S.Pivot + FVector(1200, 150, 0));
		S.Preset.Reset(MakeSolverLockOnPreset(true, true));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		Params.LookAtTarget.Actor = S.ActorB.Get();
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		return true;
	});
	WaitSeconds(Steps, 1.5);
	Steps.Add([](FScenario& S)
	{
		S.LogCamera(TEXT("Lock-on at the wall"));
		S.Test->TestTrue(TEXT("Lock-on at the wall: camera in front of the wall"), S.PCM()->GetCameraLocation().X >= S.Pivot.X - 290.0 + 12.0 - 0.5);
		S.Test->TestTrue(TEXT("Lock-on at the wall: player on screen"), ClearanceInFrame(S, S.Pivot, 50.f, FVector2D(1, 1)) >= -1.0);
		S.Test->TestTrue(TEXT("Lock-on at the wall: enemy on screen"), ClearanceInFrame(S, S.ActorB->GetActorLocation(), 80.f, FVector2D(1, 1)) >= -1.0);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationCorridorTest, "ActionCameraFramework.Integration.ConstraintsNarrowCorridor",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationCorridorTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	static constexpr double HalfWidth = 135.0;
	static constexpr double Ceiling = 250.0;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Pivot = TestArea + FVector(0, 21000, 90);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		S.ActorB = SpawnMovablePoint(S.World(), S.Pivot + FVector(900, 0, 0));
		const FVector Base = S.Pivot - FVector(0, 0, 90);
		SpawnBlocker(S, Base + FVector(0, HalfWidth + 10, 150), FVector(1500, 10, 300));
		SpawnBlocker(S, Base + FVector(0, -HalfWidth - 10, 150), FVector(1500, 10, 300));
		SpawnBlocker(S, Base + FVector(0, 0, Ceiling + 10), FVector(1500, 200, 10));
		SpawnBlocker(S, Base + FVector(450, 0, 100), FVector(15, 15, 100)); // thin post between player and enemy
		S.Preset.Reset(MakeSolverLockOnPreset(true, true));
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorA.Get();
		Params.LookAtTarget.Actor = S.ActorB.Get();
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), Params);
		S.StartTime = S.World()->GetTimeSeconds();
		S.bFlag = true;
		S.Value = 0.0;   // sign changes of the correction
		S.SavedLocation = FVector(-100.0, 0.0, 0.0); // X = time of the last change, Y = changes less than 0.75 s apart
		S.SavedRotation = FRotator::ZeroRotator; // .Yaw = last correction sign
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		// Enemy strafes across the corridor for 4 s.
		const double T = S.World()->GetTimeSeconds() - S.StartTime;
		S.ActorB->SetActorLocation(S.Pivot + FVector(900, 100.0 * FMath::Sin(T * 2.0), 0));
		const FVector Camera = S.PCM()->GetCameraLocation();
		const FVector Base = S.Pivot - FVector(0, 0, 90);
		S.bFlag &= FMath::Abs(Camera.Y - Base.Y) <= HalfWidth - 12.0 + 0.5 && Camera.Z - Base.Z <= Ceiling - 12.0 + 0.5;
		const float Yaw = S.Subsystem->GetDirector()->GetLastSolverReport().Correction.X;
		const float Sign = FMath::Abs(Yaw) > 1.f ? FMath::Sign(Yaw) : 0.f;
		if (Sign != 0.f && S.SavedRotation.Yaw != 0.f && Sign != S.SavedRotation.Yaw)
		{
			S.Value += 1.0;
			S.SavedLocation.Y += T - S.SavedLocation.X < 0.75 ? 1.0 : 0.0;
			S.SavedLocation.X = T;
		}
		if (Sign != 0.f)
		{
			S.SavedRotation.Yaw = Sign;
		}
		S.bRetry = T < 4.0;
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const FACFCameraSolverReport& Report = S.Subsystem->GetDirector()->GetLastSolverReport();
		S.LogCamera(TEXT("Corridor"));
		S.Test->AddInfo(FString::Printf(TEXT("Corridor: correction %s, side changes %.0f (rapid %.0f), occluded %d"), *Report.Correction.ToString(), S.Value, S.SavedLocation.Y, Report.NumOccluded));
		S.Test->TestTrue(TEXT("Corridor: the camera never leaves the corridor (walls, ceiling)"), S.bFlag);
		S.Test->TestTrue(TEXT("Corridor: no left-right oscillation (no side changes less than 0.75 s apart)"), S.SavedLocation.Y == 0.0);
		S.Test->TestTrue(TEXT("Corridor: player visible"), LineOfSightClear(S, S.Pivot, S.ActorA.Get(), S.ActorB.Get()));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSampleSolverCombatTest, "ActionCameraFramework.Samples.SolverAndCombatOnTestMap",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFSampleSolverCombatTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	// Drives the Phase 4B README commands on the real sample map.
	TArray<FStep> Steps;
	auto Exec = [&Steps](const TCHAR* Command) { Steps.Add([Command](FScenario& S) { GEngine->Exec(S.World(), Command); return true; }); };
	// "%s" in Command is replaced by the current index of the enemy whose label starts with Label (indices shift when one is killed).
	auto ExecEnemy = [&Steps](const TCHAR* Command, const TCHAR* Label)
	{
		Steps.Add([Command, Label](FScenario& S)
		{
			TArray<AActor*> Enemies;
			UGameplayStatics::GetAllActorsWithTag(S.World(), TEXT("ACFEnemy"), Enemies);
			Enemies.Sort([](const AActor& A, const AActor& B) { return A.GetActorNameOrLabel() < B.GetActorNameOrLabel(); });
			const int32 Index = Enemies.IndexOfByPredicate([Label](const AActor* A) { return A->GetActorNameOrLabel().StartsWith(Label); });
			S.Test->TestTrue(FString::Printf(TEXT("%s exists"), Label), Index != INDEX_NONE);
			GEngine->Exec(S.World(), *FString(Command).Replace(TEXT("%s"), *FString::FromInt(Index)));
			return true;
		});
	};
	auto ReportOccluded = [](FScenario& S, const TCHAR* Label, AActor* Enemy)
	{
		const FACFCameraEvaluationResult R = S.Subsystem->GetLastEvaluationResult();
		const bool bVisible = Enemy && LineOfSightClear(S, Enemy->GetActorLocation(), S.Controller->GetPawn(), Enemy);
		S.LogCamera(Label);
		S.Test->AddInfo(FString::Printf(TEXT("%s: occluded reported %d, enemy line of sight %s, FOV %.1f"), Label, R.NumOccludedTargets, bVisible ? TEXT("clear") : TEXT("blocked"), S.PCM()->GetFOVAngle()));
		S.Test->TestTrue(FString::Printf(TEXT("%s: the report matches the real line of sight"), Label), (R.NumOccludedTargets == 0) == bVisible);
		S.Test->TestTrue(FString::Printf(TEXT("%s: player visible"), Label), LineOfSightClear(S, S.Controller->GetPawn()->GetActorLocation() + FVector(0, 0, 60), S.Controller->GetPawn(), Enemy));
	};
	Steps.Add([](FScenario& S)
	{
		if (!S.World()->GetMapName().Contains(TEXT("L_ACFCameraTest")))
		{
			S.Test->AddWarning(TEXT("Skipped: run on /ActionCameraFramework/Samples/L_ACFCameraTest."));
			return false;
		}
		if (!FindEnemy(S, TEXT("Enemy_5")))
		{
			S.Test->AddError(TEXT("Enemy_4 / Enemy_5 missing: run Scripts/CreateManualTestAssets.py."));
			return false;
		}
		return true;
	});
	Exec(TEXT("ACF.Sample.Goto Start"));
	Exec(TEXT("ACF.Sample.Use ThirdPerson 0"));
	Wait(Steps, 0.5);
	// Lock-on to the boss behind the wall.
	ExecEnemy(TEXT("ACF.Sample.LockOn %s"), TEXT("Enemy_2"));
	Wait(Steps, 2.5);
	Steps.Add([ReportOccluded](FScenario& S) { ReportOccluded(S, TEXT("Boss behind the wall"), FindEnemy(S, TEXT("Enemy_2"))); return true; });
	Shot(Steps, TEXT("ACF_4B_BossBehindWall"));
	// Enemy patrolling around the pillars.
	Exec(TEXT("ACF.Sample.Goto Pillars"));
	ExecEnemy(TEXT("ACF.Sample.LockOn %s"), TEXT("Enemy_4"));
	Exec(TEXT("ACF.Sample.Enemy Patrol"));
	Steps.Add([](FScenario& S) { S.StartTime = S.World()->GetTimeSeconds(); S.Value = 0.0; S.bFlag = true; return true; });
	Steps.Add([](FScenario& S)
	{
		// While it patrols (6 s), the camera never ends up inside a pillar.
		const FVector Camera = S.PCM()->GetCameraLocation();
		for (const double Y : { -1600.0, -1200.0, -800.0 })
		{
			S.bFlag &= !(FMath::Abs(Camera.X + 600.0) < 30.0 + 14.5 && FMath::Abs(Camera.Y - Y) < 30.0 + 14.5 && Camera.Z < 400.0);
		}
		S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 6.0;
		return true;
	});
	Steps.Add([ReportOccluded](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Patrol around the pillars: camera never inside a pillar"), S.bFlag);
		ReportOccluded(S, TEXT("Enemy patrolling the pillars"), FindEnemy(S, TEXT("Enemy_4")));
		return true;
	});
	Shot(Steps, TEXT("ACF_4B_Pillars"));
	Exec(TEXT("ACF.Sample.Enemy Patrol"));
	// Combat state switch (exploration camera <-> combat profile).
	Exec(TEXT("ACF.Sample.LockOff 0"));
	Wait(Steps, 0.5);
	Steps.Add([](FScenario& S) { S.Value = S.PCM()->GetFOVAngle(); GEngine->Exec(S.World(), TEXT("ACF.Sample.Combat On")); return true; });
	Wait(Steps, 2.0);
	Steps.Add([](FScenario& S)
	{
		S.Test->AddInfo(FString::Printf(TEXT("Combat on: FOV %.2f -> %.2f"), S.Value, S.PCM()->GetFOVAngle()));
		S.Test->TestTrue(TEXT("Combat profile applied to the exploration camera"), S.PCM()->GetFOVAngle() > S.Value + 3.f);
		GEngine->Exec(S.World(), TEXT("ACF.Sample.Combat Off"));
		return true;
	});
	Wait(Steps, 4.0);
	Steps.Add([](FScenario& S) { S.Test->TestTrue(TEXT("Combat off: back to the exploration FOV"), FMath::IsNearlyEqual(S.PCM()->GetFOVAngle(), static_cast<float>(S.Value), 0.2f)); return true; });
	// Narrow corridor with a lock-on down the corridor.
	Exec(TEXT("ACF.Sample.Goto Corridor"));
	ExecEnemy(TEXT("ACF.Sample.LockOn %s"), TEXT("Enemy_5"));
	Wait(Steps, 2.0);
	Steps.Add([](FScenario& S)
	{
		const FVector Camera = S.PCM()->GetCameraLocation();
		S.LogCamera(TEXT("Corridor lock-on"));
		S.Test->TestTrue(TEXT("Corridor: camera between the corridor walls"), FMath::Abs(Camera.Y - 1500.0) <= 135.0 - 15.0 + 0.5);
		S.Test->TestTrue(TEXT("Corridor: camera below the low ceiling where there is one"), Camera.X < 0.0 || Camera.X > 800.0 || Camera.Z <= 250.0 - 15.0 + 0.5);
		return true;
	});
	Shot(Steps, TEXT("ACF_4B_Corridor"));
	// Collision vs Group Framing at the wall: group of the player and two enemies, back to the wall.
	Exec(TEXT("ACF.Sample.LockOff 0"));
	Exec(TEXT("ACF.Sample.Goto Wall"));
	Exec(TEXT("ACF.Sample.Group Use"));
	ExecEnemy(TEXT("ACF.Sample.Group Add %s"), TEXT("Enemy_0"));
	ExecEnemy(TEXT("ACF.Sample.Group Add %s"), TEXT("Enemy_4"));
	Wait(Steps, 2.5);
	Steps.Add([](FScenario& S)
	{
		const FACFCameraEvaluationResult R = S.Subsystem->GetLastEvaluationResult();
		S.LogCamera(TEXT("Group at the wall"));
		S.Test->TestTrue(TEXT("Group at the wall: camera in front of the wall (X <= 690 - probe)"), S.PCM()->GetCameraLocation().X <= 690.0 - 15.0 + 0.5);
		if (R.bFramingLimited)
		{
			// Defined degraded behaviour at the FOV limit: the player's centre stays inside the framing area (0.8).
			FVector2D Screen;
			SphereOnScreen(S, S.Controller->GetPawn()->GetActorLocation(), 0.f, Screen);
			S.Test->AddInfo(FString::Printf(TEXT("Group at the wall (limited): player centre at (%.3f, %.3f)"), Screen.X, Screen.Y));
			S.Test->TestTrue(TEXT("Group at the wall (limited): player centre inside the framing area"), FMath::Abs(Screen.X) <= 0.4 + 1e-3 && FMath::Abs(Screen.Y) <= 0.4 + 1e-3);
		}
		else
		{
			ExpectOnScreen(S, S.Controller->GetPawn(), 50.f, TEXT("Group at the wall"));
			ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_0")), 50.f, TEXT("Group at the wall"));
			ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_4")), 50.f, TEXT("Group at the wall"));
		}
		S.Test->AddInfo(FString::Printf(TEXT("Group at the wall: FOV %.1f, framing limited %d"), S.PCM()->GetFOVAngle(), R.bFramingLimited ? 1 : 0));
		return true;
	});
	Shot(Steps, TEXT("ACF_4B_GroupWall"));
	Exec(TEXT("ACF.Sample.Group Release 0"));
	Exec(TEXT("ACF.Sample.Release 0"));
	Exec(TEXT("ACF.Sample.Goto Start"));
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 1));
	return true;
}


// =================================================================================================
// Phase 4C: finisher / cinematic camera sequences in the real camera pipeline.

namespace ACFIntegration
{
	/** Shot Anchor (between attacker and target, line-of-action axes) + Collision. Camera at mid + Offset. */
	static UACFCameraPreset* MakeShotPreset(FScenario& S, const FVector& Offset, bool bFreeze, bool bDeoccluder = false)
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_ShotAnchor* Anchor = NewObject<UACFCameraNode_ShotAnchor>(Preset);
		Anchor->AnchorPoint = EACFCameraShotAnchorPoint::Between;
		Anchor->BetweenAlpha = 0.5f;
		Anchor->AnchorOffset = FVector::ZeroVector;
		Anchor->Basis = EACFCameraShotAnchorBasis::LineOfAction;
		Anchor->CameraOffset = Offset;
		Anchor->bFreezeAtShotStart = bFreeze;
		Anchor->DampingTime = 0.f;
		Anchor->FieldOfView = 60.f;
		UACFCameraNode_Collision* Collision = NewObject<UACFCameraNode_Collision>(Preset);
		Collision->ReturnDampingTime = 0.f;
		Collision->Deoccluder.bEnabled = bDeoccluder;
		Collision->Deoccluder.bCheckLookAtTarget = true;
		Preset->Nodes = { Anchor, Collision };
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		S.Keep.Emplace(Preset);
		return Preset;
	}

	static FACFCameraShot& AddShot(UACFCameraSequence* Sequence, FName Name, UACFCameraPreset* Preset)
	{
		FACFCameraShot& Shot = Sequence->Shots.AddDefaulted_GetRef();
		Shot.Name = Name;
		Shot.Preset = Preset;
		Shot.AdvanceMode = EACFCameraShotAdvanceMode::WaitForEvent;
		Shot.Duration = 0.f;
		Shot.Targets.Follow = EACFCameraShotRole::Attacker;
		Shot.Targets.LookAt = EACFCameraShotRole::Target;
		return Shot;
	}

	static UACFCameraSequence* NewSequence(FScenario& S)
	{
		UACFCameraSequence* Sequence = NewObject<UACFCameraSequence>(GetTransientPackage(), NAME_None, RF_Transient);
		Sequence->ReturnBlend = FACFCameraBlendSettings::MakeCut();
		S.Keep.Emplace(Sequence);
		return Sequence;
	}

	static FACFCameraSequenceState SeqState(FScenario& S)
	{
		return S.Subsystem->GetCameraSequenceState(S.Sequence);
	}

	static FVector Mid(FScenario& S)
	{
		return (S.ActorA->GetActorLocation() + S.ActorB->GetActorLocation()) * 0.5;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationFinisherAnchorTest, "ActionCameraFramework.Integration.FinisherShotAnchorAndActorLost",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationFinisherAnchorTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Pivot = TestArea + FVector(-6000, -20000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		S.ActorB = SpawnMovablePoint(S.World(), S.Pivot + FVector(300, 0, 0));
		UACFCameraSequence* Sequence = NewSequence(S);
		AddShot(Sequence, TEXT("Dynamic"), MakeShotPreset(S, FVector(0, 400, 0), false));
		AddShot(Sequence, TEXT("Fixed"), MakeShotPreset(S, FVector(0, -400, 0), true));
		S.Sequence = S.Subsystem->StartFinisherCamera(Sequence, S.ActorA.Get(), S.ActorB.Get());
		S.Test->TestTrue(TEXT("finisher handle"), S.Sequence.IsValid());
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, Mid(S) + FVector(0, 400, 0), 1.0, TEXT("Dynamic shot: right of the attacker -> target line"));
		const FRotator Look = (Mid(S) - S.PCM()->GetCameraLocation()).Rotation();
		S.Test->TestTrue(TEXT("Dynamic shot looks at the middle of the action"), S.PCM()->GetCameraRotation().Equals(Look, 0.1f));
		S.Test->TestTrue(TEXT("finisher in control"), SeqState(S).bInControl);
		S.Test->TestTrue(TEXT("Collision guard constraint active during the finisher"), S.Subsystem->GetDirector()->GetOutputConstraint().bValid);
		// Both actors move: the dynamic shot follows.
		S.ActorA->SetActorLocation(S.ActorA->GetActorLocation() + FVector(500, 200, 0));
		S.ActorB->SetActorLocation(S.ActorB->GetActorLocation() + FVector(500, 200, 0));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, Mid(S) + FVector(0, 400, 0), 1.0, TEXT("Dynamic shot followed the actors"));
		S.Subsystem->AdvanceCameraShot(S.Sequence);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Fixed shot"), SeqState(S).ShotIndex, 1);
		S.SavedLocation = S.PCM()->GetCameraLocation();
		S.SavedRotation = S.PCM()->GetCameraRotation();
		CameraCloseTo(S, Mid(S) + FVector(0, -400, 0), 1.0, TEXT("Fixed shot placed at its start"));
		S.ActorA->SetActorLocation(S.ActorA->GetActorLocation() + FVector(150, 100, 0));
		S.ActorB->SetActorLocation(S.ActorB->GetActorLocation() + FVector(150, 100, 0));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, S.SavedLocation, 0.01, TEXT("Fixed shot does not move with the actors"));
		S.Test->TestTrue(TEXT("Fixed shot rotation frozen"), S.PCM()->GetCameraRotation().Equals(S.SavedRotation, 0.01f));
		// The target dies mid-shot: the playback ends (Actor Lost Policy = Finish) and the camera returns.
		S.ActorB->Destroy();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestFalse(TEXT("target destroyed: playback ended"), S.Subsystem->IsCameraSequencePlaying(S.Sequence));
		S.Test->TestEqual(TEXT("no request left"), S.Subsystem->GetDirector()->GetNumRequests(), 0);
		S.Test->TestEqual(TEXT("no effect left"), S.Subsystem->GetDirector()->GetNumActiveEffects(), 0);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationFinisherNarrowTest, "ActionCameraFramework.Integration.FinisherNarrowFallbackAndDeoccluder",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationFinisherNarrowTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Pivot = TestArea + FVector(-6000, -26000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);
		S.ActorB = SpawnMovablePoint(S.World(), S.Pivot + FVector(300, 0, 0));
		const FVector M = Mid(S);
		// Wall 200 cm right of the action: the right-side shot (400 cm) does not fit.
		SpawnBlocker(S, M + FVector(0, 220, 0), FVector(600, 20, 400));
		// Pillar between the left-side camera and the target.
		SpawnBlocker(S, M + FVector(75, -200, 0), FVector(25, 25, 400));
		UACFCameraSequence* Sequence = NewSequence(S);
		UACFCameraPreset* Right = MakeShotPreset(S, FVector(0, 400, 0), false);
		FACFCameraShot& Fallback = AddShot(Sequence, TEXT("RightWithFallback"), Right);
		Fallback.Fallback.FallbackPreset = MakeShotPreset(S, FVector(-400, 0, 150), false);
		AddShot(Sequence, TEXT("RightSqueezed"), Right);
		FACFCameraShot& Skip = AddShot(Sequence, TEXT("RightSkipped"), Right);
		Skip.Fallback.bSkipIfInfeasible = true;
		FACFCameraShot& Deocc = AddShot(Sequence, TEXT("LeftDeoccluder"), MakeShotPreset(S, FVector(0, -400, 0), false, true));
		Deocc.bEnableDeoccluder = true;
		AddShot(Sequence, TEXT("LeftNoDeoccluder"), MakeShotPreset(S, FVector(0, -400, 0), false, true)).bEnableDeoccluder = false;
		S.Sequence = S.Subsystem->StartFinisherCamera(Sequence, S.ActorA.Get(), S.ActorB.Get());
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const FACFCameraSequenceState State = SeqState(S);
		S.Test->TestEqual(TEXT("shot 0"), State.ShotIndex, 0);
		S.Test->TestTrue(TEXT("narrow: fallback preset used"), State.bUsingFallback);
		CameraCloseTo(S, Mid(S) + FVector(-400, 0, 150), 1.0, TEXT("fallback shot (behind the attacker, high)"));
		S.Subsystem->AdvanceCameraShot(S.Sequence);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const FVector Cam = S.PCM()->GetCameraLocation();
		S.LogCamera(TEXT("squeezed shot"));
		S.Test->TestEqual(TEXT("shot 1 (no fallback configured)"), SeqState(S).ShotIndex, 1);
		S.Test->TestTrue(TEXT("squeezed shot: Collision keeps the camera in front of the wall"), Cam.Y < Mid(S).Y + 200.0 && Cam.Y > Mid(S).Y);
		S.Test->TestTrue(TEXT("guard active"), S.Subsystem->GetDirector()->GetOutputConstraint().bValid);
		S.Subsystem->AdvanceCameraShot(S.Sequence);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("infeasible shot 2 skipped -> shot 3"), SeqState(S).ShotIndex, 3);
		return true;
	});
	WaitSeconds(Steps, 2.0);
	Steps.Add([](FScenario& S)
	{
		const FACFCameraSolverReport& Report = S.Subsystem->GetDirector()->GetLastSolverReport();
		const bool bVisible = LineOfSightClear(S, S.ActorB->GetActorLocation(), S.ActorA.Get(), S.ActorB.Get());
		S.LogCamera(TEXT("deoccluder shot"));
		S.Test->AddInfo(FString::Printf(TEXT("deoccluder shot: traces %d, occluded %d, correction yaw %.1f pitch %.1f"), Report.NumTraces, Report.NumOccluded,
			Report.Correction.X, Report.Correction.Y));
		S.Test->TestTrue(TEXT("shot with Deoccluder: solver ran"), Report.NumTraces > 0);
		S.Test->TestTrue(TEXT("shot with Deoccluder: target revealed (real line of sight)"), bVisible);
		S.Subsystem->AdvanceCameraShot(S.Sequence);
		return true;
	});
	WaitSeconds(Steps, 1.0);
	Steps.Add([](FScenario& S)
	{
		const FACFCameraSolverReport& Report = S.Subsystem->GetDirector()->GetLastSolverReport();
		S.Test->TestEqual(TEXT("shot without Deoccluder: no solver traces"), Report.NumTraces, 0);
		CameraCloseTo(S, Mid(S) + FVector(0, -400, 0), 1.0, TEXT("shot without Deoccluder keeps its staged position"));
		S.Test->TestFalse(TEXT("shot without Deoccluder: the pillar hides the target"), LineOfSightClear(S, S.ActorB->GetActorLocation(), S.ActorA.Get(), S.ActorB.Get()));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFIntegrationFinisherLockOnNotifyTest, "ActionCameraFramework.Integration.FinisherFromLockOnWithAnimNotifies",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFIntegrationFinisherLockOnNotifyTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Pivot = TestArea + FVector(-6000, -32000, 0);
		S.ActorA = SpawnMovablePoint(S.World(), S.Pivot);                     // player
		S.ActorB = SpawnMovablePoint(S.World(), S.Pivot + FVector(800, 0, 0)); // locked enemy
		AActor* Victim = SpawnMovablePoint(S.World(), S.Pivot + FVector(250, 150, 0));
		S.Spawned.Add(Victim);
		USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(S.ActorA.Get(), TEXT("FinisherMesh"));
		S.Component = Mesh;
		S.Preset.Reset(MakeLockOnCollisionPreset());
		S.Handle = S.Subsystem->RequestCamera(S.Preset.Get(), UACFCameraBlueprintLibrary::MakeLockOnRequestParams(10, S.ActorA.Get(), S.ActorB.Get(), true));
		UACFCameraSequence* Sequence = NewSequence(S);
		AddShot(Sequence, TEXT("Approach"), MakeShotPreset(S, FVector(0, 400, 50), false));
		FACFCameraShot& Strike = AddShot(Sequence, TEXT("Strike"), MakeShotPreset(S, FVector(-300, -250, 80), false));
		Strike.AdvanceMode = EACFCameraShotAdvanceMode::Duration;
		Strike.Duration = 3.f;
		Sequence->ReturnBlend.Curve = EACFCameraBlendCurve::Linear;
		Sequence->ReturnBlend.Duration = 0.5f;
		S.Keep.Emplace(Sequence); // Keep.Last() = the sequence for the next step
		S.Value = 0.0;
		return true;
	});
	WaitSeconds(Steps, 0.3);
	Steps.Add([](FScenario& S)
	{
		S.SavedLocation = S.PCM()->GetCameraLocation(); // lock-on camera
		AActor* Victim = S.Spawned.Last().Get();
		S.Sequence = UACFCameraBlueprintLibrary::StartFinisherCameraForActor(S.ActorA.Get(), Cast<UACFCameraSequence>(S.Keep.Last().Get()), Victim, 1.f);
		S.Test->TestTrue(TEXT("finisher started for the attacker's local player"), S.Sequence.IsValid());
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("finisher above the lock-on"), SeqState(S).bInControl);
		S.Test->TestTrue(TEXT("lock-on kept underneath"), S.Subsystem->IsCameraRequestAlive(S.Handle));
		// Anim notify on the attacker's mesh: next shot.
		UAnimNotify_ACFCameraSequence* Notify = NewObject<UAnimNotify_ACFCameraSequence>();
		Notify->Command = EACFCameraSequenceCommand::AdvanceShot;
		Notify->Notify(Cast<USkeletalMeshComponent>(S.Component.Get()), nullptr, FAnimNotifyEventReference());
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("anim notify advanced to Strike"), SeqState(S).ShotIndex, 1);
		// Hold window (anim notify state) freezes shot time.
		UAnimNotifyState_ACFCameraHoldShot* Hold = NewObject<UAnimNotifyState_ACFCameraHoldShot>();
		Hold->NotifyBegin(Cast<USkeletalMeshComponent>(S.Component.Get()), nullptr, 1.f, FAnimNotifyEventReference());
		S.Value = SeqState(S).ShotTime;
		S.Keep.Emplace(Hold);
		return true;
	});
	WaitSeconds(Steps, 0.5);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("held by the notify state"), SeqState(S).ShotTime, static_cast<float>(S.Value));
		Cast<UAnimNotifyState_ACFCameraHoldShot>(S.Keep.Last().Get())->NotifyEnd(Cast<USkeletalMeshComponent>(S.Component.Get()), nullptr, FAnimNotifyEventReference());
		// Attacker hit-stop: half speed on the attacker -> half speed for the shot.
		S.ActorA->CustomTimeDilation = 0.5f;
		S.Value = SeqState(S).ShotTime;
		S.StartTime = S.World()->GetTimeSeconds();
		return true;
	});
	Steps.Add([](FScenario& S) { S.bRetry = S.World()->GetTimeSeconds() - S.StartTime < 1.0; return true; });
	Steps.Add([](FScenario& S)
	{
		const double World = S.World()->GetTimeSeconds() - S.StartTime;
		const double Shot = SeqState(S).ShotTime - S.Value;
		S.Test->AddInfo(FString::Printf(TEXT("attacker dilation 0.5: world %.3fs -> shot %.3fs"), World, Shot));
		S.Test->TestTrue(TEXT("shot time follows the attacker's Custom Time Dilation"), FMath::IsNearlyEqual(Shot, World * 0.5, 0.05));
		S.ActorA->CustomTimeDilation = 1.f;
		// Pause: no camera time.
		S.Value = SeqState(S).ShotTime;
		S.bFlag = UGameplayStatics::SetGamePaused(S.World(), true);
		if (!S.bFlag)
		{
			S.Test->AddWarning(TEXT("The game mode refused to pause: pause check skipped."));
		}
		return true;
	});
	Steps.Add([](FScenario& S) { return true; });
	Steps.Add([](FScenario& S)
	{
		if (S.bFlag)
		{
			S.Test->TestEqual(TEXT("paused: shot time frozen"), SeqState(S).ShotTime, static_cast<float>(S.Value));
		}
		UGameplayStatics::SetGamePaused(S.World(), false);
		// Finish from an anim notify on the target (routing by actor works for both roles).
		UAnimNotify_ACFCameraSequence* Notify = NewObject<UAnimNotify_ACFCameraSequence>();
		Notify->Command = EACFCameraSequenceCommand::Finish;
		USkeletalMeshComponent* VictimMesh = NewObject<USkeletalMeshComponent>(S.Spawned.Last().Get());
		Notify->Notify(VictimMesh, nullptr, FAnimNotifyEventReference());
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const FACFCameraEvaluationResult R = S.Subsystem->GetLastEvaluationResult();
		S.Test->TestFalse(TEXT("finished by notify"), S.Subsystem->IsCameraSequencePlaying(S.Sequence));
		S.Test->TestTrue(TEXT("back on the lock-on request"), R.ActiveRequest == S.Handle);
		S.Test->TestTrue(TEXT("with the sequence's return blend"), R.bIsBlending);
		return true;
	});
	WaitSeconds(Steps, 0.7);
	Steps.Add([](FScenario& S)
	{
		CameraCloseTo(S, S.SavedLocation, 2.0, TEXT("lock-on camera restored"));
		S.Test->TestEqual(TEXT("no sequence effect left"), S.Subsystem->GetDirector()->GetNumActiveEffects(), 0);
		S.Test->TestEqual(TEXT("notify without a playback reaches nobody"), UACFCameraBlueprintLibrary::SendCameraSequenceCommandForActor(S.ActorA.Get(), EACFCameraSequenceCommand::AdvanceShot), 0);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSampleFinisherTest, "ActionCameraFramework.Samples.FinisherOnTestMap",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACFSampleFinisherTest::RunTest(const FString& Parameters)
{
	using namespace ACFIntegration;
	// Drives the Phase 4C README commands on the real sample map (screenshots need a real RHI).
	TArray<FStep> Steps;
	auto Exec = [&Steps](const TCHAR* Command) { Steps.Add([Command](FScenario& S) { GEngine->Exec(S.World(), Command); return true; }); };
	auto ExecEnemy = [&Steps](const TCHAR* Command, const TCHAR* Label)
	{
		Steps.Add([Command, Label](FScenario& S)
		{
			TArray<AActor*> Enemies;
			UGameplayStatics::GetAllActorsWithTag(S.World(), TEXT("ACFEnemy"), Enemies);
			Enemies.Sort([](const AActor& A, const AActor& B) { return A.GetActorNameOrLabel() < B.GetActorNameOrLabel(); });
			const int32 Index = Enemies.IndexOfByPredicate([Label](const AActor* A) { return A->GetActorNameOrLabel().StartsWith(Label); });
			S.Test->TestTrue(FString::Printf(TEXT("%s exists"), Label), Index != INDEX_NONE);
			GEngine->Exec(S.World(), *FString(Command).Replace(TEXT("%s"), *FString::FromInt(Index)));
			return true;
		});
	};
	auto Current = [](FScenario& S)
	{
		TArray<FACFCameraSequenceHandle> Handles;
		S.Subsystem->GetDirector()->GetActiveSequences(Handles);
		S.Sequence = Handles.IsEmpty() ? FACFCameraSequenceHandle() : Handles[0];
		return S.Subsystem->GetCameraSequenceState(S.Sequence);
	};
	Steps.Add([](FScenario& S)
	{
		if (!S.World()->GetMapName().Contains(TEXT("L_ACFCameraTest")))
		{
			S.Test->AddWarning(TEXT("Skipped: run on /ActionCameraFramework/Samples/L_ACFCameraTest."));
			return false;
		}
		if (!FindEnemy(S, TEXT("Enemy_6")))
		{
			S.Test->AddError(TEXT("Enemy_6_Finisher missing: run Scripts/CreateManualTestAssets.py."));
			return false;
		}
		return true;
	});
	Exec(TEXT("ACF.Sample.Goto Finisher"));
	Exec(TEXT("ACF.Sample.Use ThirdPerson 0"));
	ExecEnemy(TEXT("ACF.Sample.LockOn %s"), TEXT("Enemy_6"));
	Wait(Steps, 1.5);
	Steps.Add([](FScenario& S)
	{
		S.SavedLocation = S.PCM()->GetCameraLocation();
		S.Handle = S.Subsystem->GetLastEvaluationResult().ActiveRequest; // lock-on
		return true;
	});
	Exec(TEXT("ACF.Sample.Finisher Play"));
	Exec(TEXT("ACF.Sample.Finisher Hold")); // keep "Approach" for the check / screenshot
	Wait(Steps, 0.6);
	Steps.Add([Current](FScenario& S)
	{
		const FACFCameraSequenceState State = Current(S);
		S.LogCamera(TEXT("Approach"));
		S.Test->TestEqual(TEXT("Approach: shot 0"), State.ShotIndex, 0);
		S.Test->TestFalse(TEXT("Approach: open ground, no fallback"), State.bUsingFallback);
		S.Test->TestTrue(TEXT("Approach: finisher above the lock-on"), State.bInControl && S.Subsystem->IsCameraRequestAlive(S.Handle));
		ExpectOnScreen(S, S.Controller->GetPawn(), 40.f, TEXT("Approach"));
		ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_6")), 40.f, TEXT("Approach"));
		return true;
	});
	Shot(Steps, TEXT("ACF_4C_Approach"));
	Exec(TEXT("ACF.Sample.Finisher Release"));
	Wait(Steps, 1.4); // Approach lasts 1.2 s
	Steps.Add([Current](FScenario& S)
	{
		const FACFCameraSequenceState State = Current(S);
		S.LogCamera(TEXT("Strike"));
		S.Test->TestEqual(TEXT("Strike: shot 1 after the Approach duration"), State.ShotIndex, 1);
		S.Test->TestTrue(TEXT("Strike: waits for the event"), State.bWaitingForEvent);
		S.Test->TestEqual(TEXT("Strike: owns the tension (sequence) and hit (shot) effects"), State.NumOwnedEffects, 2);
		// Reverse angle: the enemy fills the foreground (its centre must be on screen), the attacker is framed fully.
		ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_6")), 0.f, TEXT("Strike"));
		ExpectOnScreen(S, S.Controller->GetPawn(), 40.f, TEXT("Strike"));
		return true;
	});
	Shot(Steps, TEXT("ACF_4C_Strike"));
	Exec(TEXT("ACF.Sample.Finisher Notify Advance")); // what the anim notify does
	Wait(Steps, 0.9);
	Steps.Add([Current](FScenario& S)
	{
		const FACFCameraSequenceState State = Current(S);
		S.LogCamera(TEXT("Overhead"));
		S.Test->TestEqual(TEXT("Overhead: shot 2 after the notify"), State.ShotIndex, 2);
		ExpectOnScreen(S, S.Controller->GetPawn(), 40.f, TEXT("Overhead"));
		ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_6")), 40.f, TEXT("Overhead"));
		return true;
	});
	Shot(Steps, TEXT("ACF_4C_Overhead"));
	Wait(Steps, 2.2);
	Steps.Add([](FScenario& S)
	{
		const FACFCameraEvaluationResult R = S.Subsystem->GetLastEvaluationResult();
		S.Test->TestEqual(TEXT("finished: no playback"), S.Subsystem->GetDirector()->GetNumSequences(), 0);
		S.Test->TestTrue(TEXT("finished: lock-on in control again"), R.ActiveRequest == S.Handle);
		S.Test->TestEqual(TEXT("finished: no effect left"), S.Subsystem->GetDirector()->GetNumActiveEffects(), 0);
		CameraCloseTo(S, S.SavedLocation, 5.0, TEXT("finished: lock-on camera restored"));
		return true;
	});
	// Narrow place: the corridor forces the fallback shot; Cancel cuts back.
	Exec(TEXT("ACF.Sample.LockOff 0"));
	Exec(TEXT("ACF.Sample.Goto CorridorFinisher"));
	Wait(Steps, 1.0);
	ExecEnemy(TEXT("ACF.Sample.Finisher Play %s"), TEXT("Enemy_5"));
	Wait(Steps, 0.4);
	Steps.Add([Current](FScenario& S)
	{
		const FACFCameraSequenceState State = Current(S);
		const FVector Camera = S.PCM()->GetCameraLocation();
		S.LogCamera(TEXT("Corridor finisher"));
		S.Test->TestTrue(TEXT("Corridor: Approach replaced by the fallback shot"), State.ShotIndex == 0 && State.bUsingFallback);
		S.Test->TestTrue(TEXT("Corridor: camera between the corridor walls"), FMath::Abs(Camera.Y - 1500.0) <= 135.0 - 12.0 + 0.5);
		ExpectOnScreen(S, FindEnemy(S, TEXT("Enemy_5")), 40.f, TEXT("Corridor finisher"));
		return true;
	});
	Shot(Steps, TEXT("ACF_4C_CorridorFallback"));
	Exec(TEXT("ACF.Sample.Finisher Cancel"));
	Steps.Add([](FScenario& S)
	{
		const FACFCameraEvaluationResult R = S.Subsystem->GetLastEvaluationResult();
		S.Test->TestEqual(TEXT("Cancel: no playback"), S.Subsystem->GetDirector()->GetNumSequences(), 0);
		S.Test->TestEqual(TEXT("Cancel: effects cut"), S.Subsystem->GetDirector()->GetNumActiveEffects(), 0);
		S.Test->TestFalse(TEXT("Cancel: cut back (no blend)"), R.bIsBlending);
		return true;
	});
	Exec(TEXT("ACF.Sample.Release 0"));
	Exec(TEXT("ACF.Sample.Goto Start"));
	ADD_LATENT_AUTOMATION_COMMAND(FScenarioCommand(this, MoveTemp(Steps), 1));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
