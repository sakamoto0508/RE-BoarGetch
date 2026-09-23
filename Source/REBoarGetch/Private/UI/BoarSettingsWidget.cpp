#include "UI/BoarSettingsWidget.h"
#include "Audio/BoarAudioManagerSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Player/BoarPlayerController.h"

void UBoarSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bInitialFocusPending = true;
	Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>() : nullptr;
	BGMSlider = WidgetTree ? Cast<USlider>(WidgetTree->FindWidget(BGMSliderName)) : nullptr;
	SESlider = WidgetTree ? Cast<USlider>(WidgetTree->FindWidget(SESliderName)) : nullptr;
	BGMValueText = WidgetTree ? Cast<UTextBlock>(WidgetTree->FindWidget(BGMValueTextName)) : nullptr;
	SEValueText = WidgetTree ? Cast<UTextBlock>(WidgetTree->FindWidget(SEValueTextName)) : nullptr;
	BackButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(BackButtonName)) : nullptr;
	EncyclopediaButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(EncyclopediaButtonName)) : nullptr;
	if (EncyclopediaButton)
	{
		EncyclopediaButton->OnClicked.AddUniqueDynamic(this, &UBoarSettingsWidget::OpenEncyclopedia);
		EncyclopediaButton->OnHovered.AddUniqueDynamic(this, &UBoarSettingsWidget::FocusEncyclopedia);
	}
	for (USlider* Slider : { BGMSlider.Get(), SESlider.Get() })
	{
		if (!Slider) continue;
		Slider->SetMinValue(0.0f);
		Slider->SetMaxValue(1.0f);
		Slider->SetStepSize(0.05f);
		Slider->SetIsEnabled(Audio != nullptr);
	}
	RefreshValues();
	if (BGMSlider) BGMSlider->OnValueChanged.AddUniqueDynamic(this, &UBoarSettingsWidget::ChangeBGM);
	if (SESlider) SESlider->OnValueChanged.AddUniqueDynamic(this, &UBoarSettingsWidget::ChangeSE);
	if (BackButton)
	{
		BackButton->OnClicked.AddUniqueDynamic(this, &UBoarSettingsWidget::RequestClose);
		BackButton->OnHovered.AddUniqueDynamic(this, &UBoarSettingsWidget::FocusBack);
	}
}

void UBoarSettingsWidget::NativeDestruct()
{
	if (EncyclopediaButton)
	{
		EncyclopediaButton->OnClicked.RemoveDynamic(this, &UBoarSettingsWidget::OpenEncyclopedia);
		EncyclopediaButton->OnHovered.RemoveDynamic(this, &UBoarSettingsWidget::FocusEncyclopedia);
	}
	if (BGMSlider) BGMSlider->OnValueChanged.RemoveDynamic(this, &UBoarSettingsWidget::ChangeBGM);
	if (SESlider) SESlider->OnValueChanged.RemoveDynamic(this, &UBoarSettingsWidget::ChangeSE);
	if (BackButton)
	{
		BackButton->OnClicked.RemoveDynamic(this, &UBoarSettingsWidget::RequestClose);
		BackButton->OnHovered.RemoveDynamic(this, &UBoarSettingsWidget::FocusBack);
	}
	Audio = nullptr;
	Super::NativeDestruct();
}

void UBoarSettingsWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (!bInitialFocusPending) return;
	bInitialFocusPending = false;
	FocusInitialChoice();
}

void UBoarSettingsWidget::RefreshValues()
{
	TGuardValue<bool> Guard(bRefreshing, true);
	if (!Audio)
	{
		if (BGMValueText) BGMValueText->SetText(FText::FromString(TEXT("—")));
		if (SEValueText) SEValueText->SetText(FText::FromString(TEXT("—")));
		return;
	}
	const float BGM = Audio->GetBGMVolume();
	const float SE = Audio->GetSoundEffectVolume();
	if (BGMSlider) BGMSlider->SetValue(BGM);
	if (SESlider) SESlider->SetValue(SE);
	if (BGMValueText) BGMValueText->SetText(FText::AsNumber(FMath::RoundToInt(BGM * 100.0f)));
	if (SEValueText) SEValueText->SetText(FText::AsNumber(FMath::RoundToInt(SE * 100.0f)));
}

void UBoarSettingsWidget::ChangeBGM(float Value)
{
	if (!Audio || bRefreshing || !FMath::IsFinite(Value)) return;
	Audio->SetBGMVolume(Value);
	RefreshValues();
}

void UBoarSettingsWidget::ChangeSE(float Value)
{
	if (!Audio || bRefreshing || !FMath::IsFinite(Value)) return;
	Audio->SetSoundEffectVolume(Value);
	RefreshValues();
}

void UBoarSettingsWidget::FocusInitialChoice()
{
	if (Audio && BGMSlider) BGMSlider->SetKeyboardFocus();
	else FocusBack();
}

void UBoarSettingsWidget::FocusBack()
{
	if (BackButton) BackButton->SetKeyboardFocus();
}

void UBoarSettingsWidget::RequestClose()
{
	OnClosed.Broadcast();
}

void UBoarSettingsWidget::OpenEncyclopedia()
{
	if (auto* PC = Cast<ABoarPlayerController>(GetOwningPlayer())) PC->OpenEncyclopedia(this, EncyclopediaButton);
}
void UBoarSettingsWidget::FocusEncyclopedia() { if (EncyclopediaButton) EncyclopediaButton->SetKeyboardFocus(); }
