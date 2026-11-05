#include "ACFCameraSubsystem.h"
#include "ACFCameraDirector.h"
#include "ACFCameraOutputModifier.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSettings.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

void UACFCameraSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Director = NewObject<UACFCameraDirector>(this, TEXT("ACFCameraDirector"));
	OutputModifierClass = GetDefault<UACFCameraSettings>()->ResolveOutputModifierClass();
	WorldTickStartHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ThisClass::HandleWorldTickStart);
}

void UACFCameraSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldTickStart.Remove(WorldTickStartHandle);
	WorldTickStartHandle.Reset();
	DetachOutput();
	if (Director)
	{
		Director->RemoveAllRequests(true);
	}
	Super::Deinitialize();
}

void UACFCameraSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);

	// PCM is spawned in APlayerController::PostInitializeComponents, which runs before the
	// controller is handed to the local player, so it is available here.
	if (Director && Director->IsInfluencingOutput())
	{
		EnsureOutputAttached();
	}
}

void UACFCameraSubsystem::HandleWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (!Director || !Director->IsInfluencingOutput())
	{
		return;
	}
	const APlayerCameraManager* CameraManager = GetCurrentCameraManager();
	if (!CameraManager || CameraManager->GetWorld() != World)
	{
		return; // another world (other PIE instance, editor world) or no camera yet
	}
	EnsureOutputAttached();
}

FACFCameraRequestHandle UACFCameraSubsystem::RequestCamera(UACFCameraPreset* Preset, const FACFCameraRequestParams& Params)
{
	if (!Director)
	{
		return FACFCameraRequestHandle();
	}
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset, Params);
	if (Handle.IsValid())
	{
		EnsureOutputAttached();
	}
	return Handle;
}

bool UACFCameraSubsystem::ReleaseCamera(FACFCameraRequestHandle& Handle)
{
	const bool bRemoved = Director && Director->RemoveRequest(Handle);
	Handle.Invalidate();
	return bRemoved;
}

bool UACFCameraSubsystem::ReleaseCameraWithBlend(FACFCameraRequestHandle& Handle, const FACFCameraBlendSettings& BlendOut)
{
	const bool bRemoved = Director && Director->RemoveRequest(Handle, &BlendOut);
	Handle.Invalidate();
	return bRemoved;
}

void UACFCameraSubsystem::ReleaseAllCameras(bool bImmediate)
{
	if (Director)
	{
		Director->RemoveAllRequests(bImmediate);
	}
}

bool UACFCameraSubsystem::IsCameraRequestAlive(FACFCameraRequestHandle Handle) const
{
	return Director && Director->HasRequest(Handle);
}

bool UACFCameraSubsystem::IsActiveCamera(FACFCameraRequestHandle Handle) const
{
	return Director && Handle.IsValid() && Director->GetActiveRequest() == Handle;
}

FACFCameraEvaluationResult UACFCameraSubsystem::GetLastEvaluationResult() const
{
	return Director ? Director->GetLastResult() : FACFCameraEvaluationResult();
}

bool UACFCameraSubsystem::SetCameraTargets(FACFCameraRequestHandle Handle, const FACFCameraTarget& FollowTarget, const FACFCameraTarget& LookAtTarget)
{
	return Director && Director->SetRequestTargets(Handle, FollowTarget, LookAtTarget);
}

void UACFCameraSubsystem::SuppressOutput(UObject* Source, float BlendTime)
{
	if (Director)
	{
		Director->SuppressOutput(Source, BlendTime);
	}
}

bool UACFCameraSubsystem::ReleaseOutputSuppression(UObject* Source, float BlendTime)
{
	return Director && Director->ReleaseOutputSuppression(Source, BlendTime);
}

bool UACFCameraSubsystem::IsOutputSuppressed() const
{
	return Director && Director->IsOutputSuppressed();
}

FACFCameraPose UACFCameraSubsystem::GetAimCameraPose() const
{
	if (Director && Director->GetLastResult().FrameNumber != 0)
	{
		return Director->GetLastResult().AimPose;
	}
	// ACF has never evaluated (no request yet): the gameplay camera is the visual UE camera.
	return GetVisualCameraPose();
}

