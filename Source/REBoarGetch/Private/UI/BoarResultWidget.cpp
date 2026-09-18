#include "UI/BoarResultWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Input/Reply.h"

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
	SetIsFocusable(true);
}

void UBoarResultWidget::ResolveWidgetReferences()
{
	CapturedCountText = ResolveNamedWidget<UTextBlock>(WidgetTree, CapturedCountTextWidgetName);
	TargetCountText = ResolveNamedWidget<UTextBlock>(WidgetTree, TargetCountTextWidgetName);

	// 新仕様ではリザルト上に操作Buttonを表示せず、画面全体で任意入力を受け付ける。
	// 既存WBPに旧Retry／Title Buttonが残っていても表示されないよう一括で隠す。
	if (WidgetTree)
	{
		TArray<UWidget*> AllWidgets;
		WidgetTree->GetAllWidgets(AllWidgets);
		for (UWidget* Widget : AllWidgets)
		{
			if (UButton* Button = Cast<UButton>(Widget))
			{
				Button->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	if (!CapturedCountText)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: CapturedCountTextWidgetName is unset or does not reference a TextBlock."), *GetName());
	}

	if (!TargetCountText)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: TargetCountTextWidgetName is unset or does not reference a TextBlock."), *GetName());
	}
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

void UBoarResultWidget::FocusForDismissInput()
{
	SetUserFocus(GetOwningPlayer());
}

FReply UBoarResultWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	(void)InGeometry;
	(void)InKeyEvent;
	return RequestDismiss();
}

FReply UBoarResultWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	(void)InGeometry;
	(void)InMouseEvent;
	return RequestDismiss();
}

FReply UBoarResultWidget::RequestDismiss()
{
	OnDismissRequested.Broadcast();
	return FReply::Handled();
}
