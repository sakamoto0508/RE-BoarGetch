#include "UI/BoarHUDWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Gadget/GadgetBase.h"
#include "Component/GadgetComponent.h"
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
	for (int32 SlotIndex = 0; SlotIndex < UGadgetComponent::GetGadgetSlotCount(); ++SlotIndex)
	{
		const FName IconName = GadgetIconWidgetNames.IsValidIndex(SlotIndex)
			? GadgetIconWidgetNames[SlotIndex] : NAME_None;
		const FName SelectionName = GadgetSelectionWidgetNames.IsValidIndex(SlotIndex)
			? GadgetSelectionWidgetNames[SlotIndex] : NAME_None;
		GadgetIconWidgets.Add(WidgetTree ? Cast<UImage>(WidgetTree->FindWidget(IconName)) : nullptr);
		GadgetSelectionWidgets.Add(WidgetTree ? WidgetTree->FindWidget(SelectionName) : nullptr);
	}
}

void UBoarHUDWidget::UpdateGadgetSlots(
	const TArray<TSubclassOf<AGadgetBase>>& GadgetSlots,
	int32 SelectedSlotIndex)
{
	for (int32 SlotIndex = 0; SlotIndex < UGadgetComponent::GetGadgetSlotCount(); ++SlotIndex)
	{
		UTexture2D* DisplayIcon = nullptr;
		if (GadgetSlots.IsValidIndex(SlotIndex) && GadgetSlots[SlotIndex])
		{
			const AGadgetBase* GadgetDefaults = GadgetSlots[SlotIndex]->GetDefaultObject<AGadgetBase>();
			const UGadgetDataAsset* Definition = GadgetDefaults ? GadgetDefaults->GetGadgetDefinition() : nullptr;
			DisplayIcon = Definition ? Definition->DisplayIcon : nullptr;
		}

		if (GadgetIconWidgets.IsValidIndex(SlotIndex) && GadgetIconWidgets[SlotIndex])
		{
			GadgetIconWidgets[SlotIndex]->SetBrushFromTexture(DisplayIcon, true);
			GadgetIconWidgets[SlotIndex]->SetVisibility(
				DisplayIcon ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}

		if (GadgetSelectionWidgets.IsValidIndex(SlotIndex) && GadgetSelectionWidgets[SlotIndex])
		{
			GadgetSelectionWidgets[SlotIndex]->SetVisibility(
				SlotIndex == SelectedSlotIndex
					? ESlateVisibility::SelfHitTestInvisible
					: ESlateVisibility::Hidden);
		}
	}
}

void UBoarHUDWidget::UpdateCaptureCount(int32 CurrentCount, int32 TargetCount)
{
	if (CapturedCountText)
	{
		CapturedCountText->SetText(FText::AsNumber(CurrentCount));
	}

	if (TargetCountText)
	{
		TargetCountText->SetText(FText::AsNumber(TargetCount));
	}
}

void UBoarHUDWidget::UpdateHealth(float CurrentHealth, float MaxHealth)
{
	if (CurrentHealthText)
	{
		CurrentHealthText->SetText(FText::AsNumber(FMath::RoundToInt(CurrentHealth)));
	}

	if (MaxHealthText)
	{
		MaxHealthText->SetText(FText::AsNumber(FMath::RoundToInt(MaxHealth)));
	}
}
