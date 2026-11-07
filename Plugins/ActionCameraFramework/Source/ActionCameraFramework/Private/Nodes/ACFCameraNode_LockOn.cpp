#include "Nodes/ACFCameraNode_LockOn.h"
#include "Nodes/ACFCameraFraming.h"
#include "Nodes/ACFCameraNodeUtils.h"

void UACFCameraNode_LockOn::GetRangeValues(float Range, float IncomingFov, float& OutArmLength, float& OutFov) const
{
	float Alpha;
	if (FarRange > NearRange)
	{
		Alpha = FMath::Clamp((Range - NearRange) / (FarRange - NearRange), 0.f, 1.f);
	}
	else
	{
		Alpha = Range >= FarRange ? 1.f : 0.f;
	}
	OutArmLength = FMath::Lerp(NearArmLength, FarArmLength, Alpha);
	OutFov = bAdjustFieldOfView ? FMath::Lerp(NearFieldOfView, FarFieldOfView, Alpha) : IncomingFov;
}

void UACFCameraNode_LockOn::ResetInterpolation(FInstancedStruct* RuntimeState) const
{
	if (FACFCameraLockOnState* State = ACFNodes::GetState<FACFCameraLockOnState>(RuntimeState))
	{
		State->Tracking.ResetInterpolation();
		State->bLockPointInitialized = false;
		State->bSnap = true;
	}
}

