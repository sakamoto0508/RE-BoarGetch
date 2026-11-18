#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarStageClearBannerWidget.generated.h"

/**
 * Stage Clear勝利演出の「STAGE CLEAR!」バナーです（下部の情報UIは持たず、詳細はResultに任せます）。
 * 見た目はWBPの画像（帯・レール・タイトル・グリッチ片・装飾・走査線）で作り、C++は登場・グリッチ・退場の動きだけを付けます。
 * 各部品は名前で探し、無いものは動かしません。時間は実時間（Slateの経過時間）で進みます。
 */
UCLASS(Blueprintable)
class REBOARGETCH_API UBoarStageClearBannerWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 全体をDuration秒でフェードアウトし、終わったらViewportから外します。 */
	UFUNCTION(BlueprintCallable, Category = "Stage Clear Banner") void HideBanner(float Duration = .4f);
	UFUNCTION(BlueprintPure, Category = "Stage Clear Banner") bool IsHiding() const { return HideDuration >= 0.f; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(EditDefaultsOnly, Category = "Parts") FName GlowWidgetName = TEXT("Glow");
	UPROPERTY(EditDefaultsOnly, Category = "Parts") FName BandWidgetName = TEXT("Band");
	UPROPERTY(EditDefaultsOnly, Category = "Parts") FName RailTopWidgetName = TEXT("RailTop");
	UPROPERTY(EditDefaultsOnly, Category = "Parts") FName RailBottomWidgetName = TEXT("RailBottom");
	UPROPERTY(EditDefaultsOnly, Category = "Parts") FName TitleWidgetName = TEXT("Title");
	UPROPERTY(EditDefaultsOnly, Category = "Parts") FName DecoWidgetName = TEXT("Deco");
	UPROPERTY(EditDefaultsOnly, Category = "Parts") FName SweepWidgetName = TEXT("Sweep");
	/** 横にずれるグリッチ片（タイトルの一部を切り出した画像）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Parts") TArray<FName> GlitchWidgetNames = { TEXT("GlitchA"), TEXT("GlitchB") };

	/** 帯とレールが開く時間、タイトルが出始める時刻・縮む時間・開始倍率。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0.05")) float BandOpenTime = .28f;
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0")) float TitleDelay = .12f;
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0.05")) float TitleSettleTime = .25f;
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "1")) float TitleStartScale = 1.18f;
	/** 登場直後にグリッチ片が揺れる時間と最大ずれ（px）。以後はGlitchInterval秒ごとにGlitchBurstTime秒だけ小さく揺れます。 */
	UPROPERTY(EditDefaultsOnly, Category = "Glitch", meta = (ClampMin = "0")) float IntroGlitchTime = .5f;
	UPROPERTY(EditDefaultsOnly, Category = "Glitch", meta = (ClampMin = "0")) float GlitchOffset = 18.f;
	UPROPERTY(EditDefaultsOnly, Category = "Glitch", meta = (ClampMin = "0.2")) float GlitchInterval = 2.2f;
	UPROPERTY(EditDefaultsOnly, Category = "Glitch", meta = (ClampMin = "0")) float GlitchBurstTime = .12f;
	/** 走査線がタイトルを上→下へ通過する時間と移動幅（px）。通過後は中央で止まります。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0.05")) float SweepTime = .45f;
	UPROPERTY(EditDefaultsOnly, Category = "Reveal") float SweepTravel = 70.f;

private:
	UWidget* Part(FName Name) const;
	void ApplyGlitch(float Strength);
	float Age = 0.f;
	float HideAge = 0.f;
	float HideDuration = -1.f;
	float NextGlitchStep = 0.f;
	FRandomStream Random;
};
