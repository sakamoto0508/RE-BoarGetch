#include "ACFCameraOutputModifier.h"
#include "ACFCameraSubsystem.h"
#include "Camera/CameraTypes.h"
#include "Engine/Canvas.h"

UACFCameraOutputModifier::UACFCameraOutputModifier()
{
	// Before UCameraModifier_CameraShake (default 127) so UE shakes layer on top of ACF.
	Priority = 64;
}

void UACFCameraModifierBase::SetSubsystem(UACFCameraSubsystem* InSubsystem)
{
	Subsystem = InSubsystem;
}

UACFCameraSubsystem* UACFCameraModifierBase::GetSubsystem() const
{
	return Subsystem.Get();
}

bool UACFCameraOutputModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	// Deliberately does not call Super: the base class steps its own Alpha by DeltaTime on every
	// call, and the PCM calls modifiers twice per frame while blending view targets. ACF weight
	// comes from the director, which evaluates once per frame.
	UACFCameraSubsystem* OwnerSubsystem = Subsystem.Get();
	if (!OwnerSubsystem)
	{
		return false;
	}

	// Stage 7: output.
	const FACFCameraEvaluationResult& Result = OwnerSubsystem->EvaluateForOutput(DeltaTime, InOutPOV);
	if (!Result.HasInfluence())
	{
		return false;
	}

	const FACFCameraPose& Pose = Result.Pose;
	if (Result.Weight >= 1.f)
	{
		InOutPOV.Location = Pose.Location;
		InOutPOV.Rotation = Pose.Rotation;
		InOutPOV.FOV = Pose.FieldOfView;
	}
	else
	{
		const float W = Result.Weight;
		InOutPOV.Location = FMath::Lerp(InOutPOV.Location, Pose.Location, W);
		InOutPOV.Rotation = FQuat::Slerp(InOutPOV.Rotation.Quaternion(), Pose.Rotation.Quaternion(), W).Rotator();
		InOutPOV.FOV = FMath::Lerp(InOutPOV.FOV, Pose.FieldOfView, W);
	}

	// Let lower priority modifiers (camera shakes, ...) run after us.
	return false;
}

void UACFCameraOutputModifier::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	// Shown by "showdebug camera" (APlayerCameraManager::DisplayDebug lists modifiers).
	const UACFCameraSubsystem* OwnerSubsystem = Subsystem.Get();
	if (!Canvas || !OwnerSubsystem)
	{
		return;
	}
	const FACFCameraEvaluationResult Result = OwnerSubsystem->GetLastEvaluationResult();
	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;
	DisplayDebugManager.SetDrawColor(FColor::Cyan);
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   ACF active=%s requests=%d weight=%.3f (request %.3f, suppressed=%d) blending=%d alpha=%.3f"),
		*Result.ActiveRequest.ToString(), Result.NumRequests, Result.Weight, Result.RequestWeight, Result.bOutputSuppressed ? 1 : 0, Result.bIsBlending ? 1 : 0, Result.BlendAlpha));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   ACF pose loc=%s rot=%s fov=%.2f"),
		*Result.Pose.Location.ToCompactString(), *Result.Pose.Rotation.ToCompactString(), Result.Pose.FieldOfView));
}

// -------------------------------------------------------------------------------------------------

UACFCameraCollisionGuardModifier::UACFCameraCollisionGuardModifier()
{
	// Last of the standard modifiers (UCameraModifier_CameraShake is 127). Modifiers added later with
	// the same priority are inserted before this one (APlayerCameraManager::AddCameraModifierToList).
	Priority = 255;
}

bool UACFCameraCollisionGuardModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	if (UACFCameraSubsystem* OwnerSubsystem = Subsystem.Get())
	{
		OwnerSubsystem->ApplyOutputCollisionGuard(InOutPOV.Location);
	}
	return false;
}
