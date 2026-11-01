#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarGameOverEffectWidget.generated.h"

/**
 * GameOver演出用の全画面オーバーレイです（入力を受けません）。
 * 赤フラッシュ、画面周辺の細い警告ライン、短いデジタルノイズ、周辺の薄いDark Navy減光の見た目はWBP_GameOverEffect
 * （UI Material・Widget Animation）で編集し、このクラスは受け取った強さを各レイヤーのRender Opacityへ反映するだけです。
 * 値の時間変化はAGameOverPresentationDirectorが毎フレーム設定します。
 * Post Process（RetroRenderのUnbound PP）には触れず、UMGの最前面付近に重ねます。
 */
UCLASS(Blueprintable)
class REBOARGETCH_API UBoarGameOverEffectWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 0～1。画面全体に重ねる赤フラッシュの強さです。 */
	void SetFlashAlpha(float Alpha);
	/** 0～1。周辺の警告ライン・コーナー・ラベルの表示量です。 */
	void SetWarningAlpha(float Alpha);
	/** 0～1。デジタルノイズ（横ずれバー・走査線）の強さです。 */
	void SetNoiseAmount(float Amount);
	/** 0～1。周辺のDark Navy減光の強さです。 */
	void SetVignetteAlpha(float Alpha);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	/** 各レイヤーのRender OpacityへSet*の値を反映します。0のときはCollapsedにして描画しません。 */
	UPROPERTY(BlueprintReadOnly, Category = "GameOver Effect", meta = (BindWidgetOptional)) TObjectPtr<UWidget> VignetteLayer;
	UPROPERTY(BlueprintReadOnly, Category = "GameOver Effect", meta = (BindWidgetOptional)) TObjectPtr<UWidget> FlashLayer;
	UPROPERTY(BlueprintReadOnly, Category = "GameOver Effect", meta = (BindWidgetOptional)) TObjectPtr<UWidget> WarningLayer;
	UPROPERTY(BlueprintReadOnly, Category = "GameOver Effect", meta = (BindWidgetOptional)) TObjectPtr<UWidget> NoiseLayer;
	/** 表示中ずっとループ再生する警告演出（ラベルの点滅など）。WBPのWidget Animationで編集します。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "GameOver Effect", meta = (BindWidgetAnimOptional)) TObjectPtr<UWidgetAnimation> WarningLoop;
private:
	static void ApplyLayer(UWidget* Layer, float Alpha);
	float FlashAlpha = 0.f, WarningAlpha = 0.f, NoiseAmount = 0.f, VignetteAlpha = 0.f;
};
