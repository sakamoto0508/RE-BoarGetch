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
#include "Engine/CollisionProfile.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
