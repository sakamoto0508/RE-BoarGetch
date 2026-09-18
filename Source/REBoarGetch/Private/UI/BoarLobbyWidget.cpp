#include "UI/BoarLobbyWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Stage/StageConfig.h"

namespace
{
	template <typename TWidget>
	TWidget* ResolveLobbyWidget(UWidgetTree* WidgetTree, const FName WidgetName)
	{
		return WidgetTree && !WidgetName.IsNone()
			? Cast<TWidget>(WidgetTree->FindWidget(WidgetName))
			: nullptr;
	}
}

void UBoarLobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ResolveWidgetReferences();

	if (StartStageButton)
	{
		StartStageButton->OnClicked.AddUniqueDynamic(this, &UBoarLobbyWidget::HandleStartStageClicked);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.AddUniqueDynamic(this, &UBoarLobbyWidget::HandleCancelClicked);
	}
}

void UBoarLobbyWidget::NativeDestruct()
{
	if (StartStageButton)
	{
		StartStageButton->OnClicked.RemoveDynamic(this, &UBoarLobbyWidget::HandleStartStageClicked);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.RemoveDynamic(this, &UBoarLobbyWidget::HandleCancelClicked);
	}

	Super::NativeDestruct();
}

void UBoarLobbyWidget::ShowStageSelection(UStageConfig* StageConfig)
{
	SelectedStageConfig = StageConfig;
	SetVisibility(ESlateVisibility::Visible);

	if (StageNameText)
	{
		StageNameText->SetText(StageConfig ? StageConfig->DisplayName : FText::GetEmpty());
	}
	if (StageDescriptionText)
	{
		StageDescriptionText->SetText(StageConfig ? StageConfig->Description : FText::GetEmpty());
	}
	if (TargetCaptureCountText)
	{
		TargetCaptureCountText->SetText(StageConfig
			? FText::AsNumber(StageConfig->TargetCaptureCount)
			: FText::GetEmpty());
	}
	if (StageThumbnailImage)
	{
		UTexture2D* Thumbnail = StageConfig ? StageConfig->Thumbnail.LoadSynchronous() : nullptr;
		StageThumbnailImage->SetBrushFromTexture(Thumbnail, true);
		StageThumbnailImage->SetVisibility(
			Thumbnail ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (StartStageButton)
	{
		StartStageButton->SetIsEnabled(StageConfig && !StageConfig->Level.IsNull());
		StartStageButton->SetKeyboardFocus();
	}
}

void UBoarLobbyWidget::HideStageSelection()
{
	SelectedStageConfig = nullptr;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UBoarLobbyWidget::ResolveWidgetReferences()
{
	StageNameText = ResolveLobbyWidget<UTextBlock>(WidgetTree, StageNameTextWidgetName);
	StageDescriptionText = ResolveLobbyWidget<UTextBlock>(WidgetTree, StageDescriptionTextWidgetName);
	TargetCaptureCountText = ResolveLobbyWidget<UTextBlock>(WidgetTree, TargetCaptureCountTextWidgetName);
	StageThumbnailImage = ResolveLobbyWidget<UImage>(WidgetTree, StageThumbnailImageWidgetName);
	StartStageButton = ResolveLobbyWidget<UButton>(WidgetTree, StartStageButtonWidgetName);
	CancelButton = ResolveLobbyWidget<UButton>(WidgetTree, CancelButtonWidgetName);
}

void UBoarLobbyWidget::HandleStartStageClicked()
{
	if (SelectedStageConfig && !SelectedStageConfig->Level.IsNull())
	{
		OnStageStartRequested.Broadcast(SelectedStageConfig);
	}
}

void UBoarLobbyWidget::HandleCancelClicked()
{
	HideStageSelection();
	OnStageSelectionClosed.Broadcast();
}
