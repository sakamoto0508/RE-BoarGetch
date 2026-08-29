#include "UI/BoarResultWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"

namespace
{
	template <typename WidgetType>
	WidgetType* ResolveNamedWidget(UWidgetTree* WidgetTree, const FName WidgetName)
	{
		if (!WidgetTree || WidgetName.IsNone())
		{
			return nullptr;
		}

		return Cast<WidgetType>(WidgetTree->FindWidget(WidgetName));
	}
}

void UBoarResultWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ResolveWidgetReferences();

	if (RetryButton)
	{
		RetryButton->OnClicked.AddUniqueDynamic(this, &UBoarResultWidget::HandleRetryClicked);
	}

	if (TitleButton)
	{
		TitleButton->OnClicked.AddUniqueDynamic(this, &UBoarResultWidget::HandleTitleClicked);
	}
}

void UBoarResultWidget::ResolveWidgetReferences()
{
	CapturedCountText = ResolveNamedWidget<UTextBlock>(WidgetTree, CapturedCountTextWidgetName);
	TargetCountText = ResolveNamedWidget<UTextBlock>(WidgetTree, TargetCountTextWidgetName);
	RetryButton = ResolveNamedWidget<UButton>(WidgetTree, RetryButtonWidgetName);
	TitleButton = ResolveNamedWidget<UButton>(WidgetTree, TitleButtonWidgetName);

	if (!CapturedCountText)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: CapturedCountTextWidgetName is unset or does not reference a TextBlock."), *GetName());
	}

	if (!TargetCountText)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: TargetCountTextWidgetName is unset or does not reference a TextBlock."), *GetName());
	}

	if (!RetryButton)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: RetryButtonWidgetName is unset or does not reference a Button."), *GetName());
	}
}

void UBoarResultWidget::NativeDestruct()
{
	if (RetryButton)
	{
		RetryButton->OnClicked.RemoveDynamic(this, &UBoarResultWidget::HandleRetryClicked);
	}

	if (TitleButton)
	{
		TitleButton->OnClicked.RemoveDynamic(this, &UBoarResultWidget::HandleTitleClicked);
	}

	Super::NativeDestruct();
}

void UBoarResultWidget::InitializeResult(int32 CapturedCount, int32 TargetCount)
{
	if (CapturedCountText)
	{
		CapturedCountText->SetText(FText::AsNumber(CapturedCount));
	}

	if (TargetCountText)
	{
		TargetCountText->SetText(FText::AsNumber(TargetCount));
	}
}

void UBoarResultWidget::FocusInitialControl()
{
	if (RetryButton)
	{
		RetryButton->SetUserFocus(GetOwningPlayer());
	}
}

void UBoarResultWidget::HandleRetryClicked()
{
	OnRetryRequested.Broadcast();
}

void UBoarResultWidget::HandleTitleClicked()
{
	OnTitleRequested.Broadcast();
}
