#include "UI/CageHealthWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/ProgressBar.h"
#include "Rendering/DrawElements.h"

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
	if (UsesFallbackPaint())
		if (const TSharedPtr<SWidget> Cached = GetCachedWidget()) Cached->Invalidate(EInvalidateWidgetReason::Paint);
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

// Fallback：WBPのバインドWidgetが無い場合のみ、640×100の基準座標で背景・残像・現在HP・警告を描画します。
int32 UCageHealthWidget::NativePaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull,
                                     FSlateWindowElementList& E, int32 L, const FWidgetStyle& Style,
                                     bool bEnabled) const
{
	L = Super::NativePaint(Args, G, Cull, E, L, Style, bEnabled);
	if (!UsesFallbackPaint()) return L;
	static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 8.f);
	const FVector2D Size = G.GetLocalSize();
	const FVector2D Scale(Size.X / 640.f, Size.Y / 100.f);
	auto Box = [&](float X, float Y, float W, float H, FLinearColor C)
	{
		if (W <= 0) return;
		FSlateDrawElement::MakeBox(
			E, ++L, G.ToPaintGeometry(FVector2D(W, H) * Scale, FSlateLayoutTransform(FVector2D(X, Y) * Scale)), &Brush,
			ESlateDrawEffect::None, C * Style.GetColorAndOpacityTint());
	};
	Box(4, 16, 632, 80, FLinearColor(.015f, .035f, .07f));
	Box(8, 8, 624, 76, FLinearColor(.88f, .94f, .98f));
	Box(25, 23, 590, 46, FLinearColor(.008f, .025f, .05f));
	Box(32, 29, 576 * Trail, 34, FLinearColor(.75f, .85f, .92f, .55f));
	Box(32, 29, 576 * Fraction, 34, FLinearColor::LerpUsingHSV(Color, FlashColor, Flash));
	Box(14, 32, 8, 29, FLinearColor(1, .72f, .04f));
	Box(618, 32, 8, 29, FLinearColor(1, .72f, .04f));
	if (Pulse > 0) Box(50, 75, 540, 5, FLinearColor(1, .02f, .015f, PulseMinOpacity + PulseOpacityRange * Pulse));
	return L;
}
