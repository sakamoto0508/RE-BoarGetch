#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "Nodes/ACFCameraTargetTracking.h"

/** Small helpers shared by the built-in nodes. */
namespace ACFNodes
{
	/** Typed access to a node's runtime state; null if missing or of another type. */
	template <typename T>
	T* GetState(FInstancedStruct* RuntimeState)
	{
		return RuntimeState ? RuntimeState->GetMutablePtr<T>() : nullptr;
	}

	inline FVector ApplyOffset(const FVector& Base, const FQuat& TargetRotation, const FVector& Offset, EACFCameraOffsetSpace Space)
	{
		return Space == EACFCameraOffsetSpace::TargetLocal ? Base + TargetRotation.RotateVector(Offset) : Base + Offset;
	}
}
