#include "UI/BoarGameOverWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"

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
