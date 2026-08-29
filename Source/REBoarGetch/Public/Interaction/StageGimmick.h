#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "StageGimmick.generated.h"

/** BlueprintとC++のActorへステージギミック共通操作を実装可能にするInterface型です。 */
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
	/** ギミック固有の効果や動作を有効化します。 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "REBoarGetch|Stage Gimmick")
	void ActivateGimmick();

	/** ギミック固有の効果や動作を無効化します。 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "REBoarGetch|Stage Gimmick")
	void DeactivateGimmick();
};
