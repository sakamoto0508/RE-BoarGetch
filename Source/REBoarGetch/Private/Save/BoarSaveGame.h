#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Stage/StageRunData.h"
#include "BoarSaveGame.generated.h"

/** 確定済み進行と即時保存する装備を保持します。挑戦中のHPや収容数は保存しません。 */
UCLASS()
class REBOARGETCH_API UBoarSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	/** 保存前のスナップショットとの差分を確定します。ディスクへの書き込みは行いません。 */
	void FreezeRunResult(FStageRunData& Run) const;
	/** 確定済みClear結果だけを候補データへ併合します。 */
	bool MergeClearedRun(const FStageRunData& Run);
	/** 捕獲済みIDまたは空欄の3枠を検証し、展示選択だけを変更します。 */
	bool SetArchiveDisplays(const TArray<FName>& Ids);
	/** 明示選択を優先し、未選択なら確定捕獲履歴から最新3体を返します。 */
	TArray<FName> ResolveArchiveDisplays() const;
	/** 末尾が直近の確定捕獲です。古いセーブのSetから順序を推測しません。 */
	UPROPERTY(SaveGame, BlueprintReadOnly) TArray<FName> CapturedBoarHistory;
	UPROPERTY(SaveGame, BlueprintReadOnly) TArray<FName> ArchiveDisplayBoarIds;
	UPROPERTY(SaveGame, BlueprintReadOnly) bool bHasSavedArchiveDisplays = false;
	UPROPERTY(SaveGame, BlueprintReadOnly) TSet<FName> CapturedBoarUniqueIds;
	UPROPERTY(SaveGame, BlueprintReadOnly) TSet<FName> SpecialCoinIds;
	UPROPERTY(SaveGame, BlueprintReadOnly) TSet<FName> ClearedStageIds;
	UPROPERTY(SaveGame, BlueprintReadOnly) TSet<FName> UnlockedGadgetIds;
	UPROPERTY(SaveGame, BlueprintReadOnly) TArray<FName> GadgetLoadout;
	UPROPERTY(SaveGame, BlueprintReadOnly) FName LastAttemptedStageId;
	UPROPERTY(SaveGame, BlueprintReadOnly) bool bHasSavedLoadout = false;
};
