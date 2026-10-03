#include "UI/BoarStageSelectPresenter.h"
#include "Core/BoarGameInstance.h"
#include "Save/BoarSaveGame.h"
#include "Stage/StageConfig.h"

void UBoarStageSelectPresenter::Initialize(UGameInstance* Instance, const TArray<UStageConfig*>& Catalog, UStageConfig* FallbackStage)
{
	// 再表示時にも購読が重複しないよう、前回の接続を先に解除します。
	Shutdown();
	ProgressOwner = Cast<UBoarGameInstance>(Instance);
	if (ProgressOwner)
		ProgressChangedHandle = ProgressOwner->OnProgressChanged.AddUObject(this, &UBoarStageSelectPresenter::HandleProgressChanged);
	// 無効な定義を除外し、同じStageIdは最初の定義を採用して元の並び順を保ちます。
	TSet<FName> Seen;
	for (UStageConfig* Stage : Catalog)
	{
		if (!Stage || Stage->StageId.IsNone() || Seen.Contains(Stage->StageId)) continue;
		Seen.Add(Stage->StageId);
		Stages.Add(Stage);
	}
	// 初期選択の優先順位は「前回挑戦 → カタログ内のFallback → 最初の解放済み」です。
	const auto* Save = ProgressOwner ? ProgressOwner->GetProgress() : nullptr;
	for (int32 I = 0; I < Stages.Num(); ++I)
		if (Save && Stages[I]->StageId == Save->LastAttemptedStageId && IsUnlocked(Stages[I])) SelectedIndex = I;
	if (SelectedIndex == INDEX_NONE && IsUnlocked(FallbackStage)) SelectedIndex = Stages.IndexOfByKey(FallbackStage);
	if (SelectedIndex == INDEX_NONE)
		for (int32 I = 0; I < Stages.Num(); ++I) if (IsUnlocked(Stages[I])) { SelectedIndex = I; break; }
}

void UBoarStageSelectPresenter::Shutdown()
{
	if (ProgressOwner) ProgressOwner->OnProgressChanged.Remove(ProgressChangedHandle);
	ProgressChangedHandle.Reset();
	ProgressOwner = nullptr;
	Stages.Reset();
	SelectedIndex = PendingIndex = INDEX_NONE;
}

void UBoarStageSelectPresenter::BeginDestroy()
{
	Shutdown();
	Super::BeginDestroy();
}

// 通知時に進捗をキャッシュせず、Viewの再取得で保存済みの最新状態を反映します。
void UBoarStageSelectPresenter::HandleProgressChanged() { OnViewChanged.Broadcast(); }

bool UBoarStageSelectPresenter::IsUnlocked(const UStageConfig* Stage) const
{
	return Stage && (ProgressOwner ? ProgressOwner->IsStageUnlocked(Stage) : Stage->UnlockCondition.RequiredClearedStageId.IsNone());
}