void UACFCameraNode_LockOn::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraLockOnState* State = ACFNodes::GetState<FACFCameraLockOnState>(RuntimeState);
	FACFCameraTrackedTarget Player;
	const float Dt = Context.GetDeltaTime();
	if (!State || !State->Tracking.Track(Context.Request.FollowTarget, TargetMotion, Dt, Player))
	{
		return; // no player: leave the pose to the previous node / UE camera
	}
	if (Player.bTeleported)
	{
		State->bSnap = true;
		State->bLockPointInitialized = false;
	}

	// 1. Lock point: live target, else the last known point while a (destroyed) target is still assigned.
	const FACFCameraTarget& LockTarget = Context.Request.LookAtTarget;
	FTransform LockTransform;
	const UObject* LockObject = nullptr;
	const bool bLockAlive = LockTarget.Resolve(LockTransform, LockObject);
	if (bLockAlive)
	{
		State->LastKnownLockGoal = LockTransform.GetLocation() + LockTargetOffset;
		State->bHasLockGoal = true;
	}
	const bool bHasLock = State->bHasLockGoal && LockTarget.IsSet();
	if (!bHasLock)
	{
		State->bLockPointInitialized = false; // next acquisition starts on the target, not from a stale point
	}
	else if (!State->bLockPointInitialized)
	{
		State->LockPoint = State->PreviousLockGoal = State->LastKnownLockGoal;
		State->bLockPointInitialized = true;
	}
	else if (Dt > 0.f)
	{
		// Same exact first-order lag as all ACF damping: a target switch or jump glides, frame-rate independent.
		State->LockPoint = ACFCameraMath::DampTowardsMovingGoal(State->LockPoint, State->PreviousLockGoal, State->LastKnownLockGoal, LockTargetDampingTime, Dt);
		State->PreviousLockGoal = State->LastKnownLockGoal;
	}

	// 2. Player pivot (lookahead + per-axis damping in the camera's yaw frame).
	const FQuat PlayerRotation = Player.Transform.GetRotation();
	const float PlayerYaw = Player.Transform.Rotator().Yaw;
	const FVector PivotGoal = Player.Predicted + PivotOffset;
	const FQuat Frame = FACFCameraTargetTrackingState::DampingFrame(PivotDamping, PlayerRotation, FRotator(0.f, State->Yaw, 0.f));
	const FVector Pivot = State->Tracking.UpdateDamped(PivotGoal, PivotDamping.Time, Frame, Dt);

	// 3. Orbit goals from the player -> target direction.
	float GoalPitch = BasePitch;
	float Range = NearRange;
	if (bHasLock)
	{
		const FVector ToLock = State->LockPoint - Pivot;
		const double Horizontal = FVector2D(ToLock.X, ToLock.Y).Size();
		if (Horizontal >= MinHorizontalDistance)
		{
			State->GoalYaw = FMath::RadiansToDegrees(FMath::Atan2(ToLock.Y, ToLock.X));
		}
		else if (!State->bInitialized)
		{
			State->GoalYaw = PlayerYaw; // target on top of the player: start behind the player, then hold
		}
		const double Elevation = FMath::RadiansToDegrees(FMath::Atan2(ToLock.Z, FMath::Max(Horizontal, static_cast<double>(MinHorizontalDistance))));
		GoalPitch = BasePitch + static_cast<float>(Elevation) * ElevationPitchScale;
		Range = ToLock.Size();
	}
	else
	{
		State->GoalYaw = PlayerYaw; // nothing locked: stay behind the player
	}
	GoalPitch = FMath::Clamp(GoalPitch, FMath::Min(PitchMin, PitchMax), FMath::Max(PitchMin, PitchMax));
	float GoalArm = 0.f;
	float GoalFov = 0.f;
	GetRangeValues(Range, InOutPose.FieldOfView, GoalArm, GoalFov);

	// 4. Damping (shortest yaw path). Activation / teleport snaps; the request blend smooths activation.
	if (State->bSnap || !State->bInitialized)
	{
		State->Yaw = State->GoalYaw;
		State->Pitch = GoalPitch;
		State->ArmLength = GoalArm;
		State->FieldOfView = GoalFov;
		State->bSnap = false;
		State->bInitialized = true;
	}
	else
	{
		State->Yaw = FRotator::NormalizeAxis(State->Yaw + FRotator::NormalizeAxis(State->GoalYaw - State->Yaw) * ACFCameraMath::ExpAlpha(RotationDamping.X, Dt));
		State->Pitch += (GoalPitch - State->Pitch) * ACFCameraMath::ExpAlpha(RotationDamping.Y, Dt);
		State->ArmLength += (GoalArm - State->ArmLength) * ACFCameraMath::ExpAlpha(RangeDampingTime, Dt);
		State->FieldOfView += (GoalFov - State->FieldOfView) * ACFCameraMath::ExpAlpha(RangeDampingTime, Dt);
	}

	// 5. Pose.
	const FRotator OrbitRotation(State->Pitch, State->Yaw, 0.f);
	const FVector Camera = Pivot - OrbitRotation.Vector() * State->ArmLength + OrbitRotation.RotateVector(ShoulderOffset);
	InOutPose.Location = Camera;
	if (bAdjustFieldOfView)
	{
		InOutPose.FieldOfView = State->FieldOfView;
	}
	FVector Focus = Pivot;
	if (bAimAtFocus && bHasLock)
	{
		Focus = FMath::Lerp(Pivot, State->LockPoint, static_cast<double>(FocusBias));
		const ACFFraming::FLens Lens = ACFFraming::MakeLens(InOutPose.FieldOfView, Context.Evaluation.AspectRatio);
		InOutPose.Rotation = ACFFraming::AimAt(OrbitRotation, Camera, Lens, Focus, FocusScreenPosition);
	}
	else
	{
		InOutPose.Rotation = OrbitRotation;
	}

	if (bHasLock)
	{
		Context.Scratch.bHasLockOnTarget = true;
		Context.Scratch.LockOnTargetLocation = State->LockPoint - LockTargetOffset;
	}
	// Collision sweeps from the real (undamped) player pivot.
	Context.Scratch.bHasPivot = true;
	Context.Scratch.Pivot = Player.Location + PivotOffset;
	Context.Scratch.PivotActor = Player.Actor;
#if !UE_BUILD_SHIPPING
	FACFCameraRigDebug& Debug = Context.Scratch.Debug;
	Debug.bHasFollowTarget = true;
	Debug.FollowTarget = Player.Location;
	Debug.bHasOrbit = true;
	Debug.OrbitYaw = OrbitRotation.Yaw;
	Debug.OrbitPitch = OrbitRotation.Pitch;
	Debug.OrbitDistance = State->ArmLength;
	Debug.bHasLockOn = bHasLock;
	Debug.bLockOnTargetLost = bHasLock && !bLockAlive;
	Debug.LockOnPoint = State->LockPoint;
	Debug.LockOnFocus = Focus;
	Debug.LockOnRange = Range;
#endif
}
