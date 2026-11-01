#include "UI/BoarArchiveSelectionWidget.h"
#include "UI/BoarArchiveCard.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Core/BoarGameInstance.h"
#include "Save/BoarSaveGame.h"
#include "Stage/StageConfig.h"
#include "Boar/BoarBase.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "ArchiveSelection"

namespace
{
	// 捕獲済み候補グリッドの列数です。展示枠数とは別の値です。
	constexpr int32 GridColumns = 3;

	const FBoarSpawnDefinition* Definition(const TArray<TObjectPtr<UStageConfig>>& Stages, FName Id,
	                                       const UStageConfig*& FoundStage)
	{
		FoundStage = nullptr;
		if (Id.IsNone())return nullptr;
		for (const auto& S : Stages)if (S)for (const auto& D : S->BoarSpawnDefinitions)if (D.BoarUniqueId == Id)
		{
			FoundStage = S;
			return &D;
		}
		return nullptr;
	}
}

UBoarArchiveSelectionWidget::UBoarArchiveSelectionWidget(const FObjectInitializer& O) : Super(O)
{
	SetIsFocusable(true);
}

TSharedRef<SWidget> UBoarArchiveSelectionWidget::RebuildWidget()
{
	// WBPのレイアウトとBindWidgetOptionalの参照をそのまま使います。C++で代替レイアウトは作りません。
	if (WidgetTree && !WidgetTree->RootWidget)
		UE_LOG(LogTemp, Error, TEXT("[Archive] %s: WBP has no layout (RootWidget is empty). Archive Display Selection is not displayed."),
		       *GetNameSafe(GetClass()));
	return Super::RebuildWidget();
}

// 保存済みの展示枠を表示用に取り込み、最近の捕獲順を優先して候補を並べます。
void UBoarArchiveSelectionWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!IsReadyForDisplay())
	{
		UE_LOG(LogTemp, Error, TEXT("[Archive] %s: Status / SlotsPanel / Grid / BackButton が見つかりません。"),
		       *GetNameSafe(GetClass()));
		return;
	}
	if (SetButton)SetButton->OnClicked.AddUniqueDynamic(this, &UBoarArchiveSelectionWidget::Confirm);
	if (RemoveButton)RemoveButton->OnClicked.AddUniqueDynamic(this, &UBoarArchiveSelectionWidget::RemoveBoar);
	BackButton->OnClicked.AddUniqueDynamic(this, &UBoarArchiveSelectionWidget::Cancel);
	auto* GI = GetGameInstance<UBoarGameInstance>();
	PendingIds = GI ? GI->GetArchiveDisplayIds() : TArray<FName>();
	PendingIds.SetNum(UBoarSaveGame::ArchiveDisplaySlotCount);
	ActiveSlot = 0;
	FocusedCandidate = INDEX_NONE;
	Candidates.Reset();
	SlotRows.Reset();
	CandidateRows.Reset();
	SlotsPanel->ClearChildren();
	Grid->ClearChildren();
	if (const auto* Save = GI ? GI->GetProgress() : nullptr)
	{
		for (int32 I = Save->CapturedBoarHistory.Num() - 1; I >= 0; --I)
		{
			const FName Id = Save->CapturedBoarHistory[I];
			if (!Id.IsNone() && Save->CapturedBoarUniqueIds.Contains(Id))Candidates.AddUnique(Id);
		}
		// 古いセーブで捕獲順のないIDは名前順で補います。表示順のために履歴を書き換えません。
		auto Remaining = Save->CapturedBoarUniqueIds.Array();
		Remaining.Sort([](FName A, FName B) { return A.LexicalLess(B); });
		for (FName Id : Remaining)if (!Id.IsNone())Candidates.AddUnique(Id);
	}
	// カードもWBPで作成します。未設定時はC++のカードで代替せず、Errorを出してカードを並べません。
	const TSubclassOf<UBoarLoadoutEntry> CardClass = EntryClass;
	if (!CardClass)
		UE_LOG(LogTemp, Error, TEXT("[Archive] %s: EntryClass is unset. Archive cards are not displayed."),
		       *GetNameSafe(GetClass()));
	for (int32 I = 0; CardClass && I < UBoarSaveGame::ArchiveDisplaySlotCount; ++I)
	{
		auto* C = CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(), CardClass);
		if (!C)continue;
		C->OnChosen.AddUniqueDynamic(this, &UBoarArchiveSelectionWidget::SelectSlot);
		SlotsPanel->AddChildToHorizontalBox(C)->SetPadding(FMargin(0, 0, 8, 0));
		SlotRows.Add(C);
	}
	for (int32 I = 0; CardClass && I < Candidates.Num(); ++I)
	{
		auto* C = CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(), CardClass);
		if (!C)continue;
		C->Setup(I, LabelFor(Candidates[I]));
		C->OnChosen.AddUniqueDynamic(this, &UBoarArchiveSelectionWidget::ChooseBoar);
		C->OnFocused.AddUniqueDynamic(this, &UBoarArchiveSelectionWidget::ChooseBoar);
		Grid->AddChildToUniformGrid(C, I / GridColumns, I % GridColumns);
		CandidateRows.Add(C);
	}
	SetStatus(Candidates.IsEmpty()
		          ? LOCTEXT("NoCandidates", "捕獲済みのボアがいません")
		          : LOCTEXT("ChoosePrompt", "ボアを選び、展示に設定してください。"));
	RefreshSlotRows();
	RefreshDetail();
	bInitialFocusPending = true;
}

