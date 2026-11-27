#include "UI/BoarStageClearBannerWidget.h"
#include "Presentation/BoarPresentationTiming.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"

namespace
{
	// ---- 帯・レール・背景の光 ----
	/** 帯が開き始めるときの縦の大きさ（1で全開）。 */
	constexpr float BandStartHeightScale = .15f;
	/** レールが伸び切るのを帯より遅らせる時間（秒）。 */
	constexpr float RailExtraOpenTime = .1f;
	/** レールの横の大きさの下限（0だとWidgetが潰れるため）。 */
	constexpr float MinRailScale = .001f;
	/** 背景の光が出始める時刻と、出切るまでの時間（秒）。 */
	constexpr float GlowDelay = .05f;
	constexpr float GlowFadeInTime = .5f;

	// ---- タイトル ----
	/** 出始めの短い点滅（Title Delayからの秒数）と、その間の不透明度。 */
	constexpr float TitleFlickerStart = .14f;
	constexpr float TitleFlickerEnd = .18f;
	constexpr float TitleFlickerOpacity = .35f;
	/** タイトルのフェードインの速さ（Title Settle Timeに対する倍率）。 */
	constexpr float TitleFadeInRate = 2.5f;

	// ---- 装飾 ----
	/** 装飾が出始める時刻（Title Delayからの秒数）と、出切るまでの時間（秒）。 */
	constexpr float DecoDelay = .15f;
	constexpr float DecoFadeInTime = .25f;
	/** 装飾が縮んで定位置に収まる前の大きさ。 */
	constexpr float DecoStartScale = 1.06f;

	// ---- グリッチ ----
	/** 登場直後のグリッチが終わりまでに弱まる割合。 */
	constexpr float IntroGlitchFalloff = .5f;
	/** その後に時々だけ出すグリッチの強さ。 */
	constexpr float IdleGlitchStrength = .45f;
	/** グリッチのずれを更新する間隔（秒）。 */
	constexpr float GlitchStepInterval = .05f;

	float Ease(float T) { return FMath::InterpEaseOut(0.f, 1.f, FMath::Clamp(T, 0.f, 1.f), 3.f); }
}

UWidget* UBoarStageClearBannerWidget::Part(FName Name) const
{
	return WidgetTree && !Name.IsNone() ? WidgetTree->FindWidget(Name) : nullptr;
}

void UBoarStageClearBannerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Age = 0.f;
	HideDuration = -1.f;
	NextGlitchStep = 0.f;
	Random.Initialize(0x5C1EA2);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	// 最初のTick前に一瞬全表示されないよう、動かす部品は透明から始めます。
	for (FName Name : {GlowWidgetName, BandWidgetName, RailTopWidgetName, RailBottomWidgetName, TitleWidgetName, DecoWidgetName, SweepWidgetName})
		if (UWidget* W = Part(Name))
		{
			W->SetRenderTransformPivot(FVector2D(.5f, .5f));
			W->SetRenderOpacity(0.f);
		}
	for (FName Name : GlitchWidgetNames)
		if (UWidget* W = Part(Name))
			W->SetRenderOpacity(0.f);
}

void UBoarStageClearBannerWidget::HideBanner(float Duration)
{
	if (HideDuration >= 0.f)
		return;

	HideDuration = FMath::Max(0.f, Duration);
	HideAge = 0.f;

	if (HideDuration <= 0.f)
		RemoveFromParent();
}

// グリッチ片を横へずらして見せます（Strength 0で非表示）。
void UBoarStageClearBannerWidget::ApplyGlitch(float Strength)
{
	for (FName Name : GlitchWidgetNames)
		if (UWidget* W = Part(Name))
		{
			W->SetRenderOpacity(Strength > 0.f ? 1.f : 0.f);

			if (Strength > 0.f)
				W->SetRenderTranslation(FVector2D(Random.FRandRange(-GlitchOffset, GlitchOffset) * Strength, 0.f));
		}
}

void UBoarStageClearBannerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const float Dt = FMath::Min(InDeltaTime, BoarPresentationTiming::MaxFrameDeltaTime);
	Age += Dt;

	if (HideDuration >= 0.f)
	{
		HideAge += Dt;
		SetRenderOpacity(1.f - FMath::Clamp(HideAge / FMath::Max(HideDuration, .01f), 0.f, 1.f));

		if (HideAge >= HideDuration)
		{
			RemoveFromParent();
			return;
		}
	}

	// 帯：縦に開きながらフェードイン。レール：中央から左右へ伸びる。背景の光：ゆっくり出る。
	const float Open = Ease(Age / BandOpenTime);

	if (UWidget* W = Part(BandWidgetName))
	{
		W->SetRenderOpacity(Open);
		W->SetRenderScale(FVector2D(1.f, FMath::Lerp(BandStartHeightScale, 1.f, Open)));
	}

	for (FName Name : {RailTopWidgetName, RailBottomWidgetName})
		if (UWidget* W = Part(Name))
		{
			const float Rail = Ease(Age / (BandOpenTime + RailExtraOpenTime));
			W->SetRenderOpacity(FMath::Min(1.f, Rail * 2.f));
			W->SetRenderScale(FVector2D(FMath::Max(MinRailScale, Rail), 1.f));
		}
	if (UWidget* W = Part(GlowWidgetName))
		W->SetRenderOpacity(Ease((Age - GlowDelay) / GlowFadeInTime));

	// タイトル：少し大きい状態から縮んで定位置へ。出始めに一度だけ短く点滅させます。
	const float TT = (Age - TitleDelay) / TitleSettleTime;

	if (UWidget* W = Part(TitleWidgetName))
	{
		const float Settle = Ease(TT);
		const bool bFlicker = Age > TitleDelay + TitleFlickerStart && Age < TitleDelay + TitleFlickerEnd;
		W->SetRenderOpacity(bFlicker ? TitleFlickerOpacity : FMath::Clamp(TT * TitleFadeInRate, 0.f, 1.f));
		W->SetRenderScale(FVector2D(FMath::Lerp(TitleStartScale, 1.f, Settle)));
	}

	if (UWidget* W = Part(DecoWidgetName))
	{
		const float D = Ease((Age - TitleDelay - DecoDelay) / DecoFadeInTime);
		W->SetRenderOpacity(D);
		W->SetRenderScale(FVector2D(FMath::Lerp(DecoStartScale, 1.f, D)));
	}

	if (UWidget* W = Part(SweepWidgetName))
	{
		const float S = FMath::Clamp((Age - TitleDelay) / SweepTime, 0.f, 1.f);
		W->SetRenderOpacity(Age < TitleDelay ? 0.f : 1.f);

		// 上端から下端まで通過し、最後に中央へ戻って止まります。
		const float Y = S < 1.f ? FMath::Lerp(-SweepTravel, SweepTravel, S) : 0.f;
		W->SetRenderTranslation(FVector2D(0.f, Y));
	}

	// グリッチ：登場直後は強く、その後は時々だけ短く。Glitch Step Intervalごとにずれを更新します。
	float Strength = 0.f;

	if (Age > TitleDelay && Age < TitleDelay + IntroGlitchTime)
		Strength = 1.f - (Age - TitleDelay) / IntroGlitchTime * IntroGlitchFalloff;
	else if (GlitchInterval > 0.f && Age > TitleDelay + IntroGlitchTime)
		Strength = FMath::Fmod(Age - TitleDelay - IntroGlitchTime, GlitchInterval) > GlitchInterval - GlitchBurstTime ? IdleGlitchStrength : 0.f;

	if (Strength <= 0.f)
		ApplyGlitch(0.f);
	else if (Age >= NextGlitchStep)
	{
		ApplyGlitch(Strength);
		NextGlitchStep = Age + GlitchStepInterval;
	}
}
