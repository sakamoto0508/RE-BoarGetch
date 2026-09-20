#pragma once

#include "CoreMinimal.h"
#include "StageRunData.generated.h"

/** 挑戦の終了状態です。開始演出の完了まではPreparingで待機します。 */
UENUM(BlueprintType)
enum class EBoarStageState : uint8
{
	Preparing,
	Playing,
	Cleared,
	GameOver
};

/** 今回の挑戦だけを保持します。NEWは保存前に確定し、Result表示中は変更しません。 */
USTRUCT(BlueprintType)
struct REBOARGETCH_API FStageRunData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName StageId;
	UPROPERTY(BlueprintReadOnly) TArray<FName> CapturedBoarUniqueIds;
	UPROPERTY(BlueprintReadOnly) TArray<FName> SpecialCoinIds;
	UPROPERTY(BlueprintReadOnly) TArray<FName> NewBoarUniqueIds;
	UPROPERTY(BlueprintReadOnly) TArray<FName> NewSpecialCoinIds;
	UPROPERTY(BlueprintReadOnly) int32 CurrentHousedCount = 0;
	UPROPERTY(BlueprintReadOnly) float ClearTimeSeconds = 0.0f;
	UPROPERTY(BlueprintReadOnly) bool bResultFrozen = false;
};

/** 同一フレームに集まった終了要求を優先順で解決する、副作用のない判定です。 */
inline EBoarStageState ResolveBoarStageEnd(EBoarStageState Current, bool bClear, bool bGameOver)
{
	if (Current != EBoarStageState::Playing) return Current;
	return bClear ? EBoarStageState::Cleared : (bGameOver ? EBoarStageState::GameOver : Current);
}
