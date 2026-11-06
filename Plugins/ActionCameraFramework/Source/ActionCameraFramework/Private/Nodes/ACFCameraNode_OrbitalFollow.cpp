#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "Nodes/ACFCameraNodeUtils.h"

float UACFCameraNode_OrbitalFollow::GetDistanceForPitch(float Pitch) const
{
	if (!bUseRings)
	{
		return Distance;
	}
	if (Pitch < 0.f && PitchMin < 0.f)
	{
		return FMath::Lerp(Distance, TopDistance, FMath::Clamp(Pitch / PitchMin, 0.f, 1.f));
	}
	if (Pitch > 0.f && PitchMax > 0.f)
	{
		return FMath::Lerp(Distance, BottomDistance, FMath::Clamp(Pitch / PitchMax, 0.f, 1.f));
	}
	return Distance;
}

void UACFCameraNode_OrbitalFollow::SetOrbitRotation(FInstancedStruct* RuntimeState, const FRotator& Rotation, bool bInstant) const
{
	if (FACFCameraOrbitState* State = ACFNodes::GetState<FACFCameraOrbitState>(RuntimeState))
	{
		State->bHasPendingRotation = true;
		State->bPendingInstant = bInstant;
		State->PendingRotation = Rotation;
	}
}

void UACFCameraNode_OrbitalFollow::OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const
{
	// Keep the orbit angles across re-activation (the player's view is preserved), but snap damping.
	ResetInterpolation(RuntimeState);
}

void UACFCameraNode_OrbitalFollow::ResetInterpolation(FInstancedStruct* RuntimeState) const
{
	if (FACFCameraOrbitState* State = ACFNodes::GetState<FACFCameraOrbitState>(RuntimeState))
	{
		State->Tracking.ResetInterpolation();
		State->bSnapRotation = true;
	}
}

