#include "UI/BoarLoadoutWidget.h"
#include "UI/BoarLoadoutEntry.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Component/GadgetComponent.h"
#include "Core/BoarGameInstance.h"
#include "Gadget/GadgetBase.h"
#include "GadgetDataAsset.h"
#include "Player/BoarPlayerCharacter.h"

namespace
{
	const UGadgetDataAsset* Definition(TSubclassOf<AGadgetBase> Class)
	{
		return Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
	}
	FText GadgetName(TSubclassOf<AGadgetBase> Class)
	{
		if (!Class) return FText::FromString(TEXT("空き"));
		const auto* Def = Definition(Class);
		return Def && !Def->DisplayName.IsEmpty() ? Def->DisplayName : FText::FromString(TEXT("名称未設定"));
	}
}
void UBoarLoadoutWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!WidgetTree) return;
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
	const auto* Player = Cast<ABoarPlayerCharacter>(GetOwningPlayerPawn());
	Gadgets = Player ? Player->GetGadgetComponent() : nullptr;
	Progress = GetGameInstance<UBoarGameInstance>();
	if (!Gadgets || !Progress || !EntryClass || !SlotsPanel || !CandidatesPanel)
	{
		SetStatus(FText::FromString(TEXT("装備情報を取得できません。戻ってから開き直してください。")), true);
		return;
	}
	SlotsPanel->ClearChildren(); SlotEntries.Reset();
	for (int32 Index = 0; Index < UGadgetComponent::GetGadgetSlotCount(); ++Index)
	{
		auto* Entry = CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(), EntryClass);
		if (!Entry) continue;
		Entry->OnChosen.AddUniqueDynamic(this, &UBoarLoadoutWidget::ChooseSlot);
		Entry->OnFocused.AddUniqueDynamic(this, &UBoarLoadoutWidget::PreviewSlot);
		SlotsPanel->AddChild(Entry); SlotEntries.Add(Entry);
	}
	Gadgets->OnGadgetLoadoutChanged.AddUniqueDynamic(this, &UBoarLoadoutWidget::RefreshSlots);
	Progress->OnGadgetsUnlocked.AddUniqueDynamic(this, &UBoarLoadoutWidget::RebuildCandidates);
	RefreshSlots(); RebuildCandidates();
	SetStatus(FText::FromString(TEXT("スロットを決定し、装備候補を選んでください。")));
	bInitialFocusPending = true;
}
void UBoarLoadoutWidget::NativeDestruct()
{
	if (Gadgets) Gadgets->OnGadgetLoadoutChanged.RemoveDynamic(this, &UBoarLoadoutWidget::RefreshSlots);
	if (Progress) Progress->OnGadgetsUnlocked.RemoveDynamic(this, &UBoarLoadoutWidget::RebuildCandidates);
	Super::NativeDestruct();
}
void UBoarLoadoutWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	// World TimerはPause中に進まないため、最初のUI描画後に一度だけFocusを補います。
	if (bInitialFocusPending) { bInitialFocusPending = false; FocusInitialChoice(); }
}
void UBoarLoadoutWidget::FocusInitialChoice()
{
	if (SlotEntries.IsValidIndex(SelectedSlot)) SlotEntries[SelectedSlot]->FocusEntry();
	else FocusBack();
}
void UBoarLoadoutWidget::RefreshSlots()
{
	if (!Gadgets) return;
	for (int32 Index = 0; Index < SlotEntries.Num(); ++Index)
	{
		const FText Text = FText::Format(FText::FromString(TEXT("Slot {0}  /  {1}")), FText::AsNumber(Index + 1), GadgetName(Gadgets->GetGadgetSlotClass(Index)));
		SlotEntries[Index]->Setup(Index, Text, Index == SelectedSlot);
	}
}
void UBoarLoadoutWidget::RebuildCandidates()
{
	if (!Progress || !CandidatesPanel || !EntryClass) return;
	CandidatesPanel->ClearChildren(); CandidateEntries.Reset(); Candidates.Reset();
	// 空スロット操作を先頭に置き、実装済みCatalogの設定順を保ちます。
	Candidates.Add(nullptr);
	TSet<FName> Seen;
	for (const auto& Class : Progress->GadgetCatalog)
	{
		const auto* Def = Definition(Class);
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract) || !Def || Def->GadgetId.IsNone() || Seen.Contains(Def->GadgetId)) continue;
		Seen.Add(Def->GadgetId);
		if (Progress->IsGadgetUnlocked(Def->GadgetId)) Candidates.Add(Class);
	}
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		auto* Entry = CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(), EntryClass);
		if (!Entry) continue;
		Entry->Setup(Index, Index == 0 ? FText::FromString(TEXT("スロットを空にする")) : GadgetName(Candidates[Index]));
		Entry->OnChosen.AddUniqueDynamic(this, &UBoarLoadoutWidget::ChooseCandidate);
		Entry->OnFocused.AddUniqueDynamic(this, &UBoarLoadoutWidget::PreviewCandidate);
		CandidatesPanel->AddChild(Entry); CandidateEntries.Add(Entry);
	}
}
void UBoarLoadoutWidget::ChooseSlot(int32 Index)
{
	if (!SlotEntries.IsValidIndex(Index)) return;
	SelectedSlot = Index; RefreshSlots();
	// 空にする操作へ誤って即決定しないよう、装備中の候補を優先します。
	int32 CandidateIndex = Gadgets ? Candidates.IndexOfByKey(Gadgets->GetGadgetSlotClass(Index)) : 0;
	if (CandidateIndex == INDEX_NONE) CandidateIndex = 0;
	if (CandidateEntries.IsValidIndex(CandidateIndex)) CandidateEntries[CandidateIndex]->FocusEntry();
}
void UBoarLoadoutWidget::ChooseCandidate(int32 Index)
{
	if (!Gadgets || !Progress || !Candidates.IsValidIndex(Index)) return;
	const auto* Def = Definition(Candidates[Index]);
	if (Candidates[Index] && (!Def || !Progress->IsGadgetUnlocked(Def->GadgetId))) return;
	if (!Gadgets->SetGadgetSlot(SelectedSlot, Candidates[Index]))
	{
		SetStatus(FText::FromString(TEXT("装備を変更できませんでした。")), true); return;
	}
	RefreshSlots();
	const bool bSaved = Gadgets->WasLastLoadoutSaveSuccessful();
	SetStatus(FText::FromString(bSaved ? TEXT("装備を変更・保存しました。") : TEXT("装備は変更されましたが保存に失敗しました。同じ候補を選ぶと再試行します。")), !bSaved);
	FocusInitialChoice();
}
void UBoarLoadoutWidget::PreviewSlot(int32 Index) { if (Gadgets) ShowDetails(Gadgets->GetGadgetSlotClass(Index)); }
void UBoarLoadoutWidget::PreviewCandidate(int32 Index)
{
	if (!Candidates.IsValidIndex(Index)) return;
	ShowDetails(Candidates[Index]);
	if (auto* Scroll = Cast<UScrollBox>(CandidatesPanel))
		if (CandidateEntries.IsValidIndex(Index)) Scroll->ScrollWidgetIntoView(CandidateEntries[Index]);
}
void UBoarLoadoutWidget::ShowDetails(TSubclassOf<AGadgetBase> Class)
{
	const auto* Def = Definition(Class);
	if (DetailName) DetailName->SetText(GadgetName(Class));
	if (DetailRole) DetailRole->SetText(Def ? Def->RoleText : FText::GetEmpty());
	if (DetailDescription) DetailDescription->SetText(Def ? Def->Description : FText::FromString(TEXT("このスロットには何も装備しません。")));
	if (DetailIcon)
	{
		DetailIcon->SetBrushFromTexture(Def ? Def->DisplayIcon : nullptr);
		DetailIcon->SetVisibility(Def && Def->DisplayIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}
void UBoarLoadoutWidget::SetStatus(const FText& Text, bool bError)
{
	if (Status)
	{
		Status->SetText(Text);
		Status->SetColorAndOpacity(FSlateColor(bError ? FLinearColor(1.f, .2f, .15f) : FLinearColor(.6f, .9f, 1.f)));
	}
}
void UBoarLoadoutWidget::Close() { OnClosed.Broadcast(); }
void UBoarLoadoutWidget::FocusBack() { if (BackButton) BackButton->SetKeyboardFocus(); }
