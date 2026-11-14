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

	// --- Phase 7B-1: movement assistance (only used with Enable Movement Assistance) ------------------------------
	/** Seconds without any blocker (input, explicit rotation, suppression, view not shown, not in control, activation). */
	float AssistIdleTime = 0.f;
	/** Seconds the target has been moving (speed hysteresis below). Runs at the same time as AssistIdleTime. */
	float MovementTime = 0.f;
	/** Speed hysteresis: true from Start Speed Threshold until the speed drops below Stop Speed Threshold. */
	bool bMoving = false;
	/** Last evaluation's horizontal speed (cm/s, from the tracked target's smoothed velocity) and heading. */
	float MovementSpeed = 0.f;
	bool bHasHeading = false;
	float HeadingYaw = 0.f;
	EACFCameraAssistanceState AssistanceState = EACFCameraAssistanceState::Disabled;
	EACFCameraAssistanceReason AssistanceReason = EACFCameraAssistanceReason::NotEnabled;
	/**
	 * Phase 7B-2: manual input activity. Accumulated mouse turning (degrees, decays over InputActivityWindow) and the last
	 * activity (degrees per second: max of the stick rate and the mouse accumulation / window). Input is not movement:
	 * ResetAssistance keeps them (a teleport while the player turns is still manual input).
	 */
	float InputActivityAccum = 0.f;
	float InputActivity = 0.f;

	/** Forgets the movement history and restarts the waits (activation, teleport, interpolation reset). */
	void ResetAssistance()
	{
		AssistIdleTime = 0.f;
		MovementTime = 0.f;
		bMoving = false;
		MovementSpeed = 0.f;
		bHasHeading = false;
	}
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

	/**
	 * Phase 7B-2: the per-second input of Set Camera Input Rate (stick) turns the camera in real time, so a slow motion
	 * (Set Global Time Dilation) does not slow the player's camera control. Off = game time (the camera turns slower in
	 * slow motion, like every other camera motion). Add Camera Input deltas (mouse) are displacements: never scaled by time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (EditCondition = "InputSource == EACFCameraOrbitInputSource::CameraInput"))
	bool bInputRateIgnoresTimeDilation = false;

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

	/** Seconds without input before recentering starts. With Movement Assistance: without any blocker (README 29 章). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle", meta = (EditCondition = "bRecenterWhenIdle || bEnableMovementAssistance", ClampMin = "0.0", Units = s))
	float RecenterDelay = 2.f;

	/** Time constant of the recentering motion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle", meta = (EditCondition = "bRecenterWhenIdle || bEnableMovementAssistance", ClampMin = "0.01", Units = s))
	float RecenterTime = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle", meta = (EditCondition = "bRecenterWhenIdle || bEnableMovementAssistance"))
	bool bRecenterPitch = true;

	// --- Movement assistance (Phase 7B-1, README 29 章) ---------------------------------------------------------------
	/**
	 * Recenter only while the follow target moves, towards Recenter Heading, at most Max Assistance Yaw Speed, and only for
	 * the request in control while ACF is on screen. Manual input stops it at once. Works with or without Recenter When
	 * Idle (with assistance on, standing still never recenters). Off = the recenter of Phase 2, unchanged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle|Movement Assistance")
	bool bEnableMovementAssistance = false;

	/** What the recenter turns towards. Movement Direction needs Enable Movement Assistance (without it: Target Facing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle|Movement Assistance", meta = (EditCondition = "bRecenterWhenIdle || bEnableMovementAssistance"))
	EACFCameraRecenterHeading RecenterHeading = EACFCameraRecenterHeading::TargetFacing;

	/** Horizontal speed (perpendicular to world up) at which the target counts as moving. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle|Movement Assistance", meta = (EditCondition = "bEnableMovementAssistance", ClampMin = "0.0", Units = CentimetersPerSecond))
	float StartSpeedThreshold = 150.f;

	/** Once moving, the target stops counting as moving below this speed (hysteresis: at most Start Speed Threshold). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle|Movement Assistance", meta = (EditCondition = "bEnableMovementAssistance", ClampMin = "0.0", Units = CentimetersPerSecond))
	float StopSpeedThreshold = 75.f;

	/** The target must keep moving this long before assistance starts (counted at the same time as Recenter Delay). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle|Movement Assistance", meta = (EditCondition = "bEnableMovementAssistance", ClampMin = "0.0", Units = s))
	float RequiredMovementDuration = 0.3f;

	/**
	 * No assistance while the heading points towards the camera within this angle of straight back (running at the camera
	 * never swings it 180 degrees). 45 = headings more than 135 degrees away from the camera direction are ignored.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle|Movement Assistance", meta = (EditCondition = "bEnableMovementAssistance", ClampMin = "0.0", ClampMax = "180.0", Units = deg))
	float BackwardExclusionAngle = 45.f;

	/** The camera only turns by the part of the yaw difference beyond this angle (no creeping for small differences). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle|Movement Assistance", meta = (EditCondition = "bEnableMovementAssistance", ClampMin = "0.0", ClampMax = "180.0", Units = deg))
	float AngularDeadZone = 5.f;

	/** Upper bound of the yaw the assistance adds per second (before Rotation Damping, which only slows it further). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle|Movement Assistance", meta = (EditCondition = "bEnableMovementAssistance", ClampMin = "0.01", Units = DegreesPerSecond))
	float MaxAssistanceYawSpeed = 90.f;

	/**
	 * Phase 7B-2: camera input counts as manual input (stops the assistance and restarts its wait) only from this turning
	 * speed (degrees per second after Sensitivity). Smaller input (stick drift, mouse jitter) still turns the camera but
	 * does not block the assistance. One unit for both devices: the stick rate is compared directly, mouse deltas over a
	 * short window (0.1 s), so a single jitter count weighs the same at every frame rate. 0 = any input (Phase 7B-1).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle|Movement Assistance", meta = (EditCondition = "bEnableMovementAssistance && InputSource == EACFCameraOrbitInputSource::CameraInput", ClampMin = "0.0", Units = DegreesPerSecond))
	float InputActivityThreshold = 5.f;

	/** Window of the mouse (Add Camera Input) activity measure, seconds. */
	static constexpr float InputActivityWindow = 0.1f;

	/** Distance for a given absolute pitch (rings or constant). */
	float GetDistanceForPitch(float Pitch) const;

	/** Requests a new orbit rotation (absolute, world space) for the next evaluation. */
	void SetOrbitRotation(FInstancedStruct* RuntimeState, const FRotator& Rotation, bool bInstant) const;

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::Body; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraOrbitState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override;
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;

private:
	/**
	 * Phase 7B-1: the recenter with Movement Assistance (one evaluation with DeltaTime > 0). Same recenter step as without it
	 * (exponential with Recenter Time towards the home yaw); the assistance decides when it runs, the heading, the dead zone
	 * and the yaw speed limit.
	 */
	void UpdateMovementAssistance(const FACFCameraNodeContext& Context, FACFCameraOrbitState& State, float TargetYaw, bool bManualInput, bool bExplicitRotation) const;

	/** Phase 7B-2: measures this evaluation's camera input (DeltaTime > 0); true if it is manual input for the assistance. */
	bool UpdateInputActivity(const FACFCameraNodeContext& Context, FACFCameraOrbitState& State, bool bAnyInput) const;
};
