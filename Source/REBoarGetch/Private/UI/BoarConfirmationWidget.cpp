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

void UBoarConfirmationWidget::FocusInitialChoice() { FocusNo(); }
void UBoarConfirmationWidget::FocusYes() { if (YesButton) YesButton->SetKeyboardFocus(); }
void UBoarConfirmationWidget::FocusNo() { if (NoButton) NoButton->SetKeyboardFocus(); }
void UBoarConfirmationWidget::Confirm()
{
	if (bDecisionSent) return;
	bDecisionSent = true;
	OnDecision.Broadcast(true);
}
void UBoarConfirmationWidget::Cancel()
{
	if (bDecisionSent) return;
	bDecisionSent = true;
	OnDecision.Broadcast(false);
}
