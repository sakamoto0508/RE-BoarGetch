#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CageHealthWidget.generated.h"

class UProgressBar;

/**
 * ワールド空間に表示する横型HPバーの表示窓口。保持する値は呼び出し元から受け取った表示用コピーで、HPの正本ではありません。
 * 枠・色・サイズ・残像・警告の見た目はWBP_CageHealth（Designer）で編集します。
 */
UCLASS()
class REBOARGETCH_API UCageHealthWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetPresentation(float InFraction, float InTrail, FLinearColor InColor, float InFlash, float InPulse);
	/** ゲージ表示に必須のBinding（HealthBar）が揃っているか。TrailBar・PulseIndicatorは任意です。 */
	bool HasRequiredBindings() const { return HealthBar != nullptr; }
protected:
	virtual void NativeConstruct() override;

	/** 値を受け取った直後に呼ばれます。WBP側で追加演出（Widget Animation等）を組む場合に使います。 */
	UFUNCTION(BlueprintImplementableEvent,Category="Gauge") void OnPresentationChanged();

	/** 現在HP比率を表示するバー。Fill色は受け取った色にFlashを混ぜた値で上書きします。 */
	UPROPERTY(BlueprintReadOnly,Category="Gauge",meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> HealthBar;
	/** 遅れて追従する残像バー。色・不透明度はWBPで設定します。 */
	UPROPERTY(BlueprintReadOnly,Category="Gauge",meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> TrailBar;
	/** 低HP警告。Pulseが0のときは非表示、それ以外はRender Opacityで明滅させます。 */
	UPROPERTY(BlueprintReadOnly,Category="Gauge",meta=(BindWidgetOptional)) TObjectPtr<UWidget> PulseIndicator;

	/** 被弾時にFill色へ混ぜる色です。 */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Gauge") FLinearColor FlashColor=FLinearColor::White;
	/** 警告表示の不透明度＝Min + Range × Pulse。 */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Gauge",meta=(ClampMin="0",ClampMax="1")) float PulseMinOpacity=.3f;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Gauge",meta=(ClampMin="0",ClampMax="1")) float PulseOpacityRange=.35f;

	// 現在HP比率、遅れて追従する残像、被弾時の白色混合、警告の強さ（すべて表示用コピー）。
	UPROPERTY(BlueprintReadOnly,Transient,Category="Gauge|Display") float Fraction=1;
	UPROPERTY(BlueprintReadOnly,Transient,Category="Gauge|Display") float Trail=1;
	UPROPERTY(BlueprintReadOnly,Transient,Category="Gauge|Display") float Flash=0;
	UPROPERTY(BlueprintReadOnly,Transient,Category="Gauge|Display") float Pulse=0;
	UPROPERTY(BlueprintReadOnly,Transient,Category="Gauge|Display") FLinearColor Color=FLinearColor(.02f,.75f,1.f);
private:
	void ApplyToBoundWidgets();
};
