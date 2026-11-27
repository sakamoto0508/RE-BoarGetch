#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BoarStageSelectPresenter.generated.h"

class UGameInstance;
class UBoarGameInstance;
class UStageConfig;
class UTexture2D;
class ABoarStagePreviewActor;

/** 一覧・詳細・カルーセル共通の表示データ。進捗の集計や開始可否の判断はPresenterで完結します。 */
struct FBoarStageSelectViewData
{
	/** 進行と表示を対応付けるステージID。 */
	FName StageId;
	/** ステージカードに表示する名前。 */
	FText DisplayName;
	/** 選択中のステージの説明文。 */
	FText Description;
	/** 一覧項目に表示するステージラベル。 */
	FText EntryLabel;
	/** ステージカードに表示する状態バッジの文言。 */
	FText Badge;
	/** 解放・クリアなどのステージ状態を示す文言。 */
	FText Status;
	/** 現在のステージの収集進捗を表示する文言。 */
	FText Progress;
	/** そのステージをクリアするための目標捕獲数。 */
	int32 TargetCaptureCount = 0;
	// 特別コインと図鑑（捕獲済み個体）の取得数／総数。Progressと同じ集計を個別表示用に分けて持ちます。
	/** このステージで取得済みの特別コイン数。 */
	int32 CoinsFound = 0;
	/** このステージに配置された特別コインの総数。 */
	int32 CoinsTotal = 0;
	/** このステージで図鑑に登録済みの個体数。 */
	int32 BoarsFound = 0;
	/** このステージの図鑑に登録できる個体の総数。 */
	int32 BoarsTotal = 0;
	/** 参照したステージ設定が有効か。 */
	bool bValid = false;
	/** ステージへの出発が解放済みか。 */
	bool bUnlocked = false;
	/** ステージをクリア済みか。 */
	bool bCleared = false;
	/** 現在表示中の中央ステージか。 */
	bool bSelected = false;
	/** 出発を受け付けられるステージか。 */
	bool bCanStart = false;
	/** ステージのサムネイルへのソフト参照。読み込みはViewが担当する。 */
	TSoftObjectPtr<UTexture2D> Thumbnail;
	/** Viewがミニチュアを生成するためのActorクラス。 */
	TSoftClassPtr<ABoarStagePreviewActor> PreviewActorClass;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStageSelectViewChanged);

/** ステージ選択のルールと表示データを担当します。WidgetやプレビューActorは保持しません。 */
UCLASS()
class REBOARGETCH_API UBoarStageSelectPresenter : public UObject
{
	GENERATED_BODY()

public:
	/** カタログと進捗通知を接続し、前回挑戦したステージを優先して初期選択します。 */
	void Initialize(UGameInstance* Instance, const TArray<UStageConfig*>& Stages, UStageConfig* FallbackStage);
	/** 画面を閉じる際に進捗通知の購読と選択状態を解除します。複数回呼び出せます。 */
	void Shutdown();
	virtual void BeginDestroy() override;
	/** 進捗や選択表示の更新をViewに通知する。 */
	UPROPERTY()
	FOnStageSelectViewChanged OnViewChanged;

	int32 GetStageCount() const { return Stages.Num(); }
	int32 GetSelectedIndex() const { return SelectedIndex; }
	bool IsTransitionPending() const { return PendingIndex != INDEX_NONE; }
	FBoarStageSelectViewData GetStage(int32 Index) const;
	FBoarStageSelectViewData GetSelectedStage() const { return GetStage(SelectedIndex); }
	FBoarStageSelectViewData GetCarouselStage(int32 Offset) const { return GetStage(SelectedIndex + Offset); }
	/** 一覧からの直接選択。ロック中のステージは選択できません。 */
	bool TrySelect(int32 Index);
	bool CanStep(int32 Direction, bool bCarousel) const;
	/** 一覧は即時選択、カルーセルはアニメーション後に確定する移動先を予約します。 */
	bool RequestStep(int32 Direction, bool bCarousel);
	void CommitPendingSelection();
	/** 開始可能な選択ステージを返します。保存処理やレベル遷移は呼び出し側の責務です。 */
	UStageConfig* RequestStart() const;
	bool IsPreviewVisible(FName StageId) const;

private:
	bool IsUnlocked(const UStageConfig* Stage) const;
	void HandleProgressChanged();

	/** ステージの解放と収集進捗を取得する参照。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarGameInstance> ProgressOwner;

	/** 選択画面に提示するステージ設定の一覧。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStageConfig>> Stages;

	/** 永続進行の変更通知を解除するハンドル。 */
	FDelegateHandle ProgressChangedHandle;
	// SelectedIndexは表示中の中央、PendingIndexはスライド完了後の中央です。
	/** 現在中央に表示しているステージの添字。 */
	int32 SelectedIndex = INDEX_NONE;
	/** スライド完了後に中央へ表示するステージの添字。 */
	int32 PendingIndex = INDEX_NONE;
};
