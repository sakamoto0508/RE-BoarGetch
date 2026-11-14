#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "ACFCameraNode_FixedPose.generated.h"

/**
 * Overrides location, rotation and / or field of view with constant world-space values.
 * Components that are not overridden keep the value from the previous node (or the upstream
 * UE camera), e.g. "FOV only" makes a simple zoom preset.
 */
UCLASS(meta = (DisplayName = "Fixed Pose"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_FixedPose : public UACFCameraNode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Pose", meta = (InlineEditConditionToggle))
	bool bOverrideLocation = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Pose", meta = (EditCondition = "bOverrideLocation"))
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Pose", meta = (InlineEditConditionToggle))
	bool bOverrideRotation = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Pose", meta = (EditCondition = "bOverrideRotation"))
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Pose", meta = (InlineEditConditionToggle))
	bool bOverrideFieldOfView = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Pose", meta = (EditCondition = "bOverrideFieldOfView", UIMin = "5.0", UIMax = "170", ClampMin = "0.001", ClampMax = "360.0", Units = deg))
	float FieldOfView = 90.f;

	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};
