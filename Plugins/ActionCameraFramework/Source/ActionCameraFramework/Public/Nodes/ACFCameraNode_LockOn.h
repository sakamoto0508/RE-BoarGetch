#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "Nodes/ACFCameraTargetTracking.h"
#include "ACFCameraNode_LockOn.generated.h"

/** Per-request runtime state of a Lock-On node. */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraLockOnState
{
	GENERATED_BODY()

	/** Player (Follow Target): loss, teleport detection, lookahead, pivot damping. */
	FACFCameraTargetTrackingState Tracking;

	/** Smoothed lock point (target switches and target jumps glide instead of cutting). */
	FVector LockPoint = FVector::ZeroVector;
	FVector PreviousLockGoal = FVector::ZeroVector;
	FVector LastKnownLockGoal = FVector::ZeroVector;
	bool bHasLockGoal = false;
	bool bLockPointInitialized = false;

	float GoalYaw = 0.f;
	float Yaw = 0.f;
	float Pitch = 0.f;
	float ArmLength = 0.f;
	float FieldOfView = 0.f;
	bool bInitialized = false;
	/** Next evaluation snaps angles / distance (activation, player teleport). */
	bool bSnap = true;
};

/**
 * Body (+ default rotation): lock-on camera for the player (Follow Target) and a locked target (Look At Target).
 *
 *  - The camera orbits the player's pivot on the side away from the target: orbit yaw = horizontal direction
 *    player -> target, orbit pitch = BasePitch + target elevation * ElevationPitchScale (clamped).
 *  - It aims at the focus point Lerp(player pivot, target, FocusBias), placed at FocusScreenPosition, so the
 *    player and the target are both in front of the camera.
 *  - Arm length (and optionally FOV) depend on the player-target distance (Near / Far Range).
 *  - Add a Group Framing node (Follow + Look At subjects) to guarantee that both stay inside the frame and a
 *    Collision node for walls.
 *
 * Responsibilities: the node only frames. Choosing, prioritising and switching targets is gameplay: call
 * UACFCameraSubsystem::SetCameraLockOnTarget (the camera then turns smoothly to the new target).
 * Starting / ending the lock-on is a normal camera request blend (request this preset with a higher priority
 * than the exploration camera, release it to blend back).
 *
 * Safety: target too close horizontally -> yaw is held (no spinning when the target overlaps the player).
 * Target destroyed -> the last known lock point is held (use bReleaseWhenLookAtTargetLost to blend back
 * automatically). No target at all -> behind the player. Player teleport -> snap; target jumps -> glide.
 */
UCLASS(meta = (DisplayName = "Lock-On"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_LockOn : public UACFCameraNode
{
	GENERATED_BODY()

public:
	// --- Player ------------------------------------------------------------------------------
	/** Orbit centre relative to the player (Follow Target). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player")
	FVector PivotOffset = FVector(0.f, 0.f, 60.f);

	/** Per-axis pivot damping (CameraYaw space recommended). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player")
	FACFCameraPositionDamping PivotDamping = FACFCameraPositionDamping::Uniform(0.1f, EACFCameraDampingSpace::CameraYaw);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player")
	FACFCameraTargetMotionSettings TargetMotion;

	// --- Lock target -------------------------------------------------------------------------
	/** Lock point relative to the locked target (world space, e.g. chest height). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lock Target")
	FVector LockTargetOffset = FVector::ZeroVector;

	/** Time constant of the lock point. Switching targets and target teleports glide with it (0 = instant). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lock Target", meta = (ClampMin = "0.0", UIMax = "2.0", Units = s))
	float LockTargetDampingTime = 0.25f;

	/** Below this horizontal distance between player and target the orbit yaw is held (direction undefined). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lock Target", meta = (ClampMin = "1.0", Units = cm))
	float MinHorizontalDistance = 80.f;

	// --- Orbit -------------------------------------------------------------------------------
	/** Offset in orbit space after the arm (Y = shoulder side, Z = up), keeps the player off the target's line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit")
	FVector ShoulderOffset = FVector(0.f, 50.f, 30.f);

	/** Orbit pitch with the target at the player's height (negative = camera above, looking down). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (ClampMin = "-89.0", ClampMax = "89.0", Units = deg))
	float BasePitch = -12.f;

	/** How much the target's elevation angle tilts the orbit (1 = fully, 0 = never; a tall boss lowers the camera). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ElevationPitchScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (ClampMin = "-89.0", ClampMax = "89.0", Units = deg))
	float PitchMin = -55.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (ClampMin = "-89.0", ClampMax = "89.0", Units = deg))
	float PitchMax = 30.f;

	/** Rotation damping time constants (X = yaw, Y = pitch). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (ClampMin = "0.0", UIMax = "1.0"))
	FVector2D RotationDamping = FVector2D(0.2f, 0.25f);

	// --- Distance by range -------------------------------------------------------------------
	/** Player-target distance at which the Near values apply. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range", meta = (ClampMin = "0.0", Units = cm))
	float NearRange = 200.f;

	/** Player-target distance at which the Far values apply (linear in between). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range", meta = (ClampMin = "0.0", Units = cm))
	float FarRange = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range", meta = (ClampMin = "0.0", Units = cm))
	float NearArmLength = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range", meta = (ClampMin = "0.0", Units = cm))
	float FarArmLength = 350.f;

	/** Also change the field of view with the range (otherwise the incoming FOV is kept). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range", meta = (InlineEditConditionToggle))
	bool bAdjustFieldOfView = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range", meta = (EditCondition = "bAdjustFieldOfView", ClampMin = "5.0", ClampMax = "170.0", Units = deg))
	float NearFieldOfView = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range", meta = (EditCondition = "bAdjustFieldOfView", ClampMin = "5.0", ClampMax = "170.0", Units = deg))
	float FarFieldOfView = 75.f;

	/** Time constant for arm length / FOV changes (keeps the range from pumping the camera). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range", meta = (ClampMin = "0.0", UIMax = "2.0", Units = s))
	float RangeDampingTime = 0.5f;

	// --- Aim ---------------------------------------------------------------------------------
	/** Write the aim rotation (disable when an Aim node such as Rotation Composer owns the rotation). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim")
	bool bAimAtFocus = true;

	/** Focus point between the player pivot (0) and the lock point (1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (EditCondition = "bAimAtFocus", ClampMin = "0.0", ClampMax = "1.0"))
	float FocusBias = 0.6f;

	/** Where the focus point appears on screen ((0, 0) = centre, see Screen Composition). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (EditCondition = "bAimAtFocus", UIMin = "-0.5", UIMax = "0.5"))
	FVector2D FocusScreenPosition = FVector2D(0.f, 0.05f);

	/** Arm length / FOV for a player-target distance (before damping). */
	void GetRangeValues(float Range, float IncomingFov, float& OutArmLength, float& OutFov) const;

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::Body; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraLockOnState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override { ResetInterpolation(RuntimeState); }
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};
