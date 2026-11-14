#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "Nodes/ACFCameraTargetTracking.h"
#include "Nodes/ACFCameraTargetGroup.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "ACFCameraNode.h"

/** Small helpers shared by the built-in nodes. */
namespace ACFNodes
{
	/** Typed access to a node's runtime state; null if missing or of another type. */
	template <typename T>
	T* GetState(FInstancedStruct* RuntimeState)
	{
		return RuntimeState ? RuntimeState->GetMutablePtr<T>() : nullptr;
	}

	/** Tracks the target an Aim node aims at (Look At target, Follow target or Target Group centre). */
	inline bool TrackAimSource(FACFCameraTargetTrackingState& State, const FACFCameraNodeContext& Context, EACFCameraLookAtSource Source,
		const FACFCameraTargetMotionSettings& Motion, FACFCameraTrackedTarget& Out)
	{
		if (Source == EACFCameraLookAtSource::TargetGroup)
		{
			FVector Center;
			const bool bValid = ACFTargetGroup::ComputeGroupCenter(Context.Request.TargetGroup, Center);
			return State.TrackPoint(bValid, FTransform(Center), Motion, Context.GetDeltaTime(), false, Out);
		}
		const FACFCameraTarget& Target = Source == EACFCameraLookAtSource::FollowTarget ? Context.Request.FollowTarget : Context.Request.LookAtTarget;
		return State.Track(Target, Motion, Context.GetDeltaTime(), Out);
	}

	inline FVector ApplyOffset(const FVector& Base, const FQuat& TargetRotation, const FVector& Offset, EACFCameraOffsetSpace Space)
	{
		return Space == EACFCameraOffsetSpace::TargetLocal ? Base + TargetRotation.RotateVector(Offset) : Base + Offset;
	}
}
