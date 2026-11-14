#include "ACFCameraOutputModifier.h"
#include "ACFCameraSubsystem.h"
#include "ACFCameraHealth.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
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

AActor* UACFCameraModifierBase::GetViewTargetBeingUpdated(const FMinimalViewInfo& POV) const
{
	if (CameraOwner)
	{
		if (&POV == &CameraOwner->PendingViewTarget.POV)
		{
			return CameraOwner->PendingViewTarget.Target;
		}
		if (&POV == &CameraOwner->ViewTarget.POV)
		{
			return CameraOwner->ViewTarget.Target;
		}
	}
	return nullptr;
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

	// Phase 5A: 外部 ViewTarget（Sequencer のカメラなど）には何も書かない。評価もしない:
	// 同じフレームにゲームプレイ側の ViewTarget が更新されればそこで、されなければカメラ更新後の裏評価で 1 回だけ進む。
	// Phase 6B / B-2: POV がコピーの PCM でも、PCM の状態から ViewTarget を判定する（判定できない場合は外部カメラを守る）。
	if (!OwnerSubsystem->BeginOutputForModifierPass(CameraOwner, InOutPOV, DeltaTime))
	{
		return false;
	}

	// Stage 7: output.
	const FACFCameraEvaluationResult& Result = OwnerSubsystem->EvaluateForOutput(DeltaTime, InOutPOV);
	// B-4: the director guarantees a valid pose whenever it has influence; checked once more where ACF writes into UE.
	if (!Result.HasInfluence() || !ACFCameraValidation::IsValidPose(Result.Pose))
	{
		return false;
	}

	OwnerSubsystem->NoteOutputWritten();
	const FACFCameraPose& Pose = Result.Pose;
	const bool bUpstreamValid = ACFCameraValidation::IsFinite(InOutPOV.Location) && ACFCameraValidation::IsFinite(InOutPOV.Rotation)
		&& ACFCameraValidation::IsValidFov(InOutPOV.FOV);
	if (!bUpstreamValid && Result.Weight < 1.f)
	{
		// The pose UE gave ACF for this (non-external) view target is invalid: a partial blend would carry it to the screen.
		OwnerSubsystem->NoteInvalidOutputUpstream();
	}
	if (Result.Weight >= 1.f || !bUpstreamValid)
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
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   ACF active=%s requests=%d weight=%.3f (request %.3f, suppressed=%d, bypassed=%d) blending=%d alpha=%.3f"),
		*Result.ActiveRequest.ToString(), Result.NumRequests, Result.Weight, Result.RequestWeight, Result.bOutputSuppressed ? 1 : 0, Result.bOutputBypassed ? 1 : 0, Result.bIsBlending ? 1 : 0, Result.BlendAlpha));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   ACF pose loc=%s rot=%s fov=%.2f"),
		*Result.Pose.Location.ToCompactString(), *Result.Pose.Rotation.ToCompactString(), Result.Pose.FieldOfView));
	const FACFCameraOutputDiagnostics Diagnostics = OwnerSubsystem->GetOutputDiagnostics();
	DisplayDebugManager.SetDrawColor(Diagnostics.UnresolvedPasses > 0 ? FColor::Orange : FColor::Cyan);
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   ACF output: view target by %s, passes %d, writes %d, POV copied %d, unresolved passes %d"),
		*StaticEnum<EACFViewTargetResolution>()->GetNameStringByValue(static_cast<int64>(Diagnostics.LastResolution)), Diagnostics.Passes, Diagnostics.Writes,
		Diagnostics.bPOVCopied ? 1 : 0, Diagnostics.UnresolvedPasses));
	const FACFCameraHealthDiagnostics Health = OwnerSubsystem->GetHealthDiagnostics();
	if (Health.TotalIssues > 0)
	{
		DisplayDebugManager.SetDrawColor(FColor::Orange);
		DisplayDebugManager.DrawString(FString::Printf(TEXT("   ACF health: %d issues (targets %d, delta %d, upstream %d, inputs %d, nodes %d, effects %d, final %d). Last: %s"),
			Health.TotalIssues, Health.InvalidTargets, Health.InvalidDeltaTimes, Health.InvalidUpstreamPoses, Health.RejectedInputs, Health.InvalidNodeOutputs,
			Health.InvalidEffectOutputs, Health.InvalidFinalPoses, *Health.LastIssue));
	}
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
	UACFCameraSubsystem* OwnerSubsystem = Subsystem.Get();
	if (OwnerSubsystem && OwnerSubsystem->ShouldGuardModifierPass(CameraOwner, InOutPOV))
	{
		OwnerSubsystem->ApplyOutputCollisionGuard(InOutPOV.Location);
	}
	return false;
}
