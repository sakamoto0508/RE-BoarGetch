#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

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

// Phase 6B / B-7: return blend entries. The subsystem of the given player checks the conditions and delegates to the
// director's existing return blend (BlendFromPose / RemoveRequest with an override); nothing is interpolated here.

bool UACFCameraBlueprintLibrary::BlendCameraFromPoseForPlayer(APlayerController* PlayerController, const FACFCameraPose& Pose, float BlendTime, EACFCameraBlendCurve Curve, EACFCameraReturnBlendResult& Result)
{
	UACFCameraSubsystem* Subsystem = GetCameraSubsystem(PlayerController);
	Result = Subsystem ? Subsystem->TryBlendFromPose(Pose, BlendTime, Curve) : EACFCameraReturnBlendResult::NoPlayer;
	return Result == EACFCameraReturnBlendResult::Started || Result == EACFCameraReturnBlendResult::Cut;
}

bool UACFCameraBlueprintLibrary::BlendCameraFromCurrentViewForPlayer(APlayerController* PlayerController, float BlendTime, EACFCameraBlendCurve Curve, EACFCameraReturnBlendResult& Result)
{
	UACFCameraSubsystem* Subsystem = GetCameraSubsystem(PlayerController);
	Result = Subsystem ? Subsystem->TryBlendFromCurrentView(BlendTime, Curve) : EACFCameraReturnBlendResult::NoPlayer;
	return Result == EACFCameraReturnBlendResult::Started || Result == EACFCameraReturnBlendResult::Cut;
}

bool UACFCameraBlueprintLibrary::ReleaseCameraWithReturnBlendForPlayer(APlayerController* PlayerController, FACFCameraRequestHandle& Handle, float BlendTime, EACFCameraBlendCurve Curve)
{
	UACFCameraSubsystem* Subsystem = GetCameraSubsystem(PlayerController);
	if (!Subsystem)
	{
		Handle.Invalidate();
		return false;
	}
	FACFCameraBlendSettings Blend;
	Blend.Curve = Curve;
	Blend.Duration = BlendTime; // NaN / Inf / negative behave as a cut (and are reported, B-4)
	return Subsystem->ReleaseCameraWithReturnBlend(Handle, Blend);
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

UACFCameraSubsystem* UACFCameraBlueprintLibrary::GetCameraSubsystemForActor(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	// The pawn itself or the pawn owning the actor (weapon, projectile...).
	for (const AActor* It = Actor; It; It = It->GetOwner())
	{
		if (const APawn* Pawn = Cast<APawn>(It))
		{
			if (UACFCameraSubsystem* Subsystem = GetCameraSubsystem(Cast<APlayerController>(Pawn->GetController())))
			{
				return Subsystem;
			}
		}
		if (UACFCameraSubsystem* Subsystem = GetCameraSubsystem(Cast<APlayerController>(It)))
		{
			return Subsystem;
		}
	}
	const UWorld* World = Actor->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const ULocalPlayer* First = GameInstance ? GameInstance->GetFirstGamePlayer() : nullptr;
	return First ? First->GetSubsystem<UACFCameraSubsystem>() : nullptr;
}

FACFCameraSequenceHandle UACFCameraBlueprintLibrary::StartFinisherCameraForActor(AActor* Attacker, UACFCameraSequence* Sequence, AActor* Target, float PlayRate)
{
	if (UACFCameraSubsystem* Subsystem = GetCameraSubsystemForActor(Attacker))
	{
		return Subsystem->StartFinisherCamera(Sequence, Attacker, Target, PlayRate);
	}
	return FACFCameraSequenceHandle();
}

int32 UACFCameraBlueprintLibrary::SendCameraSequenceCommandForActor(AActor* Actor, EACFCameraSequenceCommand Command, FName ShotName)
{
	const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World && World->IsGameWorld() ? World->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		return 0; // editor animation preview, server without local players
	}
	int32 Count = 0;
	for (const ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
	{
		if (UACFCameraSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UACFCameraSubsystem>() : nullptr)
		{
			Count += Subsystem->SendCameraSequenceCommandToActor(Actor, Command, ShotName);
		}
	}
	return Count;
}
