#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "Nodes/ACFCameraTargetTracking.h"
#include "ACFCameraNode_Follow.generated.h"

/**
 * Body: moves the camera to the request's FollowTarget plus an offset, optionally damped per axis.
 * Position only: rotation is left to Aim nodes (LookAt, Rotation Composer) or the upstream camera.
 * Without a FollowTarget the node does nothing. Use Orbital Follow for player-controlled cameras
 * and Position Composer for screen-space framing; only one Body position node should be enabled.
 */
UCLASS(meta = (DisplayName = "Follow"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_Follow : public UACFCameraNode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow")
	FVector Offset = FVector(-300.f, 0.f, 150.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow")
	EACFCameraOffsetSpace OffsetSpace = EACFCameraOffsetSpace::TargetLocal;

	/** Per-axis time constants. A constant-velocity target is trailed by Velocity * Time on each axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow")
	FACFCameraPositionDamping Damping;

	/** Target loss, teleport detection and lookahead (the camera leads the predicted position). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow")
	FACFCameraTargetMotionSettings TargetMotion;

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::Body; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraTargetTrackingState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override { ResetInterpolation(RuntimeState); }
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};
