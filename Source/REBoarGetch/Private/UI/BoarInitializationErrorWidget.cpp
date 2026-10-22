#include "UI/BoarInitializationErrorWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

void UBoarInitializationErrorWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Root;
	auto* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(.01f, .02f, .04f, .98f));
	Panel->SetPadding(FMargin(32));
	auto* CanvasSlot = Root->AddChildToCanvas(Panel);
	CanvasSlot->SetAnchors(FAnchors(.5f));
	CanvasSlot->SetAlignment(FVector2D(.5f));
	CanvasSlot->SetAutoSize(true);
	auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Column);
	Message = WidgetTree->ConstructWidget<UTextBlock>();
	Message->SetWrapTextAt(640.f);
	Column->AddChildToVerticalBox(Message);
	RetryButton = WidgetTree->ConstructWidget<UButton>();
	auto* RetryLabel = WidgetTree->ConstructWidget<UTextBlock>();
	RetryLabel->SetText(NSLOCTEXT("WorldInit", "Retry", "もう一度読み込む"));
	RetryButton->SetContent(RetryLabel);
	RetryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Retry);
	Column->AddChildToVerticalBox(RetryButton);
	auto* LobbyButton = WidgetTree->ConstructWidget<UButton>();
	auto* LobbyLabel = WidgetTree->ConstructWidget<UTextBlock>();
	LobbyLabel->SetText(NSLOCTEXT("WorldInit", "Lobby", "ロビーへ戻る"));
	LobbyButton->SetContent(LobbyLabel);
	LobbyButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Lobby);
	Column->AddChildToVerticalBox(LobbyButton);
}

void UBoarInitializationErrorWidget::ShowError(const FText& Reason)
{
	if (Message) Message->SetText(FText::Format(
		NSLOCTEXT("WorldInit", "Failure", "ゲームを開始できませんでした。\n\n{0}\n"), Reason));
	if (RetryButton) RetryButton->SetUserFocus(GetOwningPlayer());
}
