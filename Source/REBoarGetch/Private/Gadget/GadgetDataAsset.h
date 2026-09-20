#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GadgetDataAsset.generated.h"

class UTexture2D;

/** ガジェット共通設定を追加するための基底DataAssetです。 */
UCLASS(BlueprintType)
class UGadgetDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 保存に使用する固定IDです。表示名やActor名から生成しません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Progress")
	FName GadgetId;
	/** Noneなら初期解放。解放済みIDは条件変更後も再ロックしません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Progress")
	FName RequiredClearedStageId;
	/** HUDなどで使用するガジェットの表示アイコンです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Display")
	TObjectPtr<UTexture2D> DisplayIcon;
};
