#pragma once

#include "CoreMinimal.h"
#include "ACFCameraTypes.h"
#include "ACFCameraTargetTracking.generated.h"

/** What a target-driven node does once its assigned target has been destroyed. */
UENUM(BlueprintType)
enum class EACFCameraTargetLostBehavior : uint8
{
	/** Keep using the last known target transform (camera settles there; no jump). */
	HoldLastKnown,
	/** Stop affecting the pose; the previous node / UE camera value shows through (may jump). */
	PassThrough,
};

/** Space in which an offset relative to a target is expressed. */
UENUM(BlueprintType)
enum class EACFCameraOffsetSpace : uint8
{
	/** Offset is added in world axes (target rotation is ignored). */
	World,
	/** Offset is rotated by the target's rotation (e.g. "behind and above the character"). */
	TargetLocal,
};

/**
 * Per-request runtime state shared by target-driven nodes (Follow, LookAt).
 * One instance per node per request; never stored on the shared node itself.
 */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraTargetTrackingState
{
	GENERATED_BODY()

	/** Last transform of the target while it was alive (used by HoldLastKnown). */
	FTransform LastKnownTargetTransform = FTransform::Identity;
	bool bHasLastKnownTarget = false;

	/** Identity of the target last resolved; a different object resets damping (snap). */
	TWeakObjectPtr<const UObject> LastTargetObject;

	/** Damped output point and the goal it was chasing last frame. */
	FVector DampedPoint = FVector::ZeroVector;
	FVector PreviousGoal = FVector::ZeroVector;
	bool bDampingInitialized = false;

	/** Forces the next update to snap (activation, target switch). */
	void ResetDamping() { bDampingInitialized = false; }

	/**
	 * Resolves Target, applying LostBehavior. Returns false when the node should leave the pose
	 * untouched (no target assigned, or lost with PassThrough / never seen alive).
	 */
	bool ResolveTarget(const FACFCameraTarget& Target, EACFCameraTargetLostBehavior LostBehavior, FTransform& OutTransform);

	/** Advances damping towards Goal and returns the damped point. DeltaTime <= 0 never changes state. */
	FVector UpdateDamped(const FVector& Goal, float DampingTime, float DeltaTime);
};

namespace ACFCameraMath
{
	/**
	 * First-order lag ("time constant" DampingTime seconds) towards a goal that is assumed to move
	 * linearly between the previous and the current sample. This is the exact solution of
	 * dx/dt = (goal(t) - x) / DampingTime for piecewise-linear goal motion, so the result does not
	 * depend on the frame rate (a constant-velocity target always trails by Velocity * DampingTime).
	 */
	ACTIONCAMERAFRAMEWORK_API FVector DampTowardsMovingGoal(const FVector& Current, const FVector& PreviousGoal, const FVector& Goal, float DampingTime, float DeltaTime);
}
