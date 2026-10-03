#include "UI/BoarPauseWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Player/BoarPlayerController.h"

// BPで配置したボタンへ操作とホバー時のフォーカスを接続します。
void UBoarPauseWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SettingsButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(SettingsButtonName)) : nullptr;
	if (SettingsButton)
	{
		SettingsButton->OnClicked.AddUniqueDynamic(this, &UBoarPauseWidget::OpenSettings);
		SettingsButton->OnHovered.AddUniqueDynamic(this, &UBoarPauseWidget::FocusSettings);
	}
	ResumeButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(ResumeButtonName)) : nullptr;
	LobbyButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(LobbyButtonName)) : nullptr;
	LoadoutButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(LoadoutButtonName)) : nullptr;
	if (LoadoutButton)
	{
		LoadoutButton->OnClicked.AddUniqueDynamic(this, &UBoarPauseWidget::OpenLoadout);
		LoadoutButton->OnHovered.AddUniqueDynamic(this, &UBoarPauseWidget::FocusLoadout);
	}
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
void UBoarPauseWidget::FocusLobbyChoice() { if (LobbyButton) FocusLobby(); else FocusResume(); }
void UBoarPauseWidget::FocusSettings() { if (SettingsButton) SettingsButton->SetKeyboardFocus(); }
void UBoarPauseWidget::OpenSettings()
{
	if (auto* PC = Cast<ABoarPlayerController>(GetOwningPlayer())) PC->OpenSettingsMenu(this, SettingsButton);
}
void UBoarPauseWidget::FocusLoadoutChoice() { if (LoadoutButton) FocusLoadout(); else FocusResume(); }
void UBoarPauseWidget::FocusLoadout() { if (LoadoutButton) LoadoutButton->SetKeyboardFocus(); }
void UBoarPauseWidget::OpenLoadout()
{
	if (auto* PC = Cast<ABoarPlayerController>(GetOwningPlayer())) PC->OpenLoadoutMenu();
}
void UBoarPauseWidget::FocusResume() { if (ResumeButton) ResumeButton->SetKeyboardFocus(); }
void UBoarPauseWidget::FocusLobby() { if (LobbyButton) LobbyButton->SetKeyboardFocus(); }
// 時間停止と入力復帰の処理はController経由でUIFlowへ依頼します。
void UBoarPauseWidget::Resume()
{
	if (auto* PC = Cast<ABoarPlayerController>(GetOwningPlayer())) PC->ResumeFromPause();
}
// 途中退出の確認画面を要求します。このボタンの処理では直接レベル遷移しません。
void UBoarPauseWidget::LeaveStage()
{
	if (auto* PC = Cast<ABoarPlayerController>(GetOwningPlayer())) PC->LeaveStageFromPause();
}
