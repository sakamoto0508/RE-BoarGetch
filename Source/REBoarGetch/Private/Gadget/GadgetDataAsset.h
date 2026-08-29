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
	/** HUDなどで使用するガジェットの表示アイコンです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Display")
	TObjectPtr<UTexture2D> DisplayIcon;
};
