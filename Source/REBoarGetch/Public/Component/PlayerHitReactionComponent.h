#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Effects/ACFCameraEffectTypes.h"
#include "Player/BoarPlayerCharacter.h"
#include "PlayerHitReactionComponent.generated.h"

class UAnimMontage;
class UAnimSequenceBase;
class UACFCameraEffectPreset;
class UCameraShakeBase;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;

// ABoarPlayerCharacterがGameplay（ダメージ成立・Stun開始・残り時間・終了）を確定してから発火するEventを受け、命中インパクト（衝撃波Niagara・Hit Flash・SE・ACF Camera Effect）、しりもち→起き上がりAnimation、頭上のStun VFXを再生します。- Gameplayの状態は一切持ちません。演出アセットが未設定・再生失敗でも、Stunの開始／解除・入力ロックには影響しません。- Cameraは既存のACF Subsystemへ短いEffectを1つだけ重ねます（ACFのRequest・Preset・FOVは変更しません）。ACFカメラが無いとき（Console Variableで無効化など）だけ、UE Camera ShakeをFallbackで再生します。- World Time Dilationは使いません。- 値はすべてBP_PlayerCharacterのHitReactionComponent（Details）から調整できます。
/** プレイヤー被弾・Stunの演出だけを担当するComponentです。 */
UCLASS(ClassGroup=(Presentation), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UPlayerHitReactionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerHitReactionComponent();

	/** 頭上のStun VFXを表示中かを返します（テスト・デバッグ用）。 */
	UFUNCTION(BlueprintPure, Category = "Hit Reaction")
	bool IsStunVFXActive() const;

	/** しりもち・起き上がりのAnimationを再生中かを返します（テスト・デバッグ用）。 */
	UFUNCTION(BlueprintPure, Category = "Hit Reaction")
	bool IsKnockdownAnimationPlaying() const;

	/** Hit Flashを表示中かを返します（テスト・デバッグ用）。 */
	UFUNCTION(BlueprintPure, Category = "Hit Reaction")
	bool IsHitFlashActive() const { return HitFlashMID != nullptr; }

	/** 衝撃波Niagaraを生成した回数です（テスト・デバッグ用）。 */
	int32 GetImpactVFXSpawnCount() const { return ImpactVFXSpawnCount; }

	/** 命中インパクト演出を再生した回数です（テスト・デバッグ用。無効な接触で増えないことの確認に使います）。 */
	int32 GetImpactPresentationCount() const { return ImpactPresentationCount; }

	/** Stun表示（頭上VFX等）を出すべき状態かを返します。VFXアセットの有無に関係なく、Stun開始〜終了通知の間trueです。 */
	UFUNCTION(BlueprintPure, Category = "Hit Reaction")
	bool IsStunPresentationActive() const { return bStunPresentationActive; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//-------------------------------------------------
	// 命中インパクト
	//-------------------------------------------------

	// 未設定なら表示しません。User Parameter「ImpactDirection」「RingNormal」「Scale」「Color」をこのComponentが設定します。半径・寿命・本数などはNiagaraのUser Parameterで調整します。
	/** 命中位置に出すスタイライズド衝突波です（中心フラッシュ・突進方向に垂直な衝撃リング・放射線・小粒子）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Impact")
	TObjectPtr<UNiagaraSystem> ImpactVFX;

	/** 衝撃波全体の大きさの倍率です（Niagara User Parameter「Scale」）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Impact", meta = (ClampMin = "0.0"))
	float ImpactVFXScale = 1.0f;

	/** 衝撃リング・放射線の色（Niagara User Parameter「Color」）です。白〜淡い黄色。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Impact")
	FLinearColor ImpactVFXColor = FLinearColor(1.0f, 0.93f, 0.62f, 1.0f);

	/** リング面の法線を、突進方向からカメラ方向へ傾ける割合です（0 = 突進方向に完全に垂直）。横から見たときにリングが線になって消えるのを防ぎます。大きくするほどカメラ正面向きに近づき、突進方向が伝わりにくくなります。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Impact", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ImpactRingCameraBias = 0.35f;

	/** 衝撃波を再生してから次を許可するまでの最短秒数です。同じ命中で重複して出ないための保険です（通常は無敵・Stunで1回に限られます）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Impact", meta = (ClampMin = "0.0", Units = "s"))
	float MinImpactVFXInterval = 0.15f;

	/** 比較用の旧衝撃波（カメラ正面向きのオレンジのリング＋星）です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Impact|Legacy")
	TObjectPtr<UNiagaraSystem> LegacyImpactVFX;

	/** trueなら旧衝撃波を再生します。PIE中はConsole Variable「boar.HitVFX.UseLegacy 1 / 0」で一時的に切り替えられます。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Impact|Legacy")
	bool bUseLegacyImpactVFX = false;

	/** 旧方式の被弾VFXに渡す色。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Impact|Legacy")
	FLinearColor LegacyImpactVFXColor = FLinearColor(1.0f, 0.42f, 0.12f, 1.0f);

	/** 命中した瞬間の重い被弾SEです（2D再生）。未設定なら無音です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Impact")
	TObjectPtr<USoundBase> ImpactSound;

	/** 被弾SEの音量倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Impact", meta = (ClampMin = "0.0"))
	float ImpactSoundVolume = 1.0f;

	//-------------------------------------------------
	// ---- 被弾フラッシュ ----
	//-------------------------------------------------

	// 既定は捕獲演出と同じM_CaptureGlow）。未設定ならHit Flashを出しません。元のOverlay Materialは終了時に戻します。
	/** 全身へ一瞬重ねるOverlay Materialです（Scalar「GlowAmount」・Vector「GlowColor」を持つもの。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Hit Flash")
	TObjectPtr<UMaterialInterface> HitFlashMaterial;

	/** 被弾直後のオーバーレイの色。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Hit Flash")
	FLinearColor HitFlashColor = FLinearColor(1.0f, 0.55f, 0.35f, 1.0f);

	/** 開始時のGlowAmountです。レトロ描画（減色・低解像度）でも潰れない強さにします。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Hit Flash", meta = (ClampMin = "0.0"))
	float HitFlashIntensity = 3.0f;

	/** 最大強度を保つ秒数です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Hit Flash", meta = (ClampMin = "0.0", Units = "s"))
	float HitFlashHoldDuration = 0.05f;

	/** 最大強度から0まで消える秒数です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Hit Flash", meta = (ClampMin = "0.0", Units = "s"))
	float HitFlashFadeDuration = 0.14f;

	/** フラッシュの強さを受け取るマテリアルパラメータ名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Hit Flash")
	FName HitFlashAmountParameter = TEXT("GlowAmount");

	/** フラッシュの色を受け取るマテリアルパラメータ名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Hit Flash")
	FName HitFlashColorParameter = TEXT("GlowColor");

	//-------------------------------------------------
	// Camera
	//-------------------------------------------------

	// 攻撃元の位置をSourceLocationとして渡すため、揺れの向きで攻撃方向が伝わります。未設定・ACFカメラなしでは再生しません。
	/** 命中時にACFカメラへ重ねるEffectです（攻撃元から押し出すImpulse＋短いShake）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Camera")
	TObjectPtr<UACFCameraEffectPreset> HitCameraEffect;

	/** 被弾時のACFカメラ効果に掛ける強度倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Camera", meta = (ClampMin = "0.0"))
	float HitCameraEffectScale = 1.0f;

	/** ACFカメラが無効なときだけ使うUE Camera Shakeです。未設定ならFallbackの揺れは出しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Camera")
	TSubclassOf<UCameraShakeBase> FallbackCameraShake;

	/** ACF効果を開始できないときのカメラシェイク倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Camera", meta = (ClampMin = "0.0"))
	float FallbackCameraShakeScale = 1.0f;

	//-------------------------------------------------
	// しりもち・起き上がり
	//-------------------------------------------------

	// Stunの残り時間（StunDuration）に合わせて再生速度を変えるため、起き上がり完了と操作復帰が一致します。未設定なら再生しません。
	/** のけぞり→後ろへよろめく→しりもち→起き上がりのAnimationです（Root Motionなし）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Knockdown")
	TObjectPtr<UAnimSequenceBase> KnockdownAnimation;

	/** 再生するABP_PlayerのSlotです（空中アミと同じ全身Slot）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Knockdown")
	FName KnockdownSlotName = TEXT("FullBody");

	/** しりもちMontageのブレンドイン時間（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Knockdown", meta = (ClampMin = "0.0", Units = "s"))
	float KnockdownBlendIn = 0.06f;

	/** 起き上がりから通常のIdle／移動へ戻るブレンド秒数です。Stun解除の少し前から戻し始めます。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Knockdown", meta = (ClampMin = "0.0", Units = "s"))
	float KnockdownBlendOut = 0.2f;

	/** 再生速度の下限・上限です。StunDurationを極端に変えた場合でも動きが破綻しない範囲に収めます。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Knockdown", meta = (ClampMin = "0.1"))
	float KnockdownMinPlayRate = 0.6f;

	/** スタン時間にMontageを合わせる際の再生倍率上限。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Knockdown", meta = (ClampMin = "0.1"))
	float KnockdownMaxPlayRate = 1.8f;

	/** しりもちで地面に座った瞬間（Animationの正規化時間0〜1）に鳴らすSEです。未設定なら無音です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Knockdown")
	TObjectPtr<USoundBase> SitImpactSound;

	/** しりもちSEを鳴らすMontage上の正規化位置（0～1）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Knockdown", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SitImpactNormalizedTime = 0.25f;

	/** しりもちSEの音量倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Knockdown", meta = (ClampMin = "0.0"))
	float SitImpactSoundVolume = 0.7f;

	//-------------------------------------------------
	// 頭上のStun表示
	//-------------------------------------------------

	/** Stun中に頭上を周回するVFX（ループ）です。Stun終了・中断で必ず消します。未設定なら表示しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Stun VFX")
	TObjectPtr<UNiagaraSystem> StunVFX;

	/** 追従する頭のBone／Socketです。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Stun VFX")
	FName StunVFXSocket = TEXT("head");

	/** 頭のBoneからのWorld上方向の高さ(cm)です。頭が傾いても常に真上に出します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Stun VFX", meta = (Units = "cm"))
	float StunVFXHeight = 50.0f;

	/** 頭上のスタンVFXの拡大率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Stun VFX", meta = (ClampMin = "0.0"))
	float StunVFXScale = 1.0f;

	/** 頭上VFXの色（Niagara User Parameter「Color」）です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Stun VFX")
	FLinearColor StunVFXColor = FLinearColor(1.0f, 0.86f, 0.25f, 1.0f);

	/** Stun開始時に鳴らす短いSE（目が回る音）です。未設定なら無音です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Stun VFX")
	TObjectPtr<USoundBase> StunSound;

	/** スタン中に再生するSEの音量倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Stun VFX", meta = (ClampMin = "0.0"))
	float StunSoundVolume = 0.6f;

	/** スタンから通常どおり起き上がったときのSEです（ダウン等で中断された場合は鳴らしません）。未設定なら無音です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Stun VFX")
	TObjectPtr<USoundBase> StunRecoveredSound;

	/** スタン解除SEの音量倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction|Stun VFX", meta = (ClampMin = "0.0"))
	float StunRecoveredSoundVolume = 0.6f;

private:
	UFUNCTION()
	void HandleHitReceived(const FBoarPlayerHitInfo& HitInfo);
	UFUNCTION()
	void HandleStunStarted(const FBoarPlayerHitInfo& HitInfo);
	UFUNCTION()
	void HandleStunRecoveryStarted(float RecoveryDuration);
	UFUNCTION()
	void HandleStunEnded(bool bInterrupted);

	ABoarPlayerCharacter* GetPlayer() const;
	USkeletalMeshComponent* GetMesh() const;
	APlayerController* GetLocalController() const;

	void PlayImpactVFX(const FBoarPlayerHitInfo& HitInfo);
	void PlayCameraReaction(const FBoarPlayerHitInfo& HitInfo);
	void PlaySound2D(USoundBase* Sound, float Volume) const;

	void StartHitFlash();
	void StopHitFlash();

	void StartStunVFX();
	void StopStunVFX(bool bImmediate);
	void UpdateStunVFXLocation() const;

	void PlayKnockdown(float RecoveryDuration);
	void StopKnockdown(float BlendOutTime);
	void HandleSitImpact();

	void ReleaseCameraEffect(bool bImmediate);
	void RefreshTickEnabled();

	/** スタン解除時に停止する頭上VFXの参照。 */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> StunVFXComponent;

	/** 被弾フラッシュの強さを更新する動的マテリアル。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HitFlashMID;
	/** 被弾演出の終了時に復元する元のオーバーレイ。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> HitFlashOriginalOverlay;
	/** 被弾フラッシュ開始からの経過時間（秒）。 */
	float HitFlashElapsed = 0.0f;

	/** 終了・中断時に解除する現在のしりもちMontage。 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> KnockdownMontage;
	/** 同じMontageの別の再生を区別するインスタンスID。 */
	int32 KnockdownMontageInstanceID = INDEX_NONE;
	/** しりもちSEの予約を取り消すためのタイマー。 */
	FTimerHandle SitImpactTimerHandle;

	/** 終了時に解除する被弾ACFカメラ効果のハンドル。 */
	FACFCameraEffectHandle ActiveCameraEffect;
	/** 被弾演出を開始した回数を数える検証用カウンタ。 */
	int32 ImpactPresentationCount = 0;
	/** 被弾VFXを生成した回数を数える検証用カウンタ。 */
	int32 ImpactVFXSpawnCount = 0;
	/** 連続生成を抑制する被弾VFXの最終再生時刻（秒）。 */
	double LastImpactVFXTime = -1.0;
	/** スタン演出を開始し復元待ちの状態か。 */
	bool bStunPresentationActive = false;
	/** 所有プレイヤーの被弾・スタン通知に購読済みか。 */
	bool bBound = false;
};
