#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "StageGimmick.generated.h"

UINTERFACE(BlueprintType)
class REBOARGETCH_API UStageGimmick : public UInterface
{
	GENERATED_BODY()
};

/** ダメージ床や移動床など、ステージギミック共通の有効・無効契約です。 */
class REBOARGETCH_API IStageGimmick
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "REBoarGetch|Stage Gimmick")
	void ActivateGimmick();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "REBoarGetch|Stage Gimmick")
	void DeactivateGimmick();
};
