#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "Nodes/ACFCameraTargetTracking.h"
#include "ACFCameraNode_Follow.generated.h"

/**
 * Moves the camera to the request's FollowTarget plus an offset, optionally damped.
 * Position only: rotation is left to later nodes (LookAt) or the upstream camera.
 * Without a FollowTarget the node does nothing.
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

	/** Time constant in seconds (0 = rigid). A constant-velocity target is trailed by Velocity * DampingTime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow", meta = (ClampMin = "0.0", UIMax = "2.0", Units = s))
	float DampingTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow")
	EACFCameraTargetLostBehavior TargetLostBehavior = EACFCameraTargetLostBehavior::HoldLastKnown;

	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraTargetTrackingState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};
