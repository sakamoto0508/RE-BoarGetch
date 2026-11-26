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
class UNiagaraSystem;
class UACFCameraEffectPreset;

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

	//-------------------------------------------------
	// VFX（演出のみ。未設定なら出しません。HP・破壊処理には影響しません）
	//-------------------------------------------------

	/** Boarの攻撃が通った瞬間の衝撃波です。既定はプレイヤー被弾と同じNS_PlayerHitImpact（同じ世界の同系統の衝撃波として共有）。 */
	UPROPERTY(EditAnywhere, Category="VFX|Hit")
	TObjectPtr<UNiagaraSystem> HitVFX;

	/** 衝撃波全体の大きさ（User.Scale）です。檻は少し離れて見ることが多いので、プレイヤー被弾より少し大きめです。 */
	UPROPERTY(EditAnywhere, Category="VFX|Hit", meta=(ClampMin="0"))
	float HitVFXScale = 1.15f;

	/** 衝撃リング・放射線の色（User.Color）。プレイヤー被弾（淡い黄色）より少しだけ白寄りです。 */
	UPROPERTY(EditAnywhere, Category="VFX|Hit")
	FLinearColor HitVFXColor = FLinearColor(1, .96f, .80f);

	/** 小さな火花の色（User.SparkColor）。檻の金属・ランプに合わせた薄いシアンです。 */
	UPROPERTY(EditAnywhere, Category="VFX|Hit")
	FLinearColor HitVFXSparkColor = FLinearColor(.55f, .9f, 1);

	/** 放射線の本数・火花の数（負ならNiagara既定値）。 */
	UPROPERTY(EditAnywhere, Category="VFX|Hit")
	int32 HitVFXStreakCount = 8;

	UPROPERTY(EditAnywhere, Category="VFX|Hit")
	int32 HitVFXParticleCount = 8;

	/** リングの最終半径（cm）・寿命（秒）（負ならNiagara既定値）。 */
	UPROPERTY(EditAnywhere, Category="VFX|Hit", meta=(Units="cm"))
	float HitVFXMaxRadius = 140.f;

	UPROPERTY(EditAnywhere, Category="VFX|Hit", meta=(Units="s"))
	float HitVFXLifetime = .28f;

	/** リング面をカメラへ傾ける割合（0 = 突進方向に垂直）。 */
	UPROPERTY(EditAnywhere, Category="VFX|Hit", meta=(ClampMin="0", ClampMax="1"))
	float HitVFXRingCameraBias = .5f;

	/** 命中位置を攻撃側へ寄せる距離（cm）。檻の柵の内側に隠れないようにします。 */
	UPROPERTY(EditAnywhere, Category="VFX|Hit", meta=(Units="cm"))
	float HitVFXSurfaceOffset = 15.f;

	/** 命中VFXを出す最低の高さ（檻の床から、cm）。突進したBoarの体で隠れないよう、背中より上に出します。 */
	UPROPERTY(EditAnywhere, Category="VFX|Hit", meta=(Units="cm"))
	float HitVFXMinHeight = 120.f;

	/** 命中VFXをカメラ側へ寄せる距離（cm）。柵やBoarの体に埋もれないようにします。 */
	UPROPERTY(EditAnywhere, Category="VFX|Hit", meta=(Units="cm"))
	float HitVFXCameraPull = 40.f;

	/** 攻撃が通ったときに檻の見た目（Mesh・ゲージ）を押し込む向きに揺らす量（cm）・秒数・往復回数です。0で揺らしません。Collisionは動かしません。 */
	UPROPERTY(EditAnywhere, Category="VFX|Hit", meta=(ClampMin="0", Units="cm"))
	float HitShakeAmplitude = 9.f;

	UPROPERTY(EditAnywhere, Category="VFX|Hit", meta=(ClampMin="0.01", Units="s"))
	float HitShakeDuration = .28f;

	UPROPERTY(EditAnywhere, Category="VFX|Hit", meta=(ClampMin="0.5"))
	float HitShakeOscillations = 3.f;

	/** 命中VFXを再生してから次を許可する最短秒数。同時に複数のBoarが当たっても重なりすぎないようにします。 */
	UPROPERTY(EditAnywhere, Category="VFX|Hit", meta=(ClampMin="0", Units="s"))
	float MinHitVFXInterval = .1f;

	/**
	 * 檻が壊れた瞬間の爆発演出（専用。スターバースト状のフラッシュ・爆発光・トゥーン調の火球・明るい灰茶の煙・火花・
	 * 地面と空中の衝撃波・赤熱して冷える破片・地面を這う土煙）。既定はNS_CageBreak。全体で約1.4秒で消えます。
	 */
	UPROPERTY(EditAnywhere, Category="VFX|Break")
	TObjectPtr<UNiagaraSystem> BreakVFX;

	/** 爆発全体の大きさ（User.Scale）。 */
	UPROPERTY(EditAnywhere, Category="VFX|Break", meta=(ClampMin="0"))
	float BreakVFXScale = 1.f;

	/** 火球・煙・火花・破片・地面の土煙の数（User.FireCount / SmokeCount / SparkCount / DebrisCount / DustCount）。 */
	UPROPERTY(EditAnywhere, Category="VFX|Break", meta=(ClampMin="0"))
	int32 BreakFireCount = 12;

	UPROPERTY(EditAnywhere, Category="VFX|Break", meta=(ClampMin="0"))
	int32 BreakSmokeCount = 10;

	UPROPERTY(EditAnywhere, Category="VFX|Break", meta=(ClampMin="0"))
	int32 BreakSparkCount = 28;

	UPROPERTY(EditAnywhere, Category="VFX|Break", meta=(ClampMin="0"))
	int32 BreakDebrisCount = 16;

	UPROPERTY(EditAnywhere, Category="VFX|Break", meta=(ClampMin="0"))
	int32 BreakDustCount = 14;

	/** フラッシュの色（User.FlashColor）。 */
	UPROPERTY(EditAnywhere, Category="VFX|Break")
	FLinearColor BreakFlashColor = FLinearColor(1, .92f, .70f);

	/** 爆発光（周囲の地面・小物を一瞬照らす点光源）の色・明るさ・半径（User.LightColor / LightIntensity / LightRadius）。明るさ0で光なし。 */
	UPROPERTY(EditAnywhere, Category="VFX|Break")
	FLinearColor BreakLightColor = FLinearColor(1, .62f, .28f);

	UPROPERTY(EditAnywhere, Category="VFX|Break", meta=(ClampMin="0"))
	float BreakLightIntensity = 40.f;

	UPROPERTY(EditAnywhere, Category="VFX|Break", meta=(ClampMin="0", Units="cm"))
	float BreakLightRadius = 1400.f;

	/** 煙の色味（User.SmokeTint、灰色のMaterialに掛けます）。黒くしすぎないでください。 */
	UPROPERTY(EditAnywhere, Category="VFX|Break")
	FLinearColor BreakSmokeTint = FLinearColor(1, .97f, .92f);

	/** 地面の土煙の色味（User.DustColor、灰色のMaterialに掛けます）。 */
	UPROPERTY(EditAnywhere, Category="VFX|Break")
	FLinearColor BreakDustColor = FLinearColor(1.25f, 1.02f, .74f);

	/**
	 * 爆発の瞬間にACFカメラへ重ねる揺れです（既定はプレイヤー被弾と同じDA_PlayerHitCameraEffect）。未設定・ACFカメラなしでは再生しません。
	 * 強さはBreakCameraEffectScaleを最大に、檻からの距離がBreakCameraEffectRadiusに近づくほど弱くなります。
	 */
	UPROPERTY(EditAnywhere, Category="VFX|Break")
	TObjectPtr<UACFCameraEffectPreset> BreakCameraEffect;

	UPROPERTY(EditAnywhere, Category="VFX|Break", meta=(ClampMin="0"))
	float BreakCameraEffectScale = .7f;

	UPROPERTY(EditAnywhere, Category="VFX|Break", meta=(ClampMin="1", Units="cm"))
	float BreakCameraEffectRadius = 3000.f;

	/** 檻の足元の半径・フラッシュの高さ（cm）。0以下ならCageMeshの大きさから自動で決めます。 */
	UPROPERTY(EditAnywhere, Category="VFX|Break", meta=(Units="cm"))
	float BreakFootprintRadiusOverride = 0.f;

	UPROPERTY(EditAnywhere, Category="VFX|Break", meta=(Units="cm"))
	float BreakCenterHeightOverride = 0.f;

	/** 攻撃命中の衝撃波を出します（ACageがHPを減らしたときだけ呼びます）。 */
	void PlayHitVFX(const FVector& Location, const FVector& Direction);

	/** 破壊演出を出します（ACage::DestroyCageから1回だけ呼ばれます）。 */
	void PlayBreakVFX();

	/** 見た目のMeshの中心（Meshが無ければActorの位置＋1m）です。命中位置が分からないダメージの命中VFXに使います。 */
	FVector GetCageCenter() const;

	/** 命中VFX・破壊VFXを出した回数（テスト・デバッグ用）。 */
	int32 GetHitVFXSpawnCount() const { return HitVFXSpawnCount; }
	int32 GetBreakVFXSpawnCount() const { return BreakVFXSpawnCount; }

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

	double LastHitVFXTime = -1.0e9;

	/** 命中の揺れ：残り秒数・揺らす向き（このComponentのローカル）・揺れていないときの相対位置。 */
	float ShakeRemaining = 0;
	FVector ShakeLocalDirection = FVector::ForwardVector;
	FVector RestRelativeLocation = FVector::ZeroVector;

	void StartHitShake(const FVector& WorldDirection);
	void UpdateHitShake(float Dt);
	int32 HitVFXSpawnCount = 0;
	int32 BreakVFXSpawnCount = 0;
};
