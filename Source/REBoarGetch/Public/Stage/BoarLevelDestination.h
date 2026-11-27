#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BoarLevelDestination.generated.h"

// 同じ行き先を指す複数のポータル（例：LobbyのGadgetLab行きとNet TestからのGadgetLab戻り）が同じアセットを参照するので、行き先を差し替えるとき（Redesign版への切り替えなど）はこのアセットの Level だけを変更します。
/** ポータル（AHubPortal）の行き先Level 1つ分の設定です。 */
UCLASS(BlueprintType)
class REBOARGETCH_API UBoarLevelDestination : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** このアセットを参照する全ポータルの移動先マップです。差し替えると共通の行き先が切り替わります。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Destination")
	TSoftObjectPtr<UWorld> Level;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
