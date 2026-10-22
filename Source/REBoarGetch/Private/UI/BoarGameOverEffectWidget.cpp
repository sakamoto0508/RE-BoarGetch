#include "UI/BoarGameOverEffectWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/CanvasPanel.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FSlateColorBrush& WhiteBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return Brush;
	}

	void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, float X, float Y, float W, float H, const FLinearColor& Color)
	{
		if (W <= 0.f || H <= 0.f || Color.A <= 0.002f) return;
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2D(W, H), FSlateLayoutTransform(FVector2D(X, Y))),
			&WhiteBrush(), ESlateDrawEffect::None, Color);
	}

	FLinearColor WithAlpha(FLinearColor C, float A)
	{
		C.A = FMath::Clamp(A, 0.f, 1.f);
		return C;
	}
}

void UBoarGameOverEffectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Designer未使用のネイティブ生成でも空のRootを持たせ、全画面のGeometryを受け取れるようにします。
	if (WidgetTree && !WidgetTree->RootWidget) WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

int32 UBoarGameOverEffectWidget::NativePaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, G, MyCullingRect, Out, LayerId, InWidgetStyle, bParentEnabled) + 1;
	const FVector2D Size = FVector2D(G.GetLocalSize());
	const float W = Size.X, H = Size.Y;
	if (W < 4.f || H < 4.f) return Layer;
	const double Now = FPlatformTime::Seconds();

	// 周辺の薄いDark Navy減光：中央（倒れたプレイヤー）は暗くしません。
	if (VignetteAlpha > 0.f)
	{
		const int32 Steps = 10;
		const float Band = FMath::Min(W, H) * VignetteWidth / Steps;
		for (int32 I = 0; I < Steps; ++I)
		{
			const float A = VignetteAlpha * .5f * FMath::Square(1.f - float(I) / Steps);
			const FLinearColor C = WithAlpha(NavyColor, A);
			const float O = I * Band;
			Box(Out, Layer, G, O, O, W - 2 * O, Band, C);
			Box(Out, Layer, G, O, H - O - Band, W - 2 * O, Band, C);
			Box(Out, Layer, G, O, O + Band, Band, H - 2 * O - 2 * Band, C);
			Box(Out, Layer, G, W - O - Band, O + Band, Band, H - 2 * O - 2 * Band, C);
		}
	}

	// 赤フラッシュ：全画面に一瞬だけ重ね、周辺を少し濃くします。
	if (FlashAlpha > 0.f)
	{
		Box(Out, Layer, G, 0, 0, W, H, WithAlpha(FlashColor, FlashAlpha * .55f));
		const float Edge = FMath::Min(W, H) * .12f;
		for (int32 I = 0; I < 6; ++I)
		{
			const float O = Edge * I / 6.f, T = Edge / 6.f;
			const FLinearColor C = WithAlpha(FlashColor, FlashAlpha * .45f * (1.f - I / 6.f));
			Box(Out, Layer, G, 0, O, W, T, C);
			Box(Out, Layer, G, 0, H - O - T, W, T, C);
			Box(Out, Layer, G, O, 0, T, H, C);
			Box(Out, Layer, G, W - O - T, 0, T, H, C);
		}
	}
	++Layer;

	// 細い警告ライン：周辺を流れる破線、橙のコーナー、シアンの目盛り、小さな警告ラベル。
	if (WarningAlpha > 0.f)
	{
		const float In = LineInset, T = LineThickness, Pulse = .8f + .2f * FMath::Sin(float(Now) * 6.f);
		const FLinearColor Line = WithAlpha(LineColor, WarningAlpha * .8f * Pulse);
		const float Dash = 38.f, Gap = 16.f, Period = Dash + Gap;
		const float Shift = FMath::Fmod(float(Now) * 26.f, Period);
		auto DashedH = [&](float Y, float X0, float X1, float Dir)
		{
			for (float X = X0 - Period + (Dir > 0 ? Shift : Period - Shift); X < X1; X += Period)
			{
				const float A = FMath::Max(X, X0), B = FMath::Min(X + Dash, X1);
				Box(Out, Layer, G, A, Y, B - A, T, Line);
			}
		};
		auto DashedV = [&](float X, float Y0, float Y1, float Dir)
		{
			for (float Y = Y0 - Period + (Dir > 0 ? Shift : Period - Shift); Y < Y1; Y += Period)
			{
				const float A = FMath::Max(Y, Y0), B = FMath::Min(Y + Dash, Y1);
				Box(Out, Layer, G, X, A, T, B - A, Line);
			}
		};
		const float L = CornerLength;
		DashedH(In, In + L + 8.f, W - In - L - 8.f, 1.f);
		DashedH(H - In - T, In + L + 8.f, W - In - L - 8.f, -1.f);
		DashedV(In, In + L + 8.f, H - In - L - 8.f, -1.f);
		DashedV(W - In - T, In + L + 8.f, H - In - L - 8.f, 1.f);

		const FLinearColor Corner = WithAlpha(AccentColor, WarningAlpha * .95f);
		const float CT = T + 1.5f;
		for (int32 Cx = 0; Cx < 2; ++Cx)
			for (int32 Cy = 0; Cy < 2; ++Cy)
			{
				const float X = Cx ? W - In - L : In, Y = Cy ? H - In - CT : In;
				Box(Out, Layer, G, X, Y, L, CT, Corner);
				Box(Out, Layer, G, Cx ? W - In - CT : In, Cy ? H - In - L : In, CT, L, Corner);
			}

		const FLinearColor CyanTint = WithAlpha(CyanColor, WarningAlpha * .55f);
		for (int32 I = 0; I < 12; ++I)
			Box(Out, Layer, G, W - In - L - 26.f - I * 9.f, H - In - 16.f, 2.f, (I % 3 == 0) ? 9.f : 5.f, CyanTint);

		if (!WarningLabel.IsEmpty())
		{
			const float Blink = (FMath::Fmod(float(Now), .9f) < .62f) ? 1.f : .45f;
			const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 20);
			FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2D(520, 30), FSlateLayoutTransform(FVector2D(In + 18.f, In + 14.f))),
				FString(TEXT("▲ ")) + WarningLabel.ToString(), Font, ESlateDrawEffect::None, WithAlpha(LineColor, WarningAlpha * Blink));
			if (!SubLabel.IsEmpty())
			{
				const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 14);
				FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2D(520, 22), FSlateLayoutTransform(FVector2D(In + 20.f, In + 42.f))),
					SubLabel.ToString(), Small, ESlateDrawEffect::None, CyanTint);
			}
		}
	}
	++Layer;

	// 短いデジタルノイズ：1/30秒ごとに位置が変わる横ずれバーと走査線。プレイヤーを隠さないよう細く薄くします。
	if (NoiseAmount > 0.f)
	{
		FRandomStream Rand(int32(Now * 30.0));
		for (int32 I = 0; I < NoiseBarCount; ++I)
		{
			const float Y = Rand.FRandRange(0.f, H), BH = Rand.FRandRange(1.5f, 7.f);
			const float BW = Rand.FRandRange(.08f, .55f) * W, X = Rand.FRandRange(-.1f * W, W);
			const FLinearColor C = (I % 3 == 0) ? LineColor : CyanColor;
			Box(Out, Layer, G, FMath::Max(0.f, X), Y, FMath::Min(BW, W - FMath::Max(0.f, X)), BH, WithAlpha(C, NoiseAmount * Rand.FRandRange(.12f, .4f)));
		}
		const FLinearColor Scan = WithAlpha(NavyColor, NoiseAmount * .22f);
		for (float Y = FMath::Fmod(float(Now) * 120.f, 4.f); Y < H; Y += 4.f) Box(Out, Layer, G, 0, Y, W, 1.f, Scan);
	}
	return Layer;
}
