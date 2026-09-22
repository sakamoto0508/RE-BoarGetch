#include "UI/BoarGameOverWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

void UBoarGameOverWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 終了後はActor停止を維持し、Retry/Lobbyを選べる最小UIを用意します。
	if (GetClass() != StaticClass() || !WidgetTree || WidgetTree->RootWidget) return;
	auto* Panel = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Panel;
	auto* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(FText::FromString(TEXT("Game Over")));
	Panel->AddChildToVerticalBox(Title);
	RetryButtonWidgetName = TEXT("FallbackRetry");
	LobbyButtonWidgetName = TEXT("FallbackLobby");
	for (const FName Name : {RetryButtonWidgetName, LobbyButtonWidgetName})
	{
		auto* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Name == RetryButtonWidgetName ? TEXT("Retry") : TEXT("Lobby")));
		Button->AddChild(Label);
		Panel->AddChildToVerticalBox(Button);
	}
}

void UBoarGameOverWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RetryButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(RetryButtonWidgetName)) : nullptr;
	LobbyButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(LobbyButtonWidgetName)) : nullptr;
	if (RetryButton) RetryButton->OnClicked.AddUniqueDynamic(this, &UBoarGameOverWidget::RequestRetry);
	if (LobbyButton) LobbyButton->OnClicked.AddUniqueDynamic(this, &UBoarGameOverWidget::RequestLobby);
	if (!RetryButton || !LobbyButton) UE_LOG(LogTemp, Warning, TEXT("[GameOver] Configure Retry/Lobby widget references."));
}

void UBoarGameOverWidget::NativeDestruct()
{
	if (RetryButton) RetryButton->OnClicked.RemoveDynamic(this, &UBoarGameOverWidget::RequestRetry);
	if (LobbyButton) LobbyButton->OnClicked.RemoveDynamic(this, &UBoarGameOverWidget::RequestLobby);
	Super::NativeDestruct();
}

void UBoarGameOverWidget::FocusInitialChoice()
{
	if (RetryButton) RetryButton->SetUserFocus(GetOwningPlayer());
}

void UBoarGameOverWidget::RequestRetry() { OnRetryRequested.Broadcast(); }
void UBoarGameOverWidget::RequestLobby() { OnLobbyRequested.Broadcast(); }
