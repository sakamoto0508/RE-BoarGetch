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
UCLASS(BlueprintType, meta = (ToolTip = "画面を覆う Close と、再表示する Open の演出を設定します。各フェーズの種類・秒数・見た目を調整できます。"))
class ACTIONCAMERAFRAMEWORKTRANSITIONS_API UACFTransitionPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 画面を覆うときの演出です。ロード前のフェードやワイプをここで調整します。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Close (画面を覆う)")
	FACFTransitionSettings Close;

	/** 覆った画面を再び見せるときの演出です。ロード後のフェードやワイプをここで調整します。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Open (画面を表示)")
	FACFTransitionSettings Open;

	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	const FACFTransitionSettings& GetSettings(EACFTransitionPhase Phase) const { return Phase == EACFTransitionPhase::Open ? Open : Close; }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
