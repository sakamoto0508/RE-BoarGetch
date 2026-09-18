#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StageConfig.generated.h"

class UTexture2D;
class UWorld;
class ABoarBase;

/** Stage開始時に生成するBoar Classと体数の組み合わせです。 */
USTRUCT(BlueprintType)
struct FBoarSpawnEntry
{
	GENERATED_BODY()

	/** 生成するABoarBase派生Classです。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "REBoarGetch|Stage|Spawn")
	TSubclassOf<ABoarBase> BoarClass;

	/** このClassを生成する体数です。0の場合は生成しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "REBoarGetch|Stage|Spawn", meta = (ClampMin = "0"))
	int32 Count = 0;
};

/** ロビー表示、遷移先Level、クリア条件をまとめるステージ設定です。 */
UCLASS(BlueprintType)
class REBOARGETCH_API UStageConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** セーブや解放判定で使用するステージ固有IDです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Identity")
	FName StageId;

	/** ロビーUIへ表示するステージ名です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Display")
	FText DisplayName;

	/** ロビーUIへ表示するステージ説明です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Display", meta = (MultiLine = "true"))
	FText Description;

	/** ステージ選択決定時に開くLevelです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Travel")
	TSoftObjectPtr<UWorld> Level;

	/** ロビーUIへ表示する任意のサムネイルです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Display")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	/** ステージクリアに必要な累計捕獲数です。0以下の場合は捕獲数によるクリア判定を無効にします。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Clear", meta = (ClampMin = "0"))
	int32 TargetCaptureCount = 0;

	/** Stage開始時に生成するBoarの種類と体数です。空配列なら自動生成を行いません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Spawn")
	TArray<FBoarSpawnEntry> BoarSpawnEntries;

	/** 有効なSpawn Entryの合計出現数を返します。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Stage|Spawn")
	int32 GetTotalBoarSpawnCount() const;
};
