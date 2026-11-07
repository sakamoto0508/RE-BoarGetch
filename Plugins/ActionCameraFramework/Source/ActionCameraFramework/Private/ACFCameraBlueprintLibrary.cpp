#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Actor.h"

UACFCameraSubsystem* UACFCameraBlueprintLibrary::GetCameraSubsystem(const APlayerController* PlayerController)
{
	const ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UACFCameraSubsystem>() : nullptr;
}

FACFCameraRequestHandle UACFCameraBlueprintLibrary::RequestCamera(APlayerController* PlayerController, UACFCameraPreset* Preset, const FACFCameraRequestParams& Params)
{
	if (UACFCameraSubsystem* Subsystem = GetCameraSubsystem(PlayerController))
	{
		return Subsystem->RequestCamera(Preset, Params);
	}
	UE_LOG(LogActionCamera, Warning, TEXT("RequestCamera: %s is not a local player controller."), *GetNameSafe(PlayerController));
	return FACFCameraRequestHandle();
}

bool UACFCameraBlueprintLibrary::ReleaseCamera(APlayerController* PlayerController, FACFCameraRequestHandle& Handle)
{
	if (UACFCameraSubsystem* Subsystem = GetCameraSubsystem(PlayerController))
	{
		return Subsystem->ReleaseCamera(Handle);
	}
	Handle.Invalidate();
	return false;
}

FACFCameraRequestParams UACFCameraBlueprintLibrary::MakeRequestParams(int32 Priority, UObject* Instigator)
{
	FACFCameraRequestParams Params;
	Params.Priority = Priority;
	Params.Instigator = Instigator;
	return Params;
}

FACFCameraRequestParams UACFCameraBlueprintLibrary::MakeTargetRequestParams(int32 Priority, AActor* FollowTarget, AActor* LookAtTarget, UObject* Instigator)
{
	FACFCameraRequestParams Params = MakeRequestParams(Priority, Instigator);
	Params.FollowTarget.Actor = FollowTarget;
	Params.LookAtTarget.Actor = LookAtTarget;
	return Params;
}

FACFCameraRequestParams UACFCameraBlueprintLibrary::MakeLockOnRequestParams(int32 Priority, AActor* Player, AActor* LockTarget, bool bReleaseWhenTargetLost)
{
	FACFCameraRequestParams Params = MakeTargetRequestParams(Priority, Player, LockTarget, nullptr);
	Params.bReleaseWhenLookAtTargetLost = bReleaseWhenTargetLost;
	Params.DebugName = TEXT("LockOn");
	return Params;
}

FACFCameraTargetGroupMember UACFCameraBlueprintLibrary::MakeTargetGroupMember(AActor* Actor, float Weight, float Radius, FVector Offset)
{
	FACFCameraTargetGroupMember Member;
	Member.Target.Actor = Actor;
	Member.Weight = FMath::Max(Weight, 0.f);
	Member.Radius = FMath::Max(Radius, 0.f);
	Member.Offset = Offset;
	return Member;
}
