#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StageConfig.generated.h"

class UTexture2D;
class UWorld;
class ABoarBase;
class ABoarStagePreviewActor;

/** 永続個体IDと、Level上の固定SpawnPoint IDを結び付ける定義です。 */
USTRUCT(BlueprintType)
struct FBoarSpawnDefinition
{
	GENERATED_BODY()
	/** 捕獲記録・NEW判定に使う固定個体ID。例：Stage01_Boar_001。再挑戦・再起動でも変更せず、他個体と重複させません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName BoarUniqueId;
	/** この個体として生成するABoarBase派生Class。使用するイノシシBPを指定します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<ABoarBase> BoarClass;
	/** 配置先LevelのABoarSpawnPointに設定したID。例：Spawn_A。配列順・Actor取得順ではなくIDで位置を対応付けます。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName SpawnPointId;
	/** 図鑑表示用の個体名。未登録の文面をUI側で推測しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encyclopedia") FText EncyclopediaName;
	/** 図鑑の個体説明。表示する紹介文を設定します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encyclopedia", meta = (MultiLine = "true")) FText EncyclopediaDescription;
	/** 図鑑の特徴説明。個体の性質・行動など、表示する文面を設定します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encyclopedia", meta = (MultiLine = "true")) FText EncyclopediaTraits;
	/** 図鑑で表示する個体写真。未登録の素材をUI側で推測しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encyclopedia") TSoftObjectPtr<UTexture2D> EncyclopediaPhoto;
	/** 展示専用のIdleを指定します。歩行・攻撃AnimBPは展示で使用しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encyclopedia") TSoftObjectPtr<class UAnimSequence> ArchiveIdleAnimation;
};

/** 特別コインの固定IDと配置地点です。 */
USTRUCT(BlueprintType)
struct FSpecialCoinDefinition
{
	GENERATED_BODY()
	/** 取得記録に使う固定ID。例：Stage01_Coin_001。再挑戦・再起動でも変更せず、保存済みの取得IDは再出現させません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName SpecialCoinId;
	/** 配置先LevelのABoarSpawnPointに設定したID。例：CoinSpawn_A。コインの配置位置を安定したIDで参照します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName SpawnPointId;
};

/** 解放状態は保存せず、クリア済みStage IDから導出します。 */
USTRUCT(BlueprintType)
struct FStageUnlockCondition
{
	GENERATED_BODY()
	/** 解放に必要なクリア済みStageId。Noneなら初期解放。例：Stage02の解放条件にStage01を指定します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName RequiredClearedStageId;
};

/** 既存アセットとの互換用のClass・体数方式。個体IDを持つ新規設定にはFBoarSpawnDefinitionを使用します。 */
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

/**
 * Stage固有の静的設定をまとめるDataAssetです。ロビー表示、遷移先、出現個体、コイン、クリア目標、解放条件を設定します。
 * 挑戦中の状態はStageRunData、永続進行はGameInstance / SaveGameが管理します。
 */
UCLASS(BlueprintType)
class REBOARGETCH_API UStageConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** セーブ・解放判定に使う固定Stage ID。例：Stage01。表示名とは独立し、再挑戦・再起動でも変更しません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Identity")
	FName StageId;

	/** Stage選択UIに表示するステージ名。永続IDのStageIdとは別に設定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Display")
	FText DisplayName;

	/** ロビーUIへ表示するステージ説明です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Display", meta = (MultiLine = "true"))
	FText Description;

	/** Stage選択決定時に実際にロードするLevel。対応するステージのMapアセットを指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Travel")
	TSoftObjectPtr<UWorld> Level;

	/** ロビーUIへ表示する任意のサムネイルです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Display")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	/** Stage選択UI用の任意の軽量3DミニチュアClass。表示できない場合はThumbnailを代替表示に使用します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Display")
	TSoftClassPtr<ABoarStagePreviewActor> PreviewActorClass;

	/** Clearに必要な現在の檻収容数。累計捕獲回数ではなく、檻破壊による解放で減る現在数を判定します。0以下は判定無効。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Clear", meta = (ClampMin = "0"))
	int32 TargetCaptureCount = 0;

	/** 互換用の種類・体数設定。BoarSpawnDefinitionsが空の場合だけ使用します。両方が空なら自動生成しません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Spawn")
	TArray<FBoarSpawnEntry> BoarSpawnEntries;

	/** 個体ごとの固定ID・Class・出現地点を設定します。1要素が1体。設定時は旧BoarSpawnEntriesより優先します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Spawn")
	TArray<FBoarSpawnDefinition> BoarSpawnDefinitions;
	/** コインごとの固定ID・配置地点。取得済みIDは再出現させません。生成にはGameModeのSpecialCoinClassも設定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Spawn")
	TArray<FSpecialCoinDefinition> SpecialCoinDefinitions;
	/** 指定Stageのクリア状態による解放条件。RequiredClearedStageIdがNoneなら初期解放。捕獲数・コイン数は条件に含めません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Stage|Unlock")
	FStageUnlockCondition UnlockCondition;

	/** 設定上の出現数を返します。個体定義を優先し、空なら旧方式を集計します。Level上の地点の存在・ID重複は検証しません。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Stage|Spawn")
	int32 GetTotalBoarSpawnCount() const;
};