void UACFCameraNode_OrbitalFollow::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraOrbitState* State = ACFNodes::GetState<FACFCameraOrbitState>(RuntimeState);
	FACFCameraTrackedTarget Target;
	const float Dt = Context.GetDeltaTime();
	if (!State || !State->Tracking.Track(Context.Request.FollowTarget, TargetMotion, Dt, Target))
	{
		return;
	}
	if (Target.bTeleported)
	{
		State->bSnapRotation = true;
	}

	const FACFCameraEvaluationContext& Eval = Context.Evaluation;
	const float TargetYaw = Target.Transform.Rotator().Yaw;
	State->LastTargetYaw = TargetYaw;
	auto ToGoalYaw = [this, TargetYaw](float AbsoluteYaw) { return bFollowTargetYaw ? FRotator::NormalizeAxis(AbsoluteYaw - TargetYaw) : AbsoluteYaw; };

	// 1. Initial rotation (first evaluation of this request).
	if (!State->bInitialized)
	{
		FRotator Initial;
		switch (InitialRotation)
		{
		case EACFCameraOrbitInitialRotation::ControlRotation:
			Initial = Eval.bHasControlRotation ? Eval.ControlRotation : InOutPose.Rotation;
			break;
		case EACFCameraOrbitInitialRotation::UpstreamCamera:
			Initial = InOutPose.Rotation;
			break;
		case EACFCameraOrbitInitialRotation::Fixed:
			Initial = FRotator(InitialPitch, InitialYaw, 0.f);
			break;
		default:
			Initial = FRotator(InitialPitch, TargetYaw + InitialYaw, 0.f);
			break;
		}
		State->GoalYaw = ToGoalYaw(Initial.Yaw);
		State->GoalPitch = FRotator::NormalizeAxis(Initial.Pitch);
		State->TimeSinceInput = 0.f;
		State->bInitialized = true;
		State->bSnapRotation = true;
	}
	bool bHadInput = false;
	if (State->bHasPendingRotation)
	{
		bHadInput = true; // an explicit rotation counts as input (no immediate idle recentering)
		State->GoalYaw = ToGoalYaw(State->PendingRotation.Yaw);
		State->GoalPitch = FRotator::NormalizeAxis(State->PendingRotation.Pitch);
		State->bSnapRotation |= State->bPendingInstant;
		State->bHasPendingRotation = false;
	}

	// 2. Input. CameraInput was consumed once by the director for this frame (never twice).
	switch (InputSource)
	{
	case EACFCameraOrbitInputSource::CameraInput:
		if (!Eval.CameraInput.IsNearlyZero())
		{
			State->GoalYaw += Eval.CameraInput.X * Sensitivity.X * (bInvertYaw ? -1.f : 1.f);
			State->GoalPitch += Eval.CameraInput.Y * Sensitivity.Y * (bInvertPitch ? -1.f : 1.f);
			bHadInput = true;
		}
		break;
	case EACFCameraOrbitInputSource::ControlRotation:
		if (Eval.bHasControlRotation)
		{
			const float NewYaw = ToGoalYaw(Eval.ControlRotation.Yaw);
			const float NewPitch = FRotator::NormalizeAxis(Eval.ControlRotation.Pitch);
			bHadInput = !FMath::IsNearlyEqual(FRotator::NormalizeAxis(NewYaw - State->GoalYaw), 0.f, 0.01f)
				|| !FMath::IsNearlyEqual(NewPitch, State->GoalPitch, 0.01f);
			State->GoalYaw = NewYaw;
			State->GoalPitch = NewPitch;
		}
		break;
	default:
		break;
	}
	if (Dt > 0.f)
	{
		State->TimeSinceInput = bHadInput ? 0.f : State->TimeSinceInput + Dt;
	}

	// 3. Idle recentering (behind the target), only while there is no input.
	if (bRecenterWhenIdle && InputSource != EACFCameraOrbitInputSource::ControlRotation && State->TimeSinceInput >= RecenterDelay && Dt > 0.f)
	{
		const float Alpha = ACFCameraMath::ExpAlpha(RecenterTime, Dt);
		const float HomeYaw = ToGoalYaw(TargetYaw + InitialYaw);
		State->GoalYaw += FRotator::NormalizeAxis(HomeYaw - State->GoalYaw) * Alpha;
		if (bRecenterPitch)
		{
			State->GoalPitch += (InitialPitch - State->GoalPitch) * Alpha;
		}
	}

	const float MinPitch = FMath::Min(PitchMin, PitchMax);
	const float MaxPitch = FMath::Max(PitchMin, PitchMax);
	State->GoalPitch = FMath::Clamp(State->GoalPitch, MinPitch, MaxPitch);
	State->GoalYaw = FRotator::NormalizeAxis(State->GoalYaw);
	const float AbsoluteGoalYaw = bFollowTargetYaw ? TargetYaw + State->GoalYaw : State->GoalYaw;

	// 4. Rotation damping, per axis, along the shortest path.
	if (State->bSnapRotation)
	{
		State->Yaw = AbsoluteGoalYaw;
		State->Pitch = State->GoalPitch;
		State->bSnapRotation = false;
	}
	else
	{
		State->Yaw += FRotator::NormalizeAxis(AbsoluteGoalYaw - State->Yaw) * ACFCameraMath::ExpAlpha(RotationDamping.X, Dt);
		State->Pitch += (State->GoalPitch - State->Pitch) * ACFCameraMath::ExpAlpha(RotationDamping.Y, Dt);
		if (RotationDamping.X <= UE_KINDA_SMALL_NUMBER) { State->Yaw = AbsoluteGoalYaw; }
		if (RotationDamping.Y <= UE_KINDA_SMALL_NUMBER) { State->Pitch = State->GoalPitch; }
	}
	State->Yaw = FRotator::NormalizeAxis(State->Yaw);
	const FRotator OrbitRotation(FMath::Clamp(State->Pitch, MinPitch, MaxPitch), State->Yaw, 0.f);

	// 5. Pivot (lookahead + per-axis damping) and arm.
	const FQuat TargetRotation = Target.Transform.GetRotation();
	const FVector PivotGoal = ACFNodes::ApplyOffset(Target.Predicted, TargetRotation, PivotOffset, PivotOffsetSpace);
	const FQuat Frame = FACFCameraTargetTrackingState::DampingFrame(PivotDamping, TargetRotation, OrbitRotation);
	const FVector Pivot = State->Tracking.UpdateDamped(PivotGoal, PivotDamping.Time, Frame, Dt);
	const float ArmLength = GetDistanceForPitch(OrbitRotation.Pitch);

	InOutPose.Location = Pivot - OrbitRotation.Vector() * ArmLength + OrbitRotation.RotateVector(ShoulderOffset);
	if (bControlRotation)
	{
		InOutPose.Rotation = OrbitRotation;
	}

	// Collision sweeps from the real (undamped, non-predicted) pivot, like USpringArmComponent.
	Context.Scratch.bHasPivot = true;
	Context.Scratch.Pivot = ACFNodes::ApplyOffset(Target.Location, TargetRotation, PivotOffset, PivotOffsetSpace);
	Context.Scratch.PivotActor = Target.Actor;
#if !UE_BUILD_SHIPPING
	FACFCameraRigDebug& Debug = Context.Scratch.Debug;
	Debug.bHasFollowTarget = true;
	Debug.FollowTarget = Target.Location;
	Debug.bHasPrediction |= TargetMotion.Lookahead.Time > 0.f;
	Debug.PredictedTarget = Target.Predicted;
	Debug.bHasOrbit = true;
	Debug.OrbitYaw = OrbitRotation.Yaw;
	Debug.OrbitPitch = OrbitRotation.Pitch;
	Debug.OrbitDistance = ArmLength;
#endif
}
