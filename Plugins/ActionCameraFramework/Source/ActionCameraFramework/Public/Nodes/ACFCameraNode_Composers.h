#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "Nodes/ACFCameraFraming.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "Nodes/ACFCameraTargetTracking.h"
#include "ACFCameraNode_Composers.generated.h"

/** Runtime state of a Rotation Composer: correction relative to the incoming rotation. */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraRotationComposerState
{
	GENERATED_BODY()

	FACFCameraTargetTrackingState Tracking;
	/** Yaw / pitch (degrees) added to the rotation coming from the Body stage / upstream camera. */
	FVector2D Correction = FVector2D::ZeroVector;
	bool bInitialized = false;
};

/**
 * Aim: keeps a target at a screen position by ROTATING the camera (dead / soft zones, damping).
 *
 * Responsibility split: Rotation Composer only changes rotation; the camera position comes from
 * the Body stage (Orbital Follow, Follow, Fixed Pose) or the UE camera. Its state is a correction
 * on top of the incoming rotation, so with Orbital Follow the player's orbit input turns the camera
 * immediately and only the framing correction is damped (no fight between the two).
 * Framing is computed in normalised screen space every frame, so it survives FOV and aspect changes.
 */
UCLASS(meta = (DisplayName = "Rotation Composer"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_RotationComposer : public UACFCameraNode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rotation Composer")
	EACFCameraLookAtSource TargetSource = EACFCameraLookAtSource::LookAtTarget;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rotation Composer")
	FVector TargetOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rotation Composer")
	EACFCameraOffsetSpace OffsetSpace = EACFCameraOffsetSpace::World;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rotation Composer")
	FACFCameraScreenComposition Composition;

	/** Time constants (X = horizontal, Y = vertical) for moving the target back into the dead zone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rotation Composer", meta = (ClampMin = "0.0", UIMax = "2.0"))
	FVector2D Damping = FVector2D(0.3f, 0.3f);

	/** Lookahead here makes the camera aim where the target is going. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rotation Composer")
	FACFCameraTargetMotionSettings TargetMotion;

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::Aim; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraRotationComposerState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override { ResetInterpolation(RuntimeState); }
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};

/** Runtime state of a Position Composer. */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraPositionComposerState
{
	GENERATED_BODY()

	FACFCameraTargetTrackingState Tracking;
	FVector CameraLocation = FVector::ZeroVector;
	bool bInitialized = false;
};

/**
 * Body: keeps the follow target at a screen position by MOVING the camera in its view plane and
 * keeping CameraDistance along the view direction (2.5D / side-scroller / top-down framing).
 *
 * Responsibility split: Position Composer owns the camera position (it is an alternative to
 * Follow / Orbital Follow, not a layer on top of them) and never rotates; the rotation it frames
 * with is the incoming one (Fixed Pose or the UE camera). Do not combine it with Aim nodes that
 * re-aim at the same target; the preset validator warns about that.
 */
UCLASS(meta = (DisplayName = "Position Composer"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_PositionComposer : public UACFCameraNode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Position Composer")
	FVector TargetOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Position Composer")
	EACFCameraOffsetSpace OffsetSpace = EACFCameraOffsetSpace::World;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Position Composer", meta = (ClampMin = "1.0", Units = cm))
	float CameraDistance = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Position Composer")
	FACFCameraScreenComposition Composition;

	/** Time constants: X = distance (depth), Y = horizontal, Z = vertical screen correction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Position Composer", meta = (ClampMin = "0.0", UIMax = "2.0"))
	FVector Damping = FVector(0.3f, 0.3f, 0.3f);

	/** Lookahead here leaves more room in front of a moving target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Position Composer")
	FACFCameraTargetMotionSettings TargetMotion;

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::Body; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraPositionComposerState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override { ResetInterpolation(RuntimeState); }
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};