FBoarStageSelectViewData UBoarStageSelectPresenter::GetStage(int32 Index) const
{
	FBoarStageSelectViewData Data;
	Data.Status = NSLOCTEXT("StageSelect", "NoStage", "選択できるステージがありません");
	if (!Stages.IsValidIndex(Index)) return Data;
	const UStageConfig* Stage = Stages[Index];
	const auto* Save = ProgressOwner ? ProgressOwner->GetProgress() : nullptr;
	Data.bValid = true;
	Data.StageId = Stage->StageId;
	Data.DisplayName = Stage->DisplayName;
	Data.Description = Stage->Description;
	Data.TargetCaptureCount = Stage->TargetCaptureCount;
	Data.Thumbnail = Stage->Thumbnail;
	Data.PreviewActorClass = Stage->PreviewActorClass;
	Data.bUnlocked = IsUnlocked(Stage);
	Data.bCleared = Save && Save->ClearedStageIds.Contains(Stage->StageId);
	Data.bSelected = Index == SelectedIndex;
	// スライド中は表示中と移動先が異なるため、確定するまで開始操作を無効にします。
	Data.bCanStart = Data.bUnlocked && !Stage->Level.IsNull() && !IsTransitionPending();
	Data.Badge = FText::FromString(!Data.bUnlocked ? TEXT("LOCKED") : Data.bCleared ? TEXT("CLEAR") : TEXT(""));
	const FText State = FText::FromString(!Data.bUnlocked ? TEXT("LOCKED") : Data.bCleared ? TEXT("CLEAR") : TEXT("未クリア"));
	Data.EntryLabel = FText::Format(NSLOCTEXT("StageSelect", "Entry", "{0}\n{1}"),
		Stage->DisplayName.IsEmpty() ? FText::FromName(Stage->StageId) : Stage->DisplayName, State);
	Data.Status = Stage->Level.IsNull() ? NSLOCTEXT("StageSelect", "NoLevel", "出発先未登録")
		: Data.bCleared ? FText::FromString(TEXT("CLEAR")) : NSLOCTEXT("StageSelect", "NotClear", "未クリア");
	// ステージに定義された有効なIDだけを数え、重複IDや他ステージの取得履歴を集計から除きます。
	TSet<FName> Coins, Boars;
	for (const auto& Def : Stage->SpecialCoinDefinitions) if (!Def.SpecialCoinId.IsNone()) Coins.Add(Def.SpecialCoinId);
	for (const auto& Def : Stage->BoarSpawnDefinitions) if (!Def.BoarUniqueId.IsNone()) Boars.Add(Def.BoarUniqueId);
	int32 CoinsFound = 0, BoarsFound = 0;
	for (FName Id : Coins) if (Save && Save->SpecialCoinIds.Contains(Id)) ++CoinsFound;
	for (FName Id : Boars) if (Save && Save->CapturedBoarUniqueIds.Contains(Id)) ++BoarsFound;
	const FText BoarProgress = Boars.IsEmpty() ? NSLOCTEXT("StageSelect", "NoIndividuals", "個体データ未登録")
		: FText::Format(NSLOCTEXT("StageSelect", "Count", "{0} / {1}"), BoarsFound, Boars.Num());
	Data.Progress = FText::Format(NSLOCTEXT("StageSelect", "Progress", "特別コイン：{0} / {1}\n図鑑：{2}"), CoinsFound, Coins.Num(), BoarProgress);
	return Data;
}

bool UBoarStageSelectPresenter::TrySelect(int32 Index)
{
	if (!Stages.IsValidIndex(Index) || !IsUnlocked(Stages[Index])) return false;
	SelectedIndex = Index;
	return true;
}

bool UBoarStageSelectPresenter::CanStep(int32 Direction, bool bCarousel) const
{
	if (Direction != -1 && Direction != 1) return false;
	if (bCarousel) return Direction < 0 ? SelectedIndex > 0 : SelectedIndex + 1 < Stages.Num();
	int32 UnlockedCount = 0;
	for (const auto& Stage : Stages) if (IsUnlocked(Stage)) ++UnlockedCount;
	return UnlockedCount > 1;
}

bool UBoarStageSelectPresenter::RequestStep(int32 Direction, bool bCarousel)
{
	if (Direction != -1 && Direction != 1) return false;
	if (bCarousel)
	{
		// カルーセルではロック中のカードも閲覧できます。開始可否は別途GetStageで判定します。
		// 連続入力時は前の移動先を確定してから、次のスライドを予約します。
		CommitPendingSelection();
		const int32 Next = SelectedIndex + Direction;
		if (!Stages.IsValidIndex(Next)) return false;
		PendingIndex = Next;
		return true;
	}
	// 一覧は端で折り返し、ロック中の項目を飛ばして解放済みの項目へ移動します。
	const int32 Count = Stages.Num();
	for (int32 Step = 1; Step <= Count; ++Step)
	{
		const int32 Index = (FMath::Max(SelectedIndex, 0) + Direction * Step + Count) % Count;
		if (TrySelect(Index)) return true;
	}
	return false;
}

void UBoarStageSelectPresenter::CommitPendingSelection()
{
	if (PendingIndex == INDEX_NONE) return;
	SelectedIndex = PendingIndex;
	PendingIndex = INDEX_NONE;
}

UStageConfig* UBoarStageSelectPresenter::RequestStart() const
{
	return GetSelectedStage().bCanStart ? Stages[SelectedIndex].Get() : nullptr;
}

bool UBoarStageSelectPresenter::IsPreviewVisible(FName StageId) const
{
	// 中央と左右の隣接カードだけがプレビューの保持対象です。Actorの管理はViewへ任せます。
	const int32 Index = Stages.IndexOfByPredicate([StageId](const auto& Stage) { return Stage->StageId == StageId; });
	return Index != INDEX_NONE && FMath::Abs(Index - SelectedIndex) <= 1;
}
