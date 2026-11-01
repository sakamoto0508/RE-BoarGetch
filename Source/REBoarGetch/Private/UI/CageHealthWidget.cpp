#include "UI/CageHealthWidget.h"
#include "Components/ProgressBar.h"

// 呼び出し元で計算済みの表示値を受け取り、WBPのWidgetへ反映します。HP本体は変更しません。
void UCageHealthWidget::SetPresentation(float InFraction, float InTrail, FLinearColor InColor, float InFlash,
                                        float InPulse)
{
	Fraction = InFraction;
	Trail = InTrail;
	Color = InColor;
	Flash = InFlash;
	Pulse = InPulse;
	ApplyToBoundWidgets();
	OnPresentationChanged();
}

void UCageHealthWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ApplyToBoundWidgets();
}

// 値の反映だけを行います。枠・色・サイズなどの見た目はWBP側の設定を使います。
void UCageHealthWidget::ApplyToBoundWidgets()
{
	if (HealthBar)
	{
		HealthBar->SetPercent(Fraction);
		HealthBar->SetFillColorAndOpacity(FLinearColor::LerpUsingHSV(Color, FlashColor, Flash));
	}
	if (TrailBar) TrailBar->SetPercent(Trail);
	if (PulseIndicator)
	{
		PulseIndicator->SetVisibility(Pulse > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		PulseIndicator->SetRenderOpacity(PulseMinOpacity + PulseOpacityRange * Pulse);
	}
}