void UBoarArchiveSelectionWidget::SetStatus(const FText& Message, bool bError)
{
	if (!Status)return;
	Status->SetColorAndOpacity(FSlateColor(bError ? ErrorColor : StatusColor));
	Status->SetText(Message);
}

FText UBoarArchiveSelectionWidget::LabelFor(FName Id) const
{
	if (Id.IsNone())return LOCTEXT("Empty", "EMPTY");
	const UStageConfig* S;
	const auto* D = Definition(UBoarGameInstance::ResolveStageCatalog(this, StageCatalog), Id, S);
	return D && !D->EncyclopediaName.IsEmpty() ? D->EncyclopediaName : FText::FromName(Id);
}

void UBoarArchiveSelectionWidget::UpdatePortrait(UBoarLoadoutEntry* Entry, FName Id, const FText& Badge, bool bActive)
{
	if (auto* Card = Cast<UBoarArchiveCard>(Entry))
	{
		const UStageConfig* S;
		const auto* D = Definition(UBoarGameInstance::ResolveStageCatalog(this, StageCatalog), Id, S);
		auto* Texture = D ? D->EncyclopediaPhoto.LoadSynchronous() : nullptr;
		Card->SetPortrait(Texture, Id.IsNone() ? LOCTEXT("Empty", "EMPTY") : LOCTEXT("NoPhoto", "写真未登録"), Badge,
		                  bActive);
	}
}

void UBoarArchiveSelectionWidget::RefreshSlotRows()
{
	for (int32 I = 0; I < SlotRows.Num(); ++I)
	{
		SlotRows[I]->Setup(I, LabelFor(PendingIds[I]), ActiveSlot == I);
		UpdatePortrait(SlotRows[I], PendingIds[I], FText::Format(LOCTEXT("SlotBadge", "展示{0}"), I + 1),
		               ActiveSlot == I);
	}
	for (int32 I = 0; I < CandidateRows.Num(); ++I)
	{
		const int32 DisplayIndex = PendingIds.Find(Candidates[I]);
		FText Badge = DisplayIndex == INDEX_NONE
			              ? FText::GetEmpty()
			              : FText::Format(LOCTEXT("DisplayBadge", "DISPLAY {0}"), DisplayIndex + 1);
		UpdatePortrait(CandidateRows[I], Candidates[I], Badge, false);
	}
	if (SetButton)SetButton->SetIsEnabled(Candidates.IsValidIndex(FocusedCandidate));
}

