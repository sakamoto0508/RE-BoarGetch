#include "UI/BoarLoadoutEntry.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"

void UBoarLoadoutEntry::NativeConstruct()
{
	Super::NativeConstruct();
	ActionButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(ButtonName)) : nullptr;
	Label = WidgetTree ? Cast<UTextBlock>(WidgetTree->FindWidget(LabelName)) : nullptr;
	SelectionImage = WidgetTree ? Cast<UImage>(WidgetTree->FindWidget(SelectionImageName)) : nullptr;
	LockedIndicator = WidgetTree ? WidgetTree->FindWidget(LockedIndicatorWidgetName) : nullptr;
	if (ActionButton)
	{
		ActionButton->OnClicked.AddUniqueDynamic(this, &UBoarLoadoutEntry::Choose);
		ActionButton->OnHovered.AddUniqueDynamic(this, &UBoarLoadoutEntry::Hover);
	}
	Refresh();
}
void UBoarLoadoutEntry::Setup(int32 InIndex, const FText& Text, bool bSelected)
{
	EntryIndex = InIndex; EntryText = Text; bEntrySelected = bSelected; Refresh();
}
void UBoarLoadoutEntry::Refresh()
{
	if (ActionButton) ActionButton->SetIsEnabled(bEntryAvailable);
	if (LockedIndicator) LockedIndicator->SetVisibility(bEntryAvailable ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (Label) Label->SetText(EntryText);
	if (SelectionImage) SelectionImage->SetVisibility(bEntrySelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
}
void UBoarLoadoutEntry::FocusEntry() { if (ActionButton) ActionButton->SetKeyboardFocus(); }
void UBoarLoadoutEntry::Hover() { FocusEntry(); }
void UBoarLoadoutEntry::Choose() { if (bEntryAvailable) OnChosen.Broadcast(EntryIndex); }
void UBoarLoadoutEntry::SetAvailable(bool bAvailable) { bEntryAvailable = bAvailable; Refresh(); }
void UBoarLoadoutEntry::NativeOnAddedToFocusPath(const FFocusEvent& Event)
{
	Super::NativeOnAddedToFocusPath(Event);
	OnFocused.Broadcast(EntryIndex);
}
