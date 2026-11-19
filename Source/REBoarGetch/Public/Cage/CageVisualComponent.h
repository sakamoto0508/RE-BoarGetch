#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CageVisualComponent.generated.h"

class ACage;
class UStaticMesh;
class UStaticMeshComponent;
class UWidgetComponent;
class UMaterialInstanceDynamic;
class UCageHealthWidget;

/** 檻の見た目（専用Mesh・HPランプ・頭上のHPゲージ）を担当します。HPや破壊の処理はACage側が持ちます。 */
UCLASS(ClassGroup=(Visual), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UCageVisualComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCageVisualComponent();

	/** 檻の見た目に使う専用Mesh（SM_Cage、元データはSourceArt/Cage）。未設定時はErrorを出して見た目だけを無効化します（代替表示なし）。Collisionは持たせません。 */
	UPROPERTY(EditAnywhere, Category="Visual")
	TObjectPtr<UStaticMesh> CageMesh;

	/** CageMeshの位置・回転・Scale調整です。Gameplay用のCollisionや収容範囲は動かしません。 */
	UPROPERTY(EditAnywhere, Category="Visual")
	FTransform CageMeshTransform;

	/** HPに応じて色を変えるランプのMaterial Slot番号です（CageMesh使用時）。Materialには"Tint"と"Intensity"が必要です。 */
	UPROPERTY(EditAnywhere, Category="Visual")
	TArray<int32> LampMaterialSlots;

	/** HPゲージに使うWidget（WBP_CageHealth）。見た目はこのWBPで編集します。未設定・必須Binding不足ではErrorを出してゲージだけを無効化します（HP・破壊処理は継続）。 */
	UPROPERTY(EditAnywhere, Category="Gauge")
	TSubclassOf<UCageHealthWidget> GaugeWidgetClass;

	/** World Space WidgetComponentの描画解像度です。WBPのレイアウト基準サイズと合わせます。 */
	UPROPERTY(EditAnywhere, Category="Gauge")
	FVector2D GaugeDrawSize = FVector2D(640, 100);

	/** カメラからこの距離（cm）以内のときだけゲージを表示します。0以下なら常に表示します。 */
	UPROPERTY(EditAnywhere, Category="Gauge", meta=(ClampMin="0"))
	float DisplayDistance = 12000.f;

	/** ゲージを置く高さ（このComponentからの相対Z、cm）。 */
	UPROPERTY(EditAnywhere, Category="Gauge")
	float GaugeHeight = 650.f;

	/** ゲージWidgetのWorld上のScaleです。 */
	UPROPERTY(EditAnywhere, Category="Gauge")
	float GaugeWorldScale = .9f;

	/** HP比率がこの値以下で「中」の色、LowThreshold以下で「低」の色（点滅あり）になります。 */
	UPROPERTY(EditAnywhere, Category="Gauge", meta=(ClampMin="0", ClampMax="1"))
	float MediumThreshold = .6f;

	UPROPERTY(EditAnywhere, Category="Gauge", meta=(ClampMin="0", ClampMax="1"))
	float LowThreshold = .25f;

	/** ゲージのFill色（HP比率がMediumThresholdより上／以下／LowThreshold以下）。 */
	UPROPERTY(EditAnywhere, Category="Gauge")
	FLinearColor GaugeHighColor = FLinearColor(.015f, .75f, 1);

	UPROPERTY(EditAnywhere, Category="Gauge")
	FLinearColor GaugeMediumColor = FLinearColor(1, .72f, .025f);

	UPROPERTY(EditAnywhere, Category="Gauge")
	FLinearColor GaugeLowColor = FLinearColor(1, .035f, .02f);

	/** 被弾時にゲージ・ランプを白く光らせる長さ（秒）。 */
	UPROPERTY(EditAnywhere, Category="Gauge", meta=(ClampMin="0.01", Units="s"))
	float DamageFlashDuration = .18f;

	/** 減ったHPの残像（Trail）が実際のHPへ追いつく速さ（HP比率/秒）。 */
	UPROPERTY(EditAnywhere, Category="Gauge", meta=(ClampMin="0"))
	float TrailCatchUpSpeed = .75f;

	/** 低HP時のゲージ・ランプの点滅の速さ（Sinの角速度、rad/秒）。 */
	UPROPERTY(EditAnywhere, Category="Gauge", meta=(ClampMin="0"))
	float LowHealthPulseSpeed = 4.f;

	/** ランプMaterialの"Tint"へ渡す色（通常／低HP）。被弾時は白へ寄せます。 */
	UPROPERTY(EditAnywhere, Category="Visual")
	FLinearColor LampNormalTint = FLinearColor(.01f, .65f, 1);

	UPROPERTY(EditAnywhere, Category="Visual")
	FLinearColor LampLowTint = FLinearColor(1, .04f, .015f);

	/** ランプMaterialの"Intensity"（通常時）と、低HP時の基準値・点滅で足す最大値。 */
	UPROPERTY(EditAnywhere, Category="Visual", meta=(ClampMin="0"))
	float LampNormalIntensity = 1.2f;

	UPROPERTY(EditAnywhere, Category="Visual", meta=(ClampMin="0"))
	float LampLowIntensity = .5f;

	UPROPERTY(EditAnywhere, Category="Visual", meta=(ClampMin="0"))
	float LampLowPulseIntensity = .3f;

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

private:
	UPROPERTY()
	TObjectPtr<UWidgetComponent> Gauge;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> CageMeshComponent;

	void ApplyVisualSource();

	/** ゲージWidgetを生成し、必須Bindingを検証します。使えない場合はErrorを1回出してゲージを無効化します。 */
	void InitGauge();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Lamps;

	UPROPERTY(Transient)
	TObjectPtr<ACage> Cage;

	UFUNCTION()
	void HealthChanged(float Hp, float MaxHp);

	UFUNCTION()
	void Destroyed();

	UFUNCTION()
	void Respawned();

	/** 現在のHP比率と、遅れて追いつく残像の比率、被弾フラッシュの残り時間（秒）。 */
	float Fraction = 1;
	float Trail = 1;
	float FlashRemaining = 0;

	bool bDestroyed = false;

	/** 次のTickで表示を更新するか。アニメーションしていない間は描画し直しません。 */
	bool bRedraw = true;

	/** ゲージWidgetが正しく構築できたか。falseの間はゲージを表示・更新しません。 */
	bool bGaugeAvailable = false;
};
