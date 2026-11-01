#include "UI/BoarInitializationErrorWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

// WBPのMessage/Retry/Lobbyを名前で解決して通知へ接続します。C++で代替レイアウトは作りません。
void UBoarInitializationErrorWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	Message = WidgetTree ? Cast<UTextBlock>(WidgetTree->FindWidget(MessageTextWidgetName)) : nullptr;
	RetryButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(RetryButtonWidgetName)) : nullptr;
	LobbyButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(LobbyButtonWidgetName)) : nullptr;
	if (RetryButton) RetryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Retry);
	if (LobbyButton) LobbyButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Lobby);
	if (!Message || !RetryButton || !LobbyButton)
		UE_LOG(LogTemp, Error, TEXT("[WorldInit] %s: Configure MessageTextWidgetName / RetryButtonWidgetName / LobbyButtonWidgetName."),
		       *GetNameSafe(GetClass()));
}

void UBoarInitializationErrorWidget::NativeDestruct()
{
	if (RetryButton) RetryButton->OnClicked.RemoveDynamic(this, &ThisClass::Retry);
	if (LobbyButton) LobbyButton->OnClicked.RemoveDynamic(this, &ThisClass::Lobby);
	Super::NativeDestruct();
}

void UBoarInitializationErrorWidget::ShowError(const FText& Reason)
{
	if (Message)
		Message->SetText(FText::Format(MessageFormat, Reason));
	if (RetryButton) RetryButton->SetUserFocus(GetOwningPlayer());
}
