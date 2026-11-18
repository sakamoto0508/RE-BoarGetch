#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GadgetDataAsset.generated.h"

class UTexture2D;

/** 装備選択に表示する名前・役割・アイコン、解放条件、試用マップをガジェットごとに設定します。 */
UCLASS(BlueprintType)
class UGadgetDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 装備選択画面に表示するガジェット名です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Display")
	FText DisplayName;

	/** 装備選択画面で名前と一緒に表示する、用途や役割の短い説明です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Display")
	FText RoleText;

	/** 装備選択画面の詳細欄に表示する説明文です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Display", meta = (MultiLine = "true"))
	FText Description;

	/** 解放・装備の保存に使う固定IDです。ガジェットごとに一意にし、リリース後も維持します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Progress")
	FName GadgetId;

	/** 有効にすると進行データの初期化時に解放済みになります。初期装備のアミ・剣に使用します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Progress")
	bool bInitiallyUnlocked = false;

	/** 手動解放に必要な特別コインの取得数です。0ならコインを取得する前から解放操作ができます。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Progress", meta = (ClampMin = "0"))
	int32 RequiredSpecialCoinCount = 0;

	/** 解放済みのガジェットを選んで試用するときの移動先マップです。None では試用先を提供しません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Test")
	TSoftObjectPtr<UWorld> TestLevel;

	/** HUDなどで使用するガジェットの表示アイコンです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Display")
	TObjectPtr<UTexture2D> DisplayIcon;
};
