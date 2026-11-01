#include "UI/BoarGameOverEffectWidget.h"
#include "Animation/WidgetAnimation.h"

void UBoarGameOverEffectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UBoarGameOverEffectWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ApplyLayer(VignetteLayer, VignetteAlpha);
	ApplyLayer(FlashLayer, FlashAlpha);
	ApplyLayer(WarningLayer, WarningAlpha);
	ApplyLayer(NoiseLayer, NoiseAmount);
	if (WarningLoop) PlayAnimation(WarningLoop, 0.f, 0);
}

// 強さをレイヤーのRender Opacityへ反映します。色・形・動きはWBPとUI Material側の設定です。
void UBoarGameOverEffectWidget::ApplyLayer(UWidget* Layer, float Alpha)
{
	if (!Layer) return;
	Layer->SetRenderOpacity(FMath::Clamp(Alpha, 0.f, 1.f));
	Layer->SetVisibility(Alpha > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UBoarGameOverEffectWidget::SetFlashAlpha(float Alpha)
{
	FlashAlpha = Alpha;
	ApplyLayer(FlashLayer, Alpha);
}

void UBoarGameOverEffectWidget::SetWarningAlpha(float Alpha)
{
	WarningAlpha = Alpha;
	ApplyLayer(WarningLayer, Alpha);
}

void UBoarGameOverEffectWidget::SetNoiseAmount(float Amount)
{
	NoiseAmount = Amount;
	ApplyLayer(NoiseLayer, Amount);
}

void UBoarGameOverEffectWidget::SetVignetteAlpha(float Alpha)
{
	VignetteAlpha = Alpha;
	ApplyLayer(VignetteLayer, Alpha);
}
