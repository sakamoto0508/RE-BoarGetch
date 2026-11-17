#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "ACFCameraNode_SpiralOrbit.generated.h"

class UCurveFloat;

/** Where the orbit starts (angle 0 of the spiral, seen from the focus point). */
UENUM(BlueprintType)
enum class EACFCameraSpiralStart : uint8
{
	/** Behind the player's view: the yaw of the control rotation (the gameplay camera's side). Default. */
	ControlRotation,
	/** The side the follow target (attacker / player) stands on, seen from the focus. */
	FollowTarget,
	/** World yaw Start Angle Offset. */
	World,
};

/** One evaluated point of the spiral (also used by the game for debug drawing and feasibility checks). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraSpiralSample
{
	GENERATED_BODY()

	/** 0..1 progress of the orbit (time / Duration, clamped). */
	UPROPERTY(BlueprintReadOnly, Category = "Spiral Orbit") float Progress = 0.f;
	/** Degrees travelled since the start (0..Rotation Degrees). */
	UPROPERTY(BlueprintReadOnly, Category = "Spiral Orbit") float AngleTravelled = 0.f;
	/** World yaw of the camera seen from the focus. */
	UPROPERTY(BlueprintReadOnly, Category = "Spiral Orbit") float Yaw = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Spiral Orbit") float Radius = 0.f;
	/** Camera height above the focus point. */
	UPROPERTY(BlueprintReadOnly, Category = "Spiral Orbit") float Height = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Spiral Orbit") FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Spiral Orbit") FVector LookAt = FVector::ZeroVector;
};

/** Runtime state of a Spiral Orbit (per request). */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraSpiralOrbitState
{
	GENERATED_BODY()

	bool bStarted = false;
	float Elapsed = 0.f;
	float StartYaw = 0.f;
	FVector Focus = FVector::ZeroVector;
	TWeakObjectPtr<const AActor> FocusActor;
	FACFCameraSpiralSample Last;
};

/**
 * Body + aim: the camera ORBITS a focus point (its position travels on a circle around it, it does not just spin in
 * place) while rising, and keeps looking at the focus. One evaluation = one point of a continuous spiral:
 *
 *   Angle(t)  = Start + Direction * RotationDegrees * AngleCurve(t / Duration)
 *   Radius(t) = Lerp(RadiusStart, RadiusEnd, RadiusCurve(t / Duration))
 *   Height(t) = Lerp(HeightStart, HeightEnd, HeightCurve(t / Duration))
 *   Location  = Focus + (cos Angle * Radius, sin Angle * Radius, Height),  Rotation = look at Focus + Look At Offset, Roll 0
 *
 * Time is the request's camera time (pauses with the game, starts when the request becomes active). After Duration the
 * last point is held until the request ends. The focus is the Look At Target (else the Follow Target) + Focus Offset:
 * give it a lightweight proxy actor when the subject may disappear (a lost target keeps the last focus).
 * Publishes the focus as the rig pivot, so the Collision node pulls the camera towards the subject instead of into
 * a wall when the ideal circle is blocked.
 */
UCLASS(meta = (DisplayName = "Spiral Orbit"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_SpiralOrbit : public UACFCameraNode
{
	GENERATED_BODY()

public:
	/** Offset added to the focus target location (world space). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit")
	FVector FocusOffset = FVector::ZeroVector;

	/** Damping of the focus point when the focus target moves (0 = rigid). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit", meta = (ClampMin = "0.0", UIMax = "1.0", Units = s))
	float FocusDampingTime = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit")
	EACFCameraSpiralStart StartMode = EACFCameraSpiralStart::ControlRotation;

	/** Added to the start yaw (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit", meta = (Units = deg))
	float StartAngleOffset = 0.f;

	/** Total orbit angle (360 = one full turn around the focus). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit", meta = (ClampMin = "0.0", UIMax = "720.0", Units = deg))
	float RotationDegrees = 360.f;

	/** +1 = counter-clockwise seen from above, -1 = clockwise. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit", meta = (ClampMin = "-1", ClampMax = "1"))
	int32 Direction = 1;

	/** Seconds (camera time) for the whole spiral. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit", meta = (ClampMin = "0.05", UIMax = "5.0", Units = s))
	float Duration = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit", meta = (ClampMin = "10.0", Units = cm))
	float RadiusStart = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit", meta = (ClampMin = "10.0", Units = cm))
	float RadiusEnd = 340.f;

	/** Camera height above the focus at the start (may be negative = slightly below the subject). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit", meta = (Units = cm))
	float HeightStart = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit", meta = (Units = cm))
	float HeightEnd = 260.f;

	/** Angle progress over normalized time (0..1 -> 0..1). Empty = ease in-out (Angle Ease Exponent). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit")
	TObjectPtr<UCurveFloat> AngleCurve = nullptr;

	/** Exponent of the built-in ease in-out of the angle when Angle Curve is empty (1 = constant speed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit", meta = (ClampMin = "1.0", UIMax = "4.0"))
	float AngleEaseExponent = 1.6f;

	/** Height progress over normalized time (0..1 -> 0..1). Empty = smooth step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit")
	TObjectPtr<UCurveFloat> HeightCurve = nullptr;

	/** Radius progress over normalized time (0..1 -> 0..1). Empty = linear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit")
	TObjectPtr<UCurveFloat> RadiusCurve = nullptr;

	/** The camera looks at Focus + this (e.g. slightly above the subject's centre). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit")
	FVector LookAtOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit", meta = (InlineEditConditionToggle))
	bool bOverrideFieldOfView = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spiral Orbit", meta = (EditCondition = "bOverrideFieldOfView", UIMin = "5.0", UIMax = "170", ClampMin = "0.001", ClampMax = "360.0", Units = deg))
	float FieldOfView = 70.f;

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::Body; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraSpiralOrbitState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override;
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;

	/** The spiral point at Time seconds for a given focus and start yaw (pure; for debug drawing and feasibility checks). */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Spiral Orbit")
	FACFCameraSpiralSample SampleSpiral(const FVector& Focus, float StartYaw, float Time) const;
};
