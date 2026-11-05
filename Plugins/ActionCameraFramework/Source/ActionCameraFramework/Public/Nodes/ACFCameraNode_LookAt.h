#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "Nodes/ACFCameraTargetTracking.h"
#include "ACFCameraNode_LookAt.generated.h"

/** Which request target a LookAt node aims at. */
UENUM(BlueprintType)
enum class EACFCameraLookAtSource : uint8
{
	LookAtTarget,
	/** Aim at the follow target (common for a simple chase camera with a single target). */
	FollowTarget,
};

/**
 * Rotates the camera to aim at a target point (no roll). Place it after the node that sets the
 * camera location (e.g. Follow). Damping is applied to the aim point, not to the rotation, so it
 * is frame-rate independent and does not overshoot. Without a target the node does nothing.
 */
UCLASS(meta = (DisplayName = "Look At"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_LookAt : public UACFCameraNode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look At")
	EACFCameraLookAtSource TargetSource = EACFCameraLookAtSource::LookAtTarget;

	/** Aim point relative to the target (e.g. chest height instead of the feet). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look At")
	FVector TargetOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look At")
	EACFCameraOffsetSpace OffsetSpace = EACFCameraOffsetSpace::World;

	/** Time constant in seconds for the aim point (0 = rigid). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look At", meta = (ClampMin = "0.0", UIMax = "2.0", Units = s))
	float DampingTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look At")
	EACFCameraTargetLostBehavior TargetLostBehavior = EACFCameraTargetLostBehavior::HoldLastKnown;

	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraTargetTrackingState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};
