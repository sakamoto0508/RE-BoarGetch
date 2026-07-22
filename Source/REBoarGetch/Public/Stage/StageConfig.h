#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StageConfig.generated.h"

/** 檻、出現表、制限時間、クリア条件、ギミック、報酬を定義するステージ設定です。 */
UCLASS(BlueprintType)
class REBOARGETCH_API UStageConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()
};
