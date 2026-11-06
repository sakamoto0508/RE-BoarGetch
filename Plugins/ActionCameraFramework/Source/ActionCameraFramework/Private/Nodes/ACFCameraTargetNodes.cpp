#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "Nodes/ACFCameraNodeUtils.h"

// ---------------------------------------------------------------------------------------------
// Follow

void UACFCameraNode_Follow::ResetInterpolation(FInstancedStruct* RuntimeState) const
{
	if (FACFCameraTargetTrackingState* State = ACFNodes::GetState<FACFCameraTargetTrackingState>(RuntimeState))
	{
		State->ResetInterpolation();
	}
}

void UACFCameraNode_Follow::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraTargetTrackingState* State = ACFNodes::GetState<FACFCameraTargetTrackingState>(RuntimeState);
	FACFCameraTrackedTarget Target;
	if (!State || !State->Track(Context.Request.FollowTarget, TargetMotion, Context.GetDeltaTime(), Target))
	{
		return;
	}
	const FQuat TargetRotation = Target.Transform.GetRotation();
	const FVector Goal = ACFNodes::ApplyOffset(Target.Predicted, TargetRotation, Offset, OffsetSpace);
	const FQuat Frame = FACFCameraTargetTrackingState::DampingFrame(Damping, TargetRotation, InOutPose.Rotation);
	InOutPose.Location = State->UpdateDamped(Goal, Damping.Time, Frame, Context.GetDeltaTime());

	// The camera is attached to the real target position (collision sweeps from there).
	Context.Scratch.bHasPivot = true;
	Context.Scratch.Pivot = Target.Location;
	Context.Scratch.PivotActor = Target.Actor;
#if !UE_BUILD_SHIPPING
	Context.Scratch.Debug.bHasFollowTarget = true;
	Context.Scratch.Debug.FollowTarget = Target.Location;
	Context.Scratch.Debug.bHasPrediction |= TargetMotion.Lookahead.Time > 0.f;
	Context.Scratch.Debug.PredictedTarget = Target.Predicted;
#endif
}

// ---------------------------------------------------------------------------------------------
// LookAt

void UACFCameraNode_LookAt::ResetInterpolation(FInstancedStruct* RuntimeState) const
{
	if (FACFCameraTargetTrackingState* State = ACFNodes::GetState<FACFCameraTargetTrackingState>(RuntimeState))
	{
		State->ResetInterpolation();
	}
}

void UACFCameraNode_LookAt::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraTargetTrackingState* State = ACFNodes::GetState<FACFCameraTargetTrackingState>(RuntimeState);
	const FACFCameraTarget& Source = TargetSource == EACFCameraLookAtSource::FollowTarget ? Context.Request.FollowTarget : Context.Request.LookAtTarget;
	FACFCameraTrackedTarget Target;
	if (!State || !State->Track(Source, TargetMotion, Context.GetDeltaTime(), Target))
	{
		return;
	}
	const FVector Goal = ACFNodes::ApplyOffset(Target.Predicted, Target.Transform.GetRotation(), TargetOffset, OffsetSpace);
	const FVector AimPoint = State->UpdateDamped(Goal, DampingTime, Context.GetDeltaTime());
#if !UE_BUILD_SHIPPING
	Context.Scratch.Debug.bHasLookAtTarget = true;
	Context.Scratch.Debug.LookAtTarget = AimPoint;
#endif

	const FVector ToTarget = AimPoint - InOutPose.Location;
	if (ToTarget.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
	{
		// Camera sits on the aim point: direction is undefined, keep the incoming rotation.
		return;
	}
	InOutPose.Rotation = ToTarget.Rotation();
}
