#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StageConfig.generated.h"

/** 檻、出現表、制限時間、クリア条件、ギミック、報酬を定義するステージ設定です。 */
UCLASS(BlueprintType)
class REBOARGETCH_API UStageConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** ステージクリアに必要な累計捕獲数です。0以下の場合は捕獲数によるクリア判定を無効にします。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Clear", meta = (ClampMin = "0"))
	int32 TargetCaptureCount = 0;
};
