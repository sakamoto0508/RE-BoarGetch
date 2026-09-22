#include "UI/BoarPauseWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Player/BoarPlayerController.h"

void UBoarPauseWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ResumeButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(ResumeButtonName)) : nullptr;
	LobbyButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(LobbyButtonName)) : nullptr;
	if (ResumeButton)
	{
		ResumeButton->OnClicked.AddUniqueDynamic(this, &UBoarPauseWidget::Resume);
		ResumeButton->OnHovered.AddUniqueDynamic(this, &UBoarPauseWidget::FocusResume);
	}
	if (LobbyButton)
	{
		LobbyButton->OnClicked.AddUniqueDynamic(this, &UBoarPauseWidget::LeaveStage);
		LobbyButton->OnHovered.AddUniqueDynamic(this, &UBoarPauseWidget::FocusLobby);
	}
}

void UBoarPauseWidget::FocusInitialChoice() { FocusResume(); }
void UBoarPauseWidget::FocusResume() { if (ResumeButton) ResumeButton->SetKeyboardFocus(); }
void UBoarPauseWidget::FocusLobby() { if (LobbyButton) LobbyButton->SetKeyboardFocus(); }
void UBoarPauseWidget::Resume()
{
	if (auto* PC = Cast<ABoarPlayerController>(GetOwningPlayer())) PC->ResumeFromPause();
}
void UBoarPauseWidget::LeaveStage()
{
	if (auto* PC = Cast<ABoarPlayerController>(GetOwningPlayer())) PC->LeaveStageFromPause();
}
