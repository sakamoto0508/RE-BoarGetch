#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "Nodes/ACFCameraTargetTracking.h"
#include "ACFCameraNode_LookAt.generated.h"

/** Which request target an Aim node aims at. */
UENUM(BlueprintType)
enum class EACFCameraLookAtSource : uint8
{
	LookAtTarget,
	/** Aim at the follow target (common for a simple chase camera with a single target). */
	FollowTarget,
	/**
	 * Aim at the weighted centre of the request's Target Group. The centre is not teleport-detected (it jumps when
	 * members are added or removed, which is smoothed by damping instead).
	 */
	TargetGroup,
};

/**
 * Aim: rotates the camera to look straight at a target point (no roll). Damping is applied to the
 * aim point, not to the rotation, so it is frame-rate independent and does not overshoot. Use
 * Rotation Composer instead when the target should sit at a screen position with dead / soft zones.
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
	FACFCameraTargetMotionSettings TargetMotion;

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::Aim; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraTargetTrackingState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override { ResetInterpolation(RuntimeState); }
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};
