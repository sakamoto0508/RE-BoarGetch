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
	/** Archiveの展示枠数です。展示枠を扱う処理はすべてこの値を使います。 */
	static constexpr int32 ArchiveDisplaySlotCount = 3;
	/** 捕獲済みIDまたは空欄の展示枠を検証し、展示選択だけを変更します。 */
	bool SetArchiveDisplays(const TArray<FName>& Ids);
	/** 別枠展示中の個体はSwap。空欄も明示選択として保存します。 */
	bool AssignArchiveDisplay(int32 Slot, FName Id);
	/** 明示選択を優先し、未選択なら最近捕獲順の先頭から展示枠数分を返します。 */
	TArray<FName> ResolveArchiveDisplays() const;
	/** 登録済み個体を最近捕獲順（新しい順）に返します。CapturedBoarHistoryにない旧セーブの個体は古い履歴としてID昇順で末尾に並べます。 */
	TArray<FName> GetRecentCapturedBoarIds() const;
	/** 最近捕獲順。末尾が直近で、位置がそのまま単調増加の順序です（Clear時に収容中だった個体だけ末尾へ移動）。
	 *  同時捕獲はID降順で並べ、新しい順に読むとID昇順になります。古いセーブのSetから順序を推測しません。 */
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
