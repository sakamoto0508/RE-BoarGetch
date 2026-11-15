#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ACFTransitionTypes.h"
#include "ACFTransitionPreset.generated.h"

/**
 * A designer-made transition: the look of its Close and its Open (README 33 章). The plugin ships one standard preset per
 * effect in /ActionCameraFramework/Transitions/Presets (DA_ACFTransition_<ID>_<Name>, made from the catalog defaults by
 * Scripts/Transitions/CreateTransitionAssets.py). Copy one to change it; the transition API takes a preset or plain
 * settings.
 */
UCLASS(BlueprintType)
class ACTIONCAMERAFRAMEWORKTRANSITIONS_API UACFTransitionPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Played by Close (scene -> covered). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	FACFTransitionSettings Close;

	/** Played by Open (covered -> scene). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	FACFTransitionSettings Open;

	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	const FACFTransitionSettings& GetSettings(EACFTransitionPhase Phase) const { return Phase == EACFTransitionPhase::Open ? Open : Close; }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
