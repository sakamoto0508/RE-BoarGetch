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
	UPROPERTY(SaveGame, BlueprintReadOnly) TSet<FName> CapturedBoarUniqueIds;
	UPROPERTY(SaveGame, BlueprintReadOnly) TSet<FName> SpecialCoinIds;
	UPROPERTY(SaveGame, BlueprintReadOnly) TSet<FName> ClearedStageIds;
	UPROPERTY(SaveGame, BlueprintReadOnly) TSet<FName> UnlockedGadgetIds;
	UPROPERTY(SaveGame, BlueprintReadOnly) TArray<FName> GadgetLoadout;
	UPROPERTY(SaveGame, BlueprintReadOnly) FName LastAttemptedStageId;
	UPROPERTY(SaveGame, BlueprintReadOnly) bool bHasSavedLoadout = false;
};
