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
	FName StageId;
	FText DisplayName;
	FText Description;
	FText EntryLabel;
	FText Badge;
	FText Status;
	FText Progress;
	int32 TargetCaptureCount = 0;
	bool bValid = false;
	bool bUnlocked = false;
	bool bCleared = false;
	bool bSelected = false;
	bool bCanStart = false;
	// アセットの読み込みとプレビューActorの生成・破棄はViewが担当します。
	TSoftObjectPtr<UTexture2D> Thumbnail;
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
	// 進捗更新後にViewへ再描画を要求します。表示値はGetStageで最新状態から取得します。
	UPROPERTY() FOnStageSelectViewChanged OnViewChanged;
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
	UPROPERTY(Transient) TObjectPtr<UBoarGameInstance> ProgressOwner;
	UPROPERTY(Transient) TArray<TObjectPtr<UStageConfig>> Stages;
	FDelegateHandle ProgressChangedHandle;
	// SelectedIndexは表示中の中央、PendingIndexはスライド完了後の中央です。
	int32 SelectedIndex = INDEX_NONE;
	int32 PendingIndex = INDEX_NONE;
};
