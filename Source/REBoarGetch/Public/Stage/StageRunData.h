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

	/** 今回挑戦しているステージのID。 */
	UPROPERTY(BlueprintReadOnly)
	FName StageId;

	/** 今回捕獲成功したIDの履歴（重複なし）。解放された個体も残るため、これだけを根拠に図鑑登録しません。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FName> CapturedBoarUniqueIds;

	/** 今回の各個体の最終捕獲順（捕獲成功イベントの通し番号。同じフレームの同時捕獲は同じ番号）。再捕獲で更新します。 */
	UPROPERTY(BlueprintReadOnly)
	TMap<FName, int32> LastCaptureOrderByBoar;

	/** Clear確定時点でCageに収容されていたID（ID昇順）。図鑑登録・NEW・最近捕獲順の更新の共通入力です。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FName> HousedBoarUniqueIds;

	/** HousedBoarUniqueIdsをClear確定時に取得済みか。未取得のRunからは個体を登録しません。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHousedSnapshotTaken = false;

	/** 今回の挑戦で取得した特別コインIDの集合。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FName> SpecialCoinIds;

	/** 今回の挑戦で新たに図鑑へ登録された個体IDの一覧。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FName> NewBoarUniqueIds;

	/** 今回の挑戦で新たに取得済みとなった特別コインIDの一覧。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FName> NewSpecialCoinIds;

	/** 現在檻に収容している個体数。 */
	UPROPERTY(BlueprintReadOnly)
	int32 CurrentHousedCount = 0;

	/** プレイ中に加算し、クリア確定時に固定する挑戦時間（ゲーム時間の秒）。 */
	UPROPERTY(BlueprintReadOnly)
	float ClearTimeSeconds = 0.0f;

	/** 結果を確定し挑戦データの更新を停止したか。 */
	UPROPERTY(BlueprintReadOnly)
	bool bResultFrozen = false;

	/** Clear時の収容個体を、今回の最終捕獲が古い順（同じ順位はID降順）に並べます。
	 *  最近捕獲順の履歴へこの順で末尾追加すると、新しい順の表示で同時捕獲がID昇順になります。 */
	TArray<FName> GetHousedIdsOldestFirst() const
	{
		TArray<FName> Ids = HousedBoarUniqueIds;
		Ids.Sort([this](const FName A, const FName B)
		{
			const int32 OA = LastCaptureOrderByBoar.FindRef(A), OB = LastCaptureOrderByBoar.FindRef(B);
			return OA != OB ? OA < OB : B.LexicalLess(A);
		});
		return Ids;
	}
};

/** 同一フレームに集まった終了要求を優先順で解決する、副作用のない判定です。 */
inline EBoarStageState ResolveBoarStageEnd(EBoarStageState Current, bool bClear, bool bGameOver)
{
	if (Current != EBoarStageState::Playing)
		return Current;
	return bClear ? EBoarStageState::Cleared : (bGameOver ? EBoarStageState::GameOver : Current);
}
