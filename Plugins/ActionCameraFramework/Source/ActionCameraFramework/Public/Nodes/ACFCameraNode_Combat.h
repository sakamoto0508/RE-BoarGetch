#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "ACFCameraNode_Combat.generated.h"

/** Per-request runtime state of a Combat Camera node (damped outputs). */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraCombatNodeState
{
	GENERATED_BODY()

	float RangeAlpha = 1.f;
	float DistanceScale = 1.f;
	float FovOffset = 0.f;
	FVector Shoulder = FVector::ZeroVector;
	float Range = 0.f;
	bool bHasEnemy = false;
	bool bInitialized = false;
};

/**
 * Framing (runs FIRST in the Framing stage, before Group Framing): combat composition on top of the camera
 * that Body / Aim produced (Orbital Follow for normal combat, Lock-On for lock-on combat).
 *
 *  - arm length around the rig's pivot x DistanceScale (from the player-enemy distance, Near / Far)
 *  - FOV + FieldOfViewOffset
 *  - over-the-shoulder offset in camera space
 * all scaled by the combat weight (0 out of combat, Intensity in combat) and damped (Enter / Exit time).
 *
 * The combat state comes from gameplay (UACFCameraSubsystem::SetCombatState); this node never decides
 * whether the player is fighting. Group Framing (after it) still keeps the subjects on screen, effects
 * play on top, and Collision / the constraint solver run afterwards on the final pose.
 */
UCLASS(meta = (DisplayName = "Combat Camera"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_Combat : public UACFCameraNode
{
	GENERATED_BODY()

public:
	/** Used unless the combat state overrides it (FACFCameraCombatState::bOverrideProfile). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	FACFCameraCombatProfile Profile;

	/** Time constant entering combat / intensity going up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", Units = s))
	float EnterTime = 0.35f;

	/** Time constant leaving combat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", Units = s))
	float ExitTime = 0.8f;

	/** Time constant of the enemy-distance factor (no pumping when the enemy dashes in and out). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", Units = s))
	float RangeDampingTime = 0.5f;

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::Framing; }
	virtual int32 GetStageOrder() const override { return -100; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraCombatNodeState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override { ResetInterpolation(RuntimeState); }
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};