FACFCameraPose UACFCameraSubsystem::GetVisualCameraPose() const
{
	FACFCameraPose Pose;
	if (const APlayerCameraManager* CameraManager = GetCurrentCameraManager())
	{
		const FMinimalViewInfo& View = CameraManager->GetCameraCacheView();
		Pose.Location = View.Location;
		Pose.Rotation = View.Rotation;
		Pose.FieldOfView = View.FOV;
	}
	return Pose;
}

const FACFCameraEvaluationResult& UACFCameraSubsystem::EvaluateForOutput(float DeltaTime, const FMinimalViewInfo& UpstreamPOV)
{
	check(Director);

	// Stage 1: context collection.
	FACFCameraEvaluationContext Context;
	Context.DeltaTime = DeltaTime;
	Context.FrameNumber = GFrameCounter;
	if (const ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		Context.PlayerController = LocalPlayer->PlayerController;
	}
	Context.UpstreamPose.Location = UpstreamPOV.Location;
	Context.UpstreamPose.Rotation = UpstreamPOV.Rotation;
	Context.UpstreamPose.FieldOfView = UpstreamPOV.FOV;

	// Stages 2-6.
	return Director->Evaluate(Context);
}

APlayerCameraManager* UACFCameraSubsystem::GetCurrentCameraManager() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const APlayerController* PlayerController = LocalPlayer ? LocalPlayer->PlayerController.Get() : nullptr;
	return PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr;
}

void UACFCameraSubsystem::EnsureOutputAttached()
{
	APlayerCameraManager* CameraManager = GetCurrentCameraManager();
	if (!CameraManager || !IsValid(CameraManager))
	{
		// Retried from PlayerControllerChanged / the next world tick once a camera manager exists.
		UE_LOG(LogActionCamera, Verbose, TEXT("No PlayerCameraManager yet for %s; output attach deferred."), *GetNameSafe(GetLocalPlayer()));
		return;
	}
	if (!OutputModifierClass)
	{
		OutputModifierClass = GetDefault<UACFCameraSettings>()->ResolveOutputModifierClass();
	}

	if (UACFCameraOutputModifier* Existing = OutputModifier.Get())
	{
		// Fast path: still in the current PCM's list (someone may have removed it).
		if (Existing->GetOwningCameraManager() == CameraManager && CameraManager->FindCameraModifierByClass(Existing->GetClass()) == Existing)
		{
			return;
		}
		if (APlayerCameraManager* OldManager = Existing->GetOwningCameraManager(); IsValid(OldManager) && OldManager != CameraManager)
		{
			OldManager->RemoveCameraModifier(Existing);
		}
		Existing->SetSubsystem(nullptr);
		OutputModifier.Reset();
	}

	UACFCameraOutputModifier* Modifier = Cast<UACFCameraOutputModifier>(CameraManager->FindCameraModifierByClass(OutputModifierClass));
	if (Modifier && Modifier->GetSubsystem() && Modifier->GetSubsystem() != this)
	{
		Modifier = nullptr;
	}
	if (!Modifier)
	{
		Modifier = Cast<UACFCameraOutputModifier>(CameraManager->AddNewCameraModifier(OutputModifierClass));
	}
	if (!Modifier)
	{
		UE_LOG(LogActionCamera, Error, TEXT("Failed to add %s to %s (exclusive priority conflict?). ACF output disabled."),
			*GetNameSafe(OutputModifierClass), *GetNameSafe(CameraManager));
		return;
	}

	Modifier->SetSubsystem(this);
	OutputModifier = Modifier;
	UE_LOG(LogActionCamera, Log, TEXT("Output modifier %s attached to %s (priority %d)."),
		*Modifier->GetName(), *CameraManager->GetName(), Modifier->Priority);
}

void UACFCameraSubsystem::DetachOutput()
{
	if (UACFCameraOutputModifier* Modifier = OutputModifier.Get())
	{
		Modifier->SetSubsystem(nullptr);
		if (APlayerCameraManager* CameraManager = Modifier->GetOwningCameraManager(); IsValid(CameraManager))
		{
			CameraManager->RemoveCameraModifier(Modifier);
		}
	}
	OutputModifier.Reset();
}
