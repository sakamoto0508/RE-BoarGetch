#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "StructUtils/InstancedStruct.h"

namespace ACFNodes
{
	static FACFCameraTargetTrackingState* GetTrackingState(FInstancedStruct* RuntimeState)
	{
		return RuntimeState ? RuntimeState->GetMutablePtr<FACFCameraTargetTrackingState>() : nullptr;
	}

	static FVector ApplyOffset(const FTransform& Target, const FVector& Offset, EACFCameraOffsetSpace Space)
	{
		return Space == EACFCameraOffsetSpace::TargetLocal
			? Target.GetLocation() + Target.GetRotation().RotateVector(Offset)
			: Target.GetLocation() + Offset;
	}
}

// ---------------------------------------------------------------------------------------------
// Follow

void UACFCameraNode_Follow::OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const
{
	if (FACFCameraTargetTrackingState* State = ACFNodes::GetTrackingState(RuntimeState))
	{
		State->ResetDamping();
	}
}

void UACFCameraNode_Follow::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraTargetTrackingState* State = ACFNodes::GetTrackingState(RuntimeState);
	FTransform Target;
	if (!State || !State->ResolveTarget(Context.Request.FollowTarget, TargetLostBehavior, Target))
	{
		return;
	}
	// Scale is ignored on purpose: a scaled character should not push the camera further away.
	Target.SetScale3D(FVector::OneVector);
	const FVector Goal = ACFNodes::ApplyOffset(Target, Offset, OffsetSpace);
	InOutPose.Location = State->UpdateDamped(Goal, DampingTime, Context.GetDeltaTime());
}

// ---------------------------------------------------------------------------------------------
// LookAt

void UACFCameraNode_LookAt::OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const
{
	if (FACFCameraTargetTrackingState* State = ACFNodes::GetTrackingState(RuntimeState))
	{
		State->ResetDamping();
	}
}

void UACFCameraNode_LookAt::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraTargetTrackingState* State = ACFNodes::GetTrackingState(RuntimeState);
	const FACFCameraTarget& Source = TargetSource == EACFCameraLookAtSource::FollowTarget ? Context.Request.FollowTarget : Context.Request.LookAtTarget;
	FTransform Target;
	if (!State || !State->ResolveTarget(Source, TargetLostBehavior, Target))
	{
		return;
	}
	Target.SetScale3D(FVector::OneVector);
	const FVector AimPoint = State->UpdateDamped(ACFNodes::ApplyOffset(Target, TargetOffset, OffsetSpace), DampingTime, Context.GetDeltaTime());

	const FVector ToTarget = AimPoint - InOutPose.Location;
	if (ToTarget.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
	{
		// Camera sits on the aim point: direction is undefined, keep the incoming rotation.
		return;
	}
	InOutPose.Rotation = ToTarget.Rotation();
}
