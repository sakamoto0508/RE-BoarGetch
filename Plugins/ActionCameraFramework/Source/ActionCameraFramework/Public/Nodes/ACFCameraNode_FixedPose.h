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
	/** オンにするとカメラ位置を下の Location に固定します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Pose", meta = (InlineEditConditionToggle))
	bool bOverrideLocation = true;

	/** 固定するワールド座標（cm）です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Pose", meta = (EditCondition = "bOverrideLocation"))
	FVector Location = FVector::ZeroVector;

	/** オンにするとカメラの向きを下の Rotation に固定します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Pose", meta = (InlineEditConditionToggle))
	bool bOverrideRotation = true;

	/** 固定するワールド回転（度）です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Pose", meta = (EditCondition = "bOverrideRotation"))
	FRotator Rotation = FRotator::ZeroRotator;

	/** オンにすると下の Field Of View を使います。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Pose", meta = (InlineEditConditionToggle))
	bool bOverrideFieldOfView = true;

	/** 固定する水平画角（度）です。大きいほど広い範囲が映ります。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixed Pose", meta = (EditCondition = "bOverrideFieldOfView", UIMin = "5.0", UIMax = "170", ClampMin = "0.001", ClampMax = "360.0", Units = deg))
	float FieldOfView = 90.f;

	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};
