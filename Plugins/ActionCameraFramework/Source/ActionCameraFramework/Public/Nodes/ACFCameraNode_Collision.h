#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "Engine/EngineTypes.h"
#include "ACFCameraNode_Collision.generated.h"

/** Runtime state of the Collision node. */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraCollisionState
{
	GENERATED_BODY()

	/** Current allowed distance from the pivot (smoothed when returning). */
	float CurrentDistance = 0.f;
	/** Unobstructed distance of the previous evaluation (tells "returning from a hit" from "zooming out"). */
	float LastFullDistance = 0.f;
	bool bInitialized = false;
};

/**
 * PostBlend: keeps the FINAL camera out of walls with a sphere sweep from the rig's pivot (the
 * follow / orbit target) to the camera, like USpringArmComponent's probe.
 *
 * It runs once per frame AFTER request blending (whatever its position in the node list), using
 * the pivot of the cameras being blended, so a blend between two cameras never cuts through
 * geometry. It runs BEFORE UE camera shakes (they are applied by the PlayerCameraManager after
 * ACF): keep ProbeRadius larger than the shake translation amplitude to stay clear of walls.
 *
 * Pull-in is instant by default (the camera never ends up inside a wall); moving back out once
 * the obstacle is gone is smoothed by ReturnDampingTime.
 */
UCLASS(meta = (DisplayName = "Collision"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_Collision : public UACFCameraNode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "0.0", Units = cm))
	float ProbeRadius = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
	TEnumAsByte<ECollisionChannel> ProbeChannel = ECC_Camera;

	/** The camera never gets closer to the pivot than this (unless the desired camera is closer). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "0.0", Units = cm))
	float MinDistance = 20.f;

	/** Smoothing when an obstacle pushes the camera in. 0 = instant (recommended: no wall clipping). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "0.0", UIMax = "0.5", Units = s))
	float PullInDampingTime = 0.f;

	/** Smoothing when the obstacle is gone and the camera moves back to its desired distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "0.0", UIMax = "2.0", Units = s))
	float ReturnDampingTime = 0.35f;

	/** Ignore the actor that owns the pivot (the follow target), so the character never blocks its own camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
	bool bIgnorePivotActor = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
	bool bIgnoreLookAtTargetActor = false;

	/** Ignore the pawn controlled by this player (and its attached actors). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
	bool bIgnoreControlledPawn = true;

	/** Sweep origin when the rig publishes no pivot (e.g. Fixed Pose only): follow target + this offset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
	FVector FallbackOriginOffset = FVector(0.f, 0.f, 60.f);

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::PostBlend; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraCollisionState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override { ResetInterpolation(RuntimeState); }
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};
