#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarGameOverEffectWidget.generated.h"

/**
 * GameOver演出用の全画面オーバーレイです（入力を受けません）。
 * 赤フラッシュ、画面周辺の細い警告ライン、短いデジタルノイズ、周辺の薄いDark Navy減光をSlateで直接描画します。
 * 値の時間変化はUGameOverPresentationComponentが毎フレーム設定し、このWidgetは描画だけを担当します。
 * Post Process（RetroRenderのUnbound PP）には触れず、UMGの最前面付近に重ねます。
 */
UCLASS(Blueprintable)
class REBOARGETCH_API UBoarGameOverEffectWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 0～1。画面全体に重ねる赤フラッシュの強さです。 */
	void SetFlashAlpha(float Alpha) { FlashAlpha = Alpha; }
	/** 0～1。周辺の警告ライン・コーナー・ラベルの表示量です。 */
	void SetWarningAlpha(float Alpha) { WarningAlpha = Alpha; }
	/** 0～1。デジタルノイズ（横ずれバー・走査線）の強さです。 */
	void SetNoiseAmount(float Amount) { NoiseAmount = Amount; }
	/** 0～1。周辺のDark Navy減光の強さです。 */
	void SetVignetteAlpha(float Alpha) { VignetteAlpha = Alpha; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameOver Effect") FLinearColor FlashColor = FLinearColor(1.f, .07f, .04f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameOver Effect") FLinearColor LineColor = FLinearColor(1.f, .1f, .07f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameOver Effect") FLinearColor AccentColor = FLinearColor(1.f, .48f, .06f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameOver Effect") FLinearColor CyanColor = FLinearColor(.05f, .85f, 1.f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameOver Effect") FLinearColor NavyColor = FLinearColor(.006f, .018f, .06f, 1.f);
	/** 画面端から警告ラインまでの距離（px、DPI適用前）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameOver Effect", meta = (ClampMin = "0")) float LineInset = 46.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameOver Effect", meta = (ClampMin = "0.5")) float LineThickness = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameOver Effect", meta = (ClampMin = "4")) float CornerLength = 76.f;
	/** 周辺減光の幅（画面短辺に対する割合）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameOver Effect", meta = (ClampMin = "0", ClampMax = "0.5")) float VignetteWidth = .16f;
	/** 左上の小さな警告ラベル。空なら表示しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameOver Effect") FText WarningLabel = FText::FromString(TEXT("VITAL SIGNAL LOST"));
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameOver Effect") FText SubLabel = FText::FromString(TEXT("GADGET LINK // OFFLINE"));
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameOver Effect", meta = (ClampMin = "0", ClampMax = "64")) int32 NoiseBarCount = 14;

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
private:
	float FlashAlpha = 0.f, WarningAlpha = 0.f, NoiseAmount = 0.f, VignetteAlpha = 0.f;
};
