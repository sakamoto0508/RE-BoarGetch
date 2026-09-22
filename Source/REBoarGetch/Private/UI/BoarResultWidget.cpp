#include "UI/BoarResultWidget.h"

#include "Components/VerticalBox.h"
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

void UBoarResultWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// WBP未設定・生成失敗時だけ使用する最小表示。既存WBPのTreeは変更しません。
	if (GetClass() != StaticClass() || !WidgetTree || WidgetTree->RootWidget) return;
	auto* Panel = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Panel;
	auto* Guide = WidgetTree->ConstructWidget<UTextBlock>();
	Guide->SetText(FText::FromString(TEXT("Stage Clear - Press any button to return to Lobby")));
	Panel->AddChildToVerticalBox(Guide);
	CapturedCountTextWidgetName = TEXT("FallbackCapturedCount");
	TargetCountTextWidgetName = TEXT("FallbackTargetCount");
	Panel->AddChildToVerticalBox(WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), CapturedCountTextWidgetName));
	Panel->AddChildToVerticalBox(WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TargetCountTextWidgetName));
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

	if (!CapturedCountText)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: CapturedCountTextWidgetName is unset or does not reference a TextBlock."), *GetName());
	}

	if (!TargetCountText)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: TargetCountTextWidgetName is unset or does not reference a TextBlock."), *GetName());
	}
}

void UBoarResultWidget::FocusForDismissInput()
{
	SetUserFocus(GetOwningPlayer());
}

void UBoarResultWidget::InitializeResult(const FStageRunData& Run, int32 TargetCount)
{
	if (!Run.bResultFrozen) return;
	ResultData = Run;
	// 目標数はRunDataに含まれないStage設定値。現在収容数は確定Runだけを参照します。
	if (TargetCountText) TargetCountText->SetText(FText::AsNumber(TargetCount));
	if (CapturedCountText) CapturedCountText->SetText(FText::AsNumber(Run.CurrentHousedCount));
	if (UTextBlock* Text = ResolveNamedWidget<UTextBlock>(WidgetTree, ClearTimeTextWidgetName))
		Text->SetText(FText::AsTimespan(FTimespan::FromSeconds(Run.ClearTimeSeconds)));
	if (UTextBlock* Text = ResolveNamedWidget<UTextBlock>(WidgetTree, NewBoarCountTextWidgetName))
		Text->SetText(FText::AsNumber(Run.NewBoarUniqueIds.Num()));
	if (UTextBlock* Text = ResolveNamedWidget<UTextBlock>(WidgetTree, SpecialCoinCountTextWidgetName))
		Text->SetText(FText::AsNumber(Run.SpecialCoinIds.Num()));
	OnRunResultUpdated();
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
