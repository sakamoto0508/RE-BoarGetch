#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BoarLevelDestination.generated.h"

/**
 * ポータル（AHubPortal）の行き先Level 1つ分の設定です。
 * 同じ行き先を指す複数のポータル（例：LobbyのGadgetLab行きとNet TestからのGadgetLab戻り）が同じアセットを参照するので、
 * 行き先を差し替えるとき（Redesign版への切り替えなど）はこのアセットの Level だけを変更します。
 */
UCLASS(BlueprintType)
class REBOARGETCH_API UBoarLevelDestination : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 遷移先のLevel。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Destination")
	TSoftObjectPtr<UWorld> Level;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
