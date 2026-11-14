#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "Nodes/ACFCameraNodeUtils.h"
#include "ACFCameraHealth.h"

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
		State->ResetAssistance(); // 7B-1: activation / teleport restarts the waits (no stale movement or timers)
	}
}

void UACFCameraNode_OrbitalFollow::UpdateMovementAssistance(const FACFCameraNodeContext& Context, FACFCameraOrbitState& State, float TargetYaw, bool bHadInput, bool bExplicitRotation) const
{
	const FACFCameraEvaluationContext& Eval = Context.Evaluation;
	const float Dt = Context.GetDeltaTime();

	// Movement: the tracked target's smoothed velocity (position differences of this node's own tracking state, so root
	// motion, moving bases and teleports are handled like the lookahead; never shared with another node / request).
	// Only the part perpendicular to world up counts: jumps and slopes do not change the heading.
	FVector Velocity = State.Tracking.SmoothedVelocity;
	if (!ACFCameraValidation::IsFinite(Velocity))
	{
		Velocity = FVector::ZeroVector; // B-4 policy: nothing invalid reaches the state
		State.bMoving = false;
		State.MovementTime = 0.f;
	}
	const FVector Horizontal = FVector::VectorPlaneProject(Velocity, FVector::UpVector);
	const float Speed = static_cast<float>(Horizontal.Size());
	State.MovementSpeed = Speed;
	const float Start = FMath::IsFinite(StartSpeedThreshold) ? FMath::Max(StartSpeedThreshold, 0.f) : 0.f;
	const float Stop = FMath::IsFinite(StopSpeedThreshold) ? FMath::Clamp(StopSpeedThreshold, 0.f, Start) : Start;
	const bool bAboveZero = Speed > UE_KINDA_SMALL_NUMBER;
	State.bMoving = bAboveZero && Speed >= (State.bMoving ? Stop : Start);
	State.MovementTime = State.bMoving ? State.MovementTime + Dt : 0.f;
	State.bHasHeading = bAboveZero;
	if (bAboveZero)
	{
		State.HeadingYaw = FMath::RadiansToDegrees(FMath::Atan2(Horizontal.Y, Horizontal.X));
	}

	// Blockers, highest priority first. A blocker restarts the wait (Recenter Delay counts from its end).
	EACFCameraAssistanceState NewState = EACFCameraAssistanceState::Suppressed;
	EACFCameraAssistanceReason Reason = EACFCameraAssistanceReason::None;
	const bool bValid = FMath::IsFinite(RecenterDelay) && FMath::IsFinite(RecenterTime) && FMath::IsFinite(RequiredMovementDuration)
		&& FMath::IsFinite(BackwardExclusionAngle) && FMath::IsFinite(AngularDeadZone) && FMath::IsFinite(MaxAssistanceYawSpeed)
		&& FMath::IsFinite(StartSpeedThreshold) && FMath::IsFinite(StopSpeedThreshold) && MaxAssistanceYawSpeed > 0.f;
	if (InputSource == EACFCameraOrbitInputSource::ControlRotation)
	{
		NewState = EACFCameraAssistanceState::Disabled;
		Reason = EACFCameraAssistanceReason::ControlRotationInput;
	}
	else if (RecenterHeading == EACFCameraRecenterHeading::MovementDirection && bFollowTargetYaw)
	{
		NewState = EACFCameraAssistanceState::Disabled;
		Reason = EACFCameraAssistanceReason::FollowTargetYaw;
	}
	else if (!bValid)
	{
		NewState = EACFCameraAssistanceState::Disabled;
		Reason = EACFCameraAssistanceReason::InvalidSettings;
	}
	else if (bExplicitRotation)
	{
		Reason = EACFCameraAssistanceReason::ExplicitRotation;
	}
	else if (bHadInput)
	{
		Reason = EACFCameraAssistanceReason::ManualInput;
	}
	else if (Eval.bAssistanceSuppressed)
	{
		Reason = EACFCameraAssistanceReason::SuppressedBySource;
	}
	else if (Eval.bOutputBypassed || Eval.bOutputSuppressed)
	{
		Reason = EACFCameraAssistanceReason::ViewNotShown;
	}
	else if (!Context.bInControl)
	{
		Reason = EACFCameraAssistanceReason::NotInControl;
	}
	if (Reason != EACFCameraAssistanceReason::None)
	{
		State.AssistIdleTime = 0.f;
		State.AssistanceState = NewState;
		State.AssistanceReason = Reason;
		return;
	}

	// Waits: Recenter Delay and Required Movement Duration run at the same time (the longer one decides).
	State.AssistIdleTime += Dt;
	State.AssistanceState = EACFCameraAssistanceState::Waiting;
	if (State.AssistIdleTime < RecenterDelay)
	{
		State.AssistanceReason = EACFCameraAssistanceReason::RecenterDelay;
		return;
	}
	if (!State.bMoving)
	{
		State.AssistanceReason = EACFCameraAssistanceReason::BelowSpeed;
		return;
	}
	if (State.MovementTime < RequiredMovementDuration)
	{
		State.AssistanceReason = EACFCameraAssistanceReason::MovementDuration;
		return;
	}

	// Heading (absolute yaw) and the camera that is on screen (the damped yaw of the previous evaluation).
	const float HeadingYaw = (RecenterHeading == EACFCameraRecenterHeading::MovementDirection ? State.HeadingYaw : TargetYaw) + InitialYaw;
	const float CameraYaw = State.bSnapRotation ? (bFollowTargetYaw ? TargetYaw + State.GoalYaw : State.GoalYaw) : State.Yaw;
	const float ToCamera = FMath::Abs(FRotator::NormalizeAxis(HeadingYaw - CameraYaw));
	if (ToCamera > 180.f - FMath::Clamp(BackwardExclusionAngle, 0.f, 180.f))
	{
		State.AssistanceReason = EACFCameraAssistanceReason::MovingTowardCamera;
		return;
	}

	// The recenter step of Phase 2 (exponential, Recenter Time) on the shortest angle beyond the dead zone, limited to
	// Max Assistance Yaw Speed. The step never exceeds the remaining difference, so a hitch cannot overshoot or reverse.
	State.AssistanceState = EACFCameraAssistanceState::Assisting;
	const float HomeYaw = bFollowTargetYaw ? FRotator::NormalizeAxis(HeadingYaw - TargetYaw) : HeadingYaw;
	const float Difference = FRotator::NormalizeAxis(HomeYaw - State.GoalYaw);
	const float Beyond = FMath::Max(FMath::Abs(Difference) - FMath::Clamp(AngularDeadZone, 0.f, 180.f), 0.f);
	const float Alpha = ACFCameraMath::ExpAlpha(RecenterTime, Dt);
	if (Beyond <= 0.f)
	{
		State.AssistanceReason = EACFCameraAssistanceReason::Aligned;
	}
	else
	{
		State.AssistanceReason = EACFCameraAssistanceReason::None;
		const float Step = FMath::Min(Beyond * Alpha, MaxAssistanceYawSpeed * Dt);
		State.GoalYaw += FMath::Sign(Difference) * Step;
	}
	if (bRecenterPitch)
	{
		State.GoalPitch += (InitialPitch - State.GoalPitch) * Alpha;
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
		State->ResetAssistance(); // 7B-1: the jump is not movement (its velocity was dropped with the tracking history)
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
	const bool bExplicitRotation = State->bHasPendingRotation;
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

	// 3. Idle recentering (behind the target), only while there is no input. Phase 7B-1: with Movement Assistance the same
	// recenter runs under the assistance rules instead (UpdateMovementAssistance); without it this block is unchanged.
	if (bEnableMovementAssistance)
	{
		if (Dt > 0.f) // zero-time evaluations (pause, InitializeFor) never advance the waits
		{
			UpdateMovementAssistance(Context, *State, TargetYaw, bHadInput, bExplicitRotation);
		}
	}
	else if (bRecenterWhenIdle && InputSource != EACFCameraOrbitInputSource::ControlRotation && State->TimeSinceInput >= RecenterDelay && Dt > 0.f)
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
	if (bEnableMovementAssistance || bRecenterWhenIdle)
	{
		Debug.bHasAssistance = true;
		Debug.bAssistanceEnabled = bEnableMovementAssistance;
		Debug.bAssistanceSuppressedBySource = Context.Evaluation.bAssistanceSuppressed;
		Debug.AssistanceHeading = bEnableMovementAssistance ? RecenterHeading : EACFCameraRecenterHeading::TargetFacing;
		Debug.AssistanceState = bEnableMovementAssistance ? State->AssistanceState : EACFCameraAssistanceState::Disabled;
		Debug.AssistanceReason = bEnableMovementAssistance ? State->AssistanceReason : EACFCameraAssistanceReason::NotEnabled;
		Debug.bHasAssistanceTargetYaw = Debug.AssistanceHeading == EACFCameraRecenterHeading::TargetFacing || State->bHasHeading;
		Debug.AssistanceTargetYaw = FRotator::NormalizeAxis((Debug.AssistanceHeading == EACFCameraRecenterHeading::MovementDirection ? State->HeadingYaw : TargetYaw) + InitialYaw);
		Debug.AssistanceMovementSpeed = State->MovementSpeed;
		Debug.AssistanceWaitTime = bEnableMovementAssistance ? State->AssistIdleTime : State->TimeSinceInput;
		Debug.AssistanceRecenterDelay = RecenterDelay;
		Debug.AssistanceMovementTime = State->MovementTime;
		Debug.AssistanceRequiredMovementTime = RequiredMovementDuration;
	}
#endif
}
