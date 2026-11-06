#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "Nodes/ACFCameraTargetTracking.h"
#include "ACFCameraNode_OrbitalFollow.generated.h"

/** Where an Orbital Follow node takes its yaw / pitch from. */
UENUM(BlueprintType)
enum class EACFCameraOrbitInputSource : uint8
{
	/** ACF's own camera input (UACFCameraSubsystem::AddCameraInput / SetCameraInputRate). Engine-input agnostic. */
	CameraInput,
	/** The PlayerController's control rotation (behaves like a SpringArm with bUsePawnControlRotation). */
	ControlRotation,
	/** No input: the orbit stays at its initial rotation (plus recentering / target yaw following). */
	None,
};

/** Starting orbit rotation when the camera is activated. */
UENUM(BlueprintType)
enum class EACFCameraOrbitInitialRotation : uint8
{
	/** Behind the follow target: target yaw + InitialYaw, InitialPitch. */
	BehindTarget,
	/** The PlayerController's control rotation. */
	ControlRotation,
	/** Continue from the camera that was on screen (no visible rotation jump on activation). */
	UpstreamCamera,
	/** World yaw InitialYaw, pitch InitialPitch. */
	Fixed,
};

/** Per-request runtime state of an Orbital Follow node. */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraOrbitState
{
	GENERATED_BODY()

	FACFCameraTargetTrackingState Tracking;

	/** Goal orbit angles. Yaw is relative to the target heading when bFollowTargetYaw. */
	float GoalYaw = 0.f;
	float GoalPitch = 0.f;
	/** Damped absolute orbit angles actually used. */
	float Yaw = 0.f;
	float Pitch = 0.f;
	float LastTargetYaw = 0.f;
	float TimeSinceInput = 0.f;
	bool bInitialized = false;
	/** Next evaluation snaps the damped angles to the goal (activation, teleport). */
	bool bSnapRotation = true;

	/** Pending SetOrbitRotation request (applied on the next evaluation, when the target yaw is known). */
	bool bHasPendingRotation = false;
	bool bPendingInstant = false;
	FRotator PendingRotation = FRotator::ZeroRotator;
};

/**
 * Body (+ rotation): Third-person / FreeLook orbit around the follow target.
 *
 *   pivot  = target + PivotOffset                 (damped per axis, lookahead applied)
 *   camera = pivot - Rotation.Vector() * Distance + Rotation.RotateVector(ShoulderOffset)
 *
 * Same geometry as USpringArmComponent (TargetOffset = PivotOffset in World space,
 * TargetArmLength = Distance, SocketOffset = ShoulderOffset). Use the Collision node for the
 * equivalent of the spring arm probe. With rings enabled the distance depends on pitch
 * (Cinemachine FreeLook's top / middle / bottom rigs).
 */
UCLASS(meta = (DisplayName = "Orbital Follow"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_OrbitalFollow : public UACFCameraNode
{
	GENERATED_BODY()

public:
	// --- Pivot ---------------------------------------------------------------------------------
	/** Orbit centre relative to the follow target (e.g. head height). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pivot")
	FVector PivotOffset = FVector(0.f, 0.f, 60.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pivot")
	EACFCameraOffsetSpace PivotOffsetSpace = EACFCameraOffsetSpace::World;

	/** Per-axis pivot damping (CameraYaw space: X = towards the view, Y = sideways, Z = vertical). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pivot")
	FACFCameraPositionDamping PivotDamping = FACFCameraPositionDamping::Uniform(0.f, EACFCameraDampingSpace::CameraYaw);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pivot")
	FACFCameraTargetMotionSettings TargetMotion;

	// --- Orbit ---------------------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (ClampMin = "0.0", Units = cm))
	float Distance = 400.f;

	/** Offset in orbit space after the arm (X back/forward, Y = shoulder side, Z = up). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit")
	FVector ShoulderOffset = FVector::ZeroVector;

	/** Pitch limits in degrees (negative = camera above the target looking down). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (ClampMin = "-89.0", ClampMax = "89.0", Units = deg))
	float PitchMin = -70.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (ClampMin = "-89.0", ClampMax = "89.0", Units = deg))
	float PitchMax = 60.f;

	/** FreeLook rings: Distance applies at pitch 0, TopDistance at PitchMin, BottomDistance at PitchMax. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (InlineEditConditionToggle))
	bool bUseRings = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (EditCondition = "bUseRings", ClampMin = "0.0", Units = cm))
	float TopDistance = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (EditCondition = "bUseRings", ClampMin = "0.0", Units = cm))
	float BottomDistance = 250.f;

	/** If true the orbit yaw is relative to the target heading: turning the actor turns the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit")
	bool bFollowTargetYaw = false;

	/** Write the orbit rotation to the camera (disable when an Aim node fully owns rotation). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit")
	bool bControlRotation = true;

	/** Rotation damping time constants (X = yaw, Y = pitch). Applied to every rotation change. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (ClampMin = "0.0", UIMax = "1.0"))
	FVector2D RotationDamping = FVector2D::ZeroVector;

	// --- Input ---------------------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	EACFCameraOrbitInputSource InputSource = EACFCameraOrbitInputSource::CameraInput;

	/** Degrees per input unit (X = yaw, Y = pitch). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (EditCondition = "InputSource == EACFCameraOrbitInputSource::CameraInput"))
	FVector2D Sensitivity = FVector2D(1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (EditCondition = "InputSource == EACFCameraOrbitInputSource::CameraInput"))
	bool bInvertYaw = false;

	/** Positive input pitches the camera up (looks up) unless inverted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (EditCondition = "InputSource == EACFCameraOrbitInputSource::CameraInput"))
	bool bInvertPitch = false;

	// --- Initial / idle ------------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Initial")
	EACFCameraOrbitInitialRotation InitialRotation = EACFCameraOrbitInitialRotation::BehindTarget;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Initial", meta = (Units = deg))
	float InitialYaw = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Initial", meta = (Units = deg))
	float InitialPitch = -15.f;

	/** Without input for RecenterDelay seconds, ease back behind the target (yaw + InitialYaw). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle")
	bool bRecenterWhenIdle = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle", meta = (EditCondition = "bRecenterWhenIdle", ClampMin = "0.0", Units = s))
	float RecenterDelay = 2.f;

	/** Time constant of the recentering motion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle", meta = (EditCondition = "bRecenterWhenIdle", ClampMin = "0.01", Units = s))
	float RecenterTime = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle", meta = (EditCondition = "bRecenterWhenIdle"))
	bool bRecenterPitch = true;

	/** Distance for a given absolute pitch (rings or constant). */
	float GetDistanceForPitch(float Pitch) const;

	/** Requests a new orbit rotation (absolute, world space) for the next evaluation. */
	void SetOrbitRotation(FInstancedStruct* RuntimeState, const FRotator& Rotation, bool bInstant) const;

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::Body; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraOrbitState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override;
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};
