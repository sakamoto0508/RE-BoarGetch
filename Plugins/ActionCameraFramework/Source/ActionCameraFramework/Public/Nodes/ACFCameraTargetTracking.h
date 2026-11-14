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

/** Axes in which per-axis position damping is applied. X = forward, Y = right, Z = up of that space. */
UENUM(BlueprintType)
enum class EACFCameraDampingSpace : uint8
{
	World,
	/** Target rotation (e.g. less damping sideways than forward for a character). */
	TargetLocal,
	/** Camera yaw (forward = camera look direction projected on the ground). */
	CameraYaw,
};

/** Per-axis position damping (time constants in seconds; 0 = rigid on that axis). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraPositionDamping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damping", meta = (ClampMin = "0.0", UIMax = "2.0"))
	FVector Time = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damping")
	EACFCameraDampingSpace Space = EACFCameraDampingSpace::World;

	bool IsRigid() const { return Time.GetMax() <= UE_KINDA_SMALL_NUMBER; }
	static FACFCameraPositionDamping Uniform(float Seconds, EACFCameraDampingSpace InSpace = EACFCameraDampingSpace::World)
	{
		FACFCameraPositionDamping D;
		D.Time = FVector(Seconds);
		D.Space = InSpace;
		return D;
	}
};

/** Predicts where the target is heading so the camera can lead it. */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraLookaheadSettings
{
	GENERATED_BODY()

	/** Seconds ahead to predict (0 = off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lookahead", meta = (ClampMin = "0.0", UIMax = "1.0", Units = s))
	float Time = 0.f;

	/** Time constant used to smooth the measured velocity (larger = calmer, slower to react). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lookahead", meta = (ClampMin = "0.0", UIMax = "1.0", Units = s))
	float Smoothing = 0.25f;

	/**
	 * Smoothing multiplier while the target moves against the predicted direction (sharp turn,
	 * reversal). Lower values drop the stale prediction faster so the camera does not keep
	 * running ahead in the old direction.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lookahead", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float DirectionChangeSmoothingScale = 0.5f;

	/** Upper bound of the predicted offset (0 = unlimited). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lookahead", meta = (ClampMin = "0.0", Units = cm))
	float MaxDistance = 300.f;

	/** Ignore vertical motion (jumps should not make the camera bob ahead). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lookahead")
	bool bIgnoreVertical = true;
};

/** How a node treats its target over time: loss, teleports and prediction. */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraTargetMotionSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	EACFCameraTargetLostBehavior LostBehavior = EACFCameraTargetLostBehavior::HoldLastKnown;

	/**
	 * A jump larger than this between two evaluations is treated as a teleport: damping and
	 * lookahead history are dropped so the camera cuts instead of flying across the level
	 * (0 = never). Call UACFCameraSubsystem::NotifyTeleport for teleports you know about.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (ClampMin = "0.0", Units = cm))
	float TeleportDistance = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	FACFCameraLookaheadSettings Lookahead;
};

/** Result of tracking a target for one evaluation. */
struct FACFCameraTrackedTarget
{
	FTransform Transform = FTransform::Identity;
	/** Where the target actually is. */
	FVector Location = FVector::ZeroVector;
	/** Location + lookahead (== Location when lookahead is off). Composition uses this. */
	FVector Predicted = FVector::ZeroVector;
	bool bTeleported = false;
	const AActor* Actor = nullptr;
};

/**
 * Per-request runtime state shared by target-driven nodes (Follow, LookAt, Orbital Follow,
 * composers). One instance per node per request; never stored on the shared node itself.
 */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraTargetTrackingState
{
	GENERATED_BODY()

	FTransform LastKnownTargetTransform = FTransform::Identity;
	bool bHasLastKnownTarget = false;
	TWeakObjectPtr<const UObject> LastTargetObject;

	// Damping
	FVector DampedPoint = FVector::ZeroVector;
	FVector PreviousGoal = FVector::ZeroVector;
	bool bDampingInitialized = false;

	// Lookahead / teleport detection
	FVector PreviousTargetLocation = FVector::ZeroVector;
	FVector SmoothedVelocity = FVector::ZeroVector;
	bool bMotionInitialized = false;

	/** Forces the next update to snap (activation, target switch, teleport). */
	void ResetInterpolation()
	{
		bDampingInitialized = false;
		bMotionInitialized = false;
		SmoothedVelocity = FVector::ZeroVector;
	}

	/**
	 * Resolves Target, applying loss / teleport / lookahead. Returns false when the node should
	 * leave the pose untouched (no target assigned, or lost with PassThrough / never seen alive).
	 */
	bool Track(const FACFCameraTarget& Target, const FACFCameraTargetMotionSettings& Settings, float DeltaTime, FACFCameraTrackedTarget& Out);

	/**
	 * Same motion handling (lookahead, optional teleport detection) for a computed point that is not a
	 * single target, e.g. a target group centre. bValid false = hold the last known point (false if none).
	 * Group centres jump when members are added / removed, so callers usually pass bDetectTeleport = false.
	 */
	bool TrackPoint(bool bValid, const FTransform& Transform, const FACFCameraTargetMotionSettings& Settings, float DeltaTime, bool bDetectTeleport, FACFCameraTrackedTarget& Out);

	/** Uniform damping towards Goal. DeltaTime <= 0 never changes state. */
	FVector UpdateDamped(const FVector& Goal, float DampingTime, float DeltaTime);

	/** Per-axis damping in Frame's axes. DeltaTime <= 0 never changes state. */
	FVector UpdateDamped(const FVector& Goal, const FVector& AxisTimes, const FQuat& Frame, float DeltaTime);

	/** Resolves the damping frame for Damping.Space. */
	static FQuat DampingFrame(const FACFCameraPositionDamping& Damping, const FQuat& TargetRotation, const FRotator& CameraRotation);

private:
	void UpdateMotion(const FACFCameraTargetMotionSettings& Settings, float DeltaTime, bool bDetectTeleport, FACFCameraTrackedTarget& Out);
	bool ResolveTarget(const FACFCameraTarget& Target, EACFCameraTargetLostBehavior LostBehavior, FTransform& OutTransform, const UObject*& OutObject);
};

namespace ACFCameraMath
{
	/**
	 * First-order lag ("time constant" DampingTime seconds) towards a goal that is assumed to move
	 * linearly between the previous and the current sample. This is the exact solution of
	 * dx/dt = (goal(t) - x) / DampingTime for piecewise-linear goal motion, so the result does not
	 * depend on the frame rate (a constant-velocity target always trails by Velocity * DampingTime)
	 * and stays stable for any DeltaTime spike.
	 */
	ACTIONCAMERAFRAMEWORK_API FVector DampTowardsMovingGoal(const FVector& Current, const FVector& PreviousGoal, const FVector& Goal, float DampingTime, float DeltaTime);

	/** Same model, one axis. */
	ACTIONCAMERAFRAMEWORK_API double DampTowardsMovingGoal1D(double Current, double PreviousGoal, double Goal, float DampingTime, float DeltaTime);

	/** Exponential approach to a fixed goal: 1 - exp(-dt/T) (1 when T <= 0, 0 when dt <= 0). */
	ACTIONCAMERAFRAMEWORK_API float ExpAlpha(float DampingTime, float DeltaTime);
}
