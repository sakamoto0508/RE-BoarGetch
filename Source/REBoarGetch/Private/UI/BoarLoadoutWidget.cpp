#include "UI/BoarLoadoutWidget.h"
#include "UI/BoarLoadoutEntry.h"
#include "UI/BoarLoadoutPresenter.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"

// 名前指定のWidgetを接続し、Presenterの表示データと通知から装備画面を組み立てます。
void UBoarLoadoutWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!WidgetTree)
		return;
	SlotsPanel = Cast<UPanelWidget>(WidgetTree->FindWidget(SlotsPanelName));
	CandidatesPanel = Cast<UPanelWidget>(WidgetTree->FindWidget(CandidatesPanelName));
	DetailName = Cast<UTextBlock>(WidgetTree->FindWidget(DetailNameWidgetName));
	DetailRole = Cast<UTextBlock>(WidgetTree->FindWidget(DetailRoleWidgetName));
	DetailDescription = Cast<UTextBlock>(WidgetTree->FindWidget(DetailDescriptionWidgetName));
	DetailIcon = Cast<UImage>(WidgetTree->FindWidget(DetailIconWidgetName));
	Status = Cast<UTextBlock>(WidgetTree->FindWidget(StatusWidgetName));
	BackButton = Cast<UButton>(WidgetTree->FindWidget(BackButtonName));
	if (BackButton)
	{
		BackButton->OnClicked.AddUniqueDynamic(this, &UBoarLoadoutWidget::Close);
		BackButton->OnHovered.AddUniqueDynamic(this, &UBoarLoadoutWidget::FocusBack);
	}
	Presenter = NewObject<UBoarLoadoutPresenter>(this);
	const bool bReady = Presenter->Initialize(GetOwningPlayer(), bRequestedLabContext);
	if (!bReady || !EntryClass || !SlotsPanel || !CandidatesPanel)
	{
		SetStatus(FText::FromString(TEXT("装備情報を取得できません。戻ってから開き直してください。")), true);
		return;
	}

	SlotsPanel->ClearChildren();
	SlotEntries.Reset();
	LabModeEntries.Reset();
	for (int32 Index = 0; Index < Presenter->GetSlotCount(); ++Index)
	{
		auto *Entry = CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(), EntryClass);
		if (!Entry)
			continue;
		Entry->OnChosen.AddUniqueDynamic(this, &UBoarLoadoutWidget::ChooseSlot);
		Entry->OnFocused.AddUniqueDynamic(this, &UBoarLoadoutWidget::PreviewSlot);
		SlotsPanel->AddChild(Entry);
		SlotEntries.Add(Entry);
		if (Presenter->IsLabContext())
			Entry->UseCompactLabRow();
	}
	Presenter->OnSlotsChanged.AddUniqueDynamic(this, &UBoarLoadoutWidget::RefreshSlots);
	if (Presenter->IsLabContext())
	{
		const TCHAR *Labels[] = {TEXT("装備を変更"), TEXT("ガジェットを解放"), TEXT("Test対象を選択")};
		for (int32 I = 0; I < 3; ++I)
		{
			auto *E = CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(), EntryClass);
			if (!E)
				continue;
			SlotsPanel->AddChild(E);
			E->Setup(I, FText::FromString(Labels[I]), I == 0);
			E->UseCompactLabRow();
			E->OnChosen.AddUniqueDynamic(this, &UBoarLoadoutWidget::ChooseLabMode);
			LabModeEntries.Add(E);
		}
	}
	Presenter->OnCandidatesChanged.AddUniqueDynamic(this, &UBoarLoadoutWidget::RebuildCandidates);
	RefreshSlots();
	RebuildCandidates();
	SetStatus(FText::FromString(TEXT("スロットを決定し、装備候補を選んでください。")));
	bInitialFocusPending = true;
}
// 画面破棄時はViewへの通知とPresenterのモデル購読を解除します。
void UBoarLoadoutWidget::NativeDestruct()
{
	if (Presenter)
	{
		Presenter->OnSlotsChanged.RemoveDynamic(this, &UBoarLoadoutWidget::RefreshSlots);
		Presenter->OnCandidatesChanged.RemoveDynamic(this, &UBoarLoadoutWidget::RebuildCandidates);
		Presenter->Shutdown();
		Presenter = nullptr;
	}
	Super::NativeDestruct();
}
void UBoarLoadoutWidget::NativeTick(const FGeometry &Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	// World TimerはPause中に進まないため、最初のUI描画後に一度だけFocusを補います。
	if (bInitialFocusPending)
	{
		bInitialFocusPending = false;
		FocusInitialChoice();
	}
}
void UBoarLoadoutWidget::FocusInitialChoice()
{
	if (Presenter && SlotEntries.IsValidIndex(Presenter->GetSelectedSlot()))
		SlotEntries[Presenter->GetSelectedSlot()]->FocusEntry();
	else
		FocusBack();
}
void UBoarLoadoutWidget::RefreshSlots()
{
	if (!Presenter)
		return;
	for (int32 Index = 0; Index < SlotEntries.Num(); ++Index)
	{
		const auto Data = Presenter->GetSlot(Index);
		SlotEntries[Index]->Setup(Index, Data.Label, Data.bSelected);
	}
}
void UBoarLoadoutWidget::RebuildCandidates()
{
	if (!Presenter || !CandidatesPanel || !EntryClass)
		return;
	CandidatesPanel->ClearChildren();
	CandidateEntries.Reset();
	Presenter->RebuildCatalog();
	for (int32 Index = 0; Index < Presenter->GetCandidateCount(); ++Index)
	{
		auto *Entry = CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(), EntryClass);
		CandidateEntries.Add(Entry);
		if (!Entry)
			continue;
		const auto Data = Presenter->GetCandidate(Index);
		Entry->Setup(Index, Data.Label, Data.bSelected);
		Entry->OnChosen.AddUniqueDynamic(this, &UBoarLoadoutWidget::ChooseCandidate);
		Entry->OnFocused.AddUniqueDynamic(this, &UBoarLoadoutWidget::PreviewCandidate);
		CandidatesPanel->AddChild(Entry);
	}
}
void UBoarLoadoutWidget::ChooseSlot(int32 Index)
{
	if (!Presenter || !SlotEntries.IsValidIndex(Index))
		return;
	if (Presenter->IsLabContext() && Presenter->GetLabMode() != 0)
		ChooseLabMode(0);
	const int32 CandidateIndex = Presenter->ChooseSlot(Index);
	RefreshSlots();
	if (CandidateEntries.IsValidIndex(CandidateIndex) && CandidateEntries[CandidateIndex])
		CandidateEntries[CandidateIndex]->FocusEntry();
}
// 操作結果に従って表示とフォーカスを更新します。解放条件や保存結果の判断はPresenterに任せます。
void UBoarLoadoutWidget::ChooseCandidate(int32 Index)
{
	if (!Presenter)
		return;
	const auto Result = Presenter->ChooseCandidate(Index);
	if (Result.bRefreshCandidates)
		RebuildCandidates();
	if (Result.Status.IsEmpty())
		return;
	SetStatus(Result.Status, Result.bError);
	if (Result.bFocusSlots)
	{
		RefreshSlots();
		FocusInitialChoice();
	}
	else if (Presenter->GetLabMode() != 0)
		if (CandidateEntries.IsValidIndex(Index) && CandidateEntries[Index])
			CandidateEntries[Index]->FocusEntry();
}
void UBoarLoadoutWidget::ChooseLabMode(int32 Index)
{
	if (!Presenter || !Presenter->ChooseLabMode(Index))
		return;
	const TCHAR *Labels[] = {TEXT("装備を変更"), TEXT("ガジェットを解放"), TEXT("Test対象を選択")};
	for (int32 I = 0; I < LabModeEntries.Num(); ++I)
		LabModeEntries[I]->Setup(I, FText::FromString(Labels[I]), I == Index);
	RebuildCandidates();
	SetStatus(Presenter->GetLabModeStatus());
	if (CandidateEntries.Num() > 1 && CandidateEntries[1])
		CandidateEntries[1]->FocusEntry();
}
void UBoarLoadoutWidget::PreviewSlot(int32 Index)
{
	if (Presenter)
		ShowDetails(Presenter->GetSlot(Index));
}
// フォーカス中の候補を詳細欄へ反映し、スクロール範囲内に収めます。装備変更は決定操作で行います。
void UBoarLoadoutWidget::PreviewCandidate(int32 Index)
{
	if (!Presenter || Index < 0 || Index >= Presenter->GetCandidateCount())
		return;
	ShowDetails(Presenter->GetCandidate(Index));
	if (auto *Scroll = Cast<UScrollBox>(CandidatesPanel))
		if (CandidateEntries.IsValidIndex(Index) && CandidateEntries[Index])
			Scroll->ScrollWidgetIntoView(CandidateEntries[Index]);
}
void UBoarLoadoutWidget::ShowDetails(const FBoarLoadoutEntryViewData &Data)
{
	if (DetailName)
		DetailName->SetText(Data.Name);
	if (DetailRole)
		DetailRole->SetText(Data.Role);
	if (DetailDescription)
		DetailDescription->SetText(Data.Description);
	if (DetailIcon)
	{
		DetailIcon->SetBrushFromTexture(Data.Icon);
		DetailIcon->SetVisibility(Data.Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}
void UBoarLoadoutWidget::SetStatus(const FText &Text, bool bError)
{
	if (Status)
	{
		Status->SetText(Text);
		Status->SetColorAndOpacity(FSlateColor(bError ? FLinearColor(1.f, .2f, .15f) : FLinearColor(.6f, .9f, 1.f)));
	}
}
void UBoarLoadoutWidget::Close()
{
	OnClosed.Broadcast();
}
void UBoarLoadoutWidget::FocusBack()
{
	if (BackButton)
		BackButton->SetKeyboardFocus();
}
