#include "UI/BoarHUDWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Gadget/GadgetBase.h"
#include "Gadget/GadgetDataAsset.h"

namespace
{
	UTextBlock* ResolveHUDTextBlock(UWidgetTree* WidgetTree, const FName WidgetName)
	{
		if (!WidgetTree || WidgetName.IsNone())
		{
			return nullptr;
		}

		return Cast<UTextBlock>(WidgetTree->FindWidget(WidgetName));
	}
}

void UBoarHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ResolveWidgetReferences();
}

void UBoarHUDWidget::ResolveWidgetReferences()
{
	CapturedCountText = ResolveHUDTextBlock(WidgetTree, CapturedCountTextWidgetName);
	TargetCountText = ResolveHUDTextBlock(WidgetTree, TargetCountTextWidgetName);
	CurrentHealthText = ResolveHUDTextBlock(WidgetTree, CurrentHealthTextWidgetName);
	MaxHealthText = ResolveHUDTextBlock(WidgetTree, MaxHealthTextWidgetName);

	GadgetIconWidgets.Reset();
	GadgetSelectionWidgets.Reset();
	for (int32 SlotIndex = 0; SlotIndex < 4; ++SlotIndex)
	{
		const FName IconName = GadgetIconWidgetNames.IsValidIndex(SlotIndex)
			? GadgetIconWidgetNames[SlotIndex] : NAME_None;
		const FName SelectionName = GadgetSelectionWidgetNames.IsValidIndex(SlotIndex)
			? GadgetSelectionWidgetNames[SlotIndex] : NAME_None;
		GadgetIconWidgets.Add(WidgetTree ? Cast<UImage>(WidgetTree->FindWidget(Ico