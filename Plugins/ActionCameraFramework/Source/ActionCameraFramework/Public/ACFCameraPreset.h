#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ACFCameraTypes.h"
#include "ACFCameraPreset.generated.h"

class UACFCameraNode;

/**
 * Shareable, designer-authored description of one camera ("virtual camera" in Cinemachine
 * terms): the rig node stack plus default blends.
 *
 * Contains configuration only. Runtime state lives in FACFCameraRigInstance, created per
 * request, so the same preset can safely drive several local players at once.
 */
UCLASS(BlueprintType)
class ACTIONCAMERAFRAMEWORK_API UACFCameraPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Evaluated top to bottom; each node refines the pose of the previous one. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Rig")
	TArray<TObjectPtr<UACFCameraNode>> Nodes;

	/** Blend used when this preset becomes the active camera (can be overridden per request). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend")
	FACFCameraBlendSettings DefaultBlendIn;

	/** Blend used when this preset is released and control returns to the UE camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend")
	FACFCameraBlendSettings DefaultBlendOut;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