void UBoarArchiveSelectionWidget::RefreshDetail()
{
	const FName Id = Candidates.IsValidIndex(FocusedCandidate) ? Candidates[FocusedCandidate] : NAME_None;
	const UStageConfig* S;
	const auto* D = Definition(UBoarGameInstance::ResolveStageCatalog(this, StageCatalog), Id, S);
	FText Value = Id.IsNone() ? LOCTEXT("SelectPrompt", "ボアを選択してください") : LabelFor(Id);
	if (D)
	{
		const FText Type = D->BoarClass
			                   ? StaticEnum<EBoarArchetype>()->GetDisplayNameTextByValue(
				                   static_cast<int64>(D->BoarClass.GetDefaultObject()->GetBoarArchetype()))
			                   : LOCTEXT("Unregistered", "未登録");
		const FText StageName = S->DisplayName.IsEmpty() ? FText::FromName(S->StageId) : S->DisplayName;
		Value = FText::Format(LOCTEXT("DetailFormat", "{0}\n種類：{1}\n初捕獲Stage：{2}"), Value, Type, StageName);
		if (!D->EncyclopediaDescription.IsEmpty())Value = FText::Format(
			LOCTEXT("DetailDescription", "{0}\n{1}"), Value,
			FText::FromString(D->EncyclopediaDescription.ToString().Left(140)));
	}
	else if (!Id.IsNone())Value = FText::Format(LOCTEXT("DetailMissing", "{0}\n個体情報未登録"), Value);
	auto* Texture = D ? D->EncyclopediaPhoto.LoadSynchronous() : nullptr;
	if (DetailPhoto)
	{
		DetailPhoto->SetBrushFromTexture(Texture);
		DetailPhoto->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (DetailText)DetailText->SetText(Value);
	if (SetButton)SetButton->SetIsEnabled(!Id.IsNone());
}

void UBoarArchiveSelectionWidget::SelectSlot(int32 Index)
{
	if (PendingIds.IsValidIndex(Index))
	{
		ActiveSlot = Index;
		RefreshSlotRows();
	}
}

void UBoarArchiveSelectionWidget::ChooseBoar(int32 Index)
{
	if (Candidates.IsValidIndex(Index))
	{
		FocusedCandidate = Index;
		RefreshDetail();
	}
}

// 重複展示の調整と保存はGameInstanceへ依頼します。成功後に保存側の確定値を読み直します。
void UBoarArchiveSelectionWidget::AssignBoar(FName Id)
{
	if (!PendingIds.IsValidIndex(ActiveSlot))return;
	if (!Id.IsNone() && PendingIds[ActiveSlot] == Id)
	{
		SetStatus(LOCTEXT("AlreadyDisplayed", "この枠には選択中のボアが展示されています。"));
		return;
	}
	auto* GI = GetGameInstance<UBoarGameInstance>();
	if (!GI || !GI->AssignArchiveDisplay(ActiveSlot, Id))
	{
		SetStatus(LOCTEXT("SaveFailed", "保存できませんでした。展示は変更されていません。"), true);
		return;
	}
	PendingIds = GI->GetArchiveDisplayIds();
	RefreshSlotRows();
	SetStatus(LOCTEXT("Saved", "展示を更新・保存しました。"));
}

void UBoarArchiveSelectionWidget::Confirm()
{
	if (Candidates.IsValidIndex(FocusedCandidate))AssignBoar(Candidates[FocusedCandidate]);
}

void UBoarArchiveSelectionWidget::RemoveBoar() { AssignBoar(NAME_None); }
void UBoarArchiveSelectionWidget::Cancel() { OnClosed.Broadcast(); }

void UBoarArchiveSelectionWidget::FocusInitialChoice()
{
	if (!CandidateRows.IsEmpty())CandidateRows[0]->FocusEntry();
	else if (BackButton)BackButton->SetKeyboardFocus();
}

void UBoarArchiveSelectionWidget::NativeTick(const FGeometry& G, float D)
{
	Super::NativeTick(G, D);
	if (bInitialFocusPending)
	{
		bInitialFocusPending = false;
		FocusInitialChoice();
	}
}

// 3列グリッドの候補へフォーカスを移し、選択中のカードが見えるようにスクロールします。
void UBoarArchiveSelectionWidget::MoveGridFocus(int32 Offset)
{
	if (CandidateRows.IsEmpty())return;
	int32 Index = FMath::Clamp((FocusedCandidate == INDEX_NONE ? 0 : FocusedCandidate) + Offset, 0,
	                           CandidateRows.Num() - 1);
	CandidateRows[Index]->FocusEntry();
	if (GridScroll)GridScroll->ScrollWidgetIntoView(CandidateRows[Index], true, EDescendantScrollDestination::IntoView);
}

FReply UBoarArchiveSelectionWidget::NativeOnPreviewKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	const FKey K = E.GetKey();
	if (K == EKeys::Escape || K == EKeys::Gamepad_FaceButton_Bottom)
	{
		Cancel();
		return FReply::Handled();
	}
	if (K == EKeys::E || K == EKeys::Gamepad_FaceButton_Right)
	{
		if (!E.IsRepeat())Confirm();
		return FReply::Handled();
	}
	if (K == EKeys::Delete || K == EKeys::Gamepad_FaceButton_Left)
	{
		if (!E.IsRepeat())RemoveBoar();
		return FReply::Handled();
	}
	if (K == EKeys::One || K == EKeys::Two || K == EKeys::Three)
	{
		SelectSlot(K == EKeys::One ? 0 : K == EKeys::Two ? 1 : 2);
		return FReply::Handled();
	}
	if (K == EKeys::Gamepad_LeftShoulder || K == EKeys::Gamepad_RightShoulder)
	{
		SelectSlot(
			(ActiveSlot + (K == EKeys::Gamepad_RightShoulder ? 1 : UBoarSaveGame::ArchiveDisplaySlotCount - 1)) %
			UBoarSaveGame::ArchiveDisplaySlotCount);
		return FReply::Handled();
	}
	int32 Move = 0;
	if (K == EKeys::Right || K == EKeys::D || K == EKeys::Gamepad_DPad_Right)Move = 1;
	if (K == EKeys::Left || K == EKeys::A || K == EKeys::Gamepad_DPad_Left)Move = -1;
	if (K == EKeys::Down || K == EKeys::S || K == EKeys::Gamepad_DPad_Down)Move = GridColumns;
	if (K == EKeys::Up || K == EKeys::W || K == EKeys::Gamepad_DPad_Up)Move = -GridColumns;
	if (Move)
	{
		MoveGridFocus(Move);
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(G, E);
}

// スティックの閾値と実時間の待ち間隔で移動を制限し、フレームごとの過剰なフォーカス移動を抑えます。
FReply UBoarArchiveSelectionWidget::NativeOnAnalogValueChanged(const FGeometry& G, const FAnalogInputEvent& E)
{
	if (E.GetKey() == EKeys::Gamepad_LeftX || E.GetKey() == EKeys::Gamepad_LeftY)
	{
		if (FMath::Abs(E.GetAnalogValue()) > 0.6f && FPlatformTime::Seconds() >= NextAnalogNavigation)
		{
			const int32 Sign = E.GetAnalogValue() > 0 ? 1 : -1;
			MoveGridFocus(E.GetKey() == EKeys::Gamepad_LeftX ? Sign : -Sign * GridColumns);
			NextAnalogNavigation = FPlatformTime::Seconds() + 0.18;
		}
		return FReply::Handled();
	}
	return Super::NativeOnAnalogValueChanged(G, E);
}

FReply UBoarArchiveSelectionWidget::NativeOnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	return Super::NativeOnKeyDown(G, E);
}

#undef LOCTEXT_NAMESPACE
