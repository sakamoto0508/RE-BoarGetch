#include "UI/BoarConfirmationWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"

void UBoarConfirmationWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bInitialFocusPending = true;
	bDecisionSent = false;
	YesButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(YesButtonName)) : nullptr;
	NoButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(NoButtonName)) : nullptr;
	if (YesButton)
	{
		YesButton->OnClicked.AddUniqueDynamic(this, &UBoarConfirmationWidget::Confirm);
		YesButton->OnHovered.AddUniqueDynamic(this, &UBoarConfirmationWidget::FocusYes);
	}
	if (NoButton)
	{
		NoButton->OnClicked.AddUniqueDynamic(this, &UBoarConfirmationWidget::Cancel);
		NoButton->OnHovered.AddUniqueDynamic(this, &UBoarConfirmationWidget::FocusNo);
	}
}

void UBoarConfirmationWidget::NativeDestruct()
{
	if (YesButton)
	{
		YesButton->OnClicked.RemoveDynamic(this, &UBoarConfirmationWidget::Confirm);
		YesButton->OnHovered.RemoveDynamic(this, &UBoarConfirmationWidget::FocusYes);
	}
	if (NoButton)
	{
		NoButton->OnClicked.RemoveDynamic(this, &UBoarConfirmationWidget::Cancel);
		NoButton->OnHovered.RemoveDynamic(this, &UBoarConfirmationWidget::FocusNo);
	}
	Super::NativeDestruct();
}

void UBoarConfirmationWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (!bInitialFocusPending) return;
	bInitialFocusPending = false;
	FocusInitialChoice();
}

// 初期フォーカスは否定側に置き、決定キーの持ち越しによる承認を避けやすくします。
void UBoarConfirmationWidget::FocusInitialChoice() { FocusNo(); }
void UBoarConfirmationWidget::FocusYes() { if (YesButton) YesButton->SetKeyboardFocus(); }
void UBoarConfirmationWidget::FocusNo() { if (NoButton) NoButton->SetKeyboardFocus(); }
// 一度だけ承認を通知します。連打や重複イベントで確認後の処理が複数回走るのを防ぎます。
void UBoarConfirmationWidget::Confirm()
{
	if (bDecisionSent) return;
	bDecisionSent = true;
	OnDecision.Broadcast(true);
}
// 承認と共通の送信済みフラグを使い、否定側の決定も一度だけ通知します。
void UBoarConfirmationWidget::Cancel()
{
	if (bDecisionSent) return;
	bDecisionSent = true;
	OnDecision.Broadcast(false);
}
