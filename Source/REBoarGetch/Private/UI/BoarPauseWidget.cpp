#include "UI/BoarPauseWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Player/BoarPlayerController.h"
#include "InputCoreTypes.h"

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
FReply UBoarPauseWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	const FKey Key = Event.GetKey();
	const auto* PC = Cast<ABoarPlayerController>(GetOwningPlayer());
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || (PC && PC->IsPauseMenuKey(Key)))
	{
		// 長押しリピートで閉じ直後に再表示しないよう、最初の押下だけ扱います。
		if (!Event.IsRepeat()) Resume();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
