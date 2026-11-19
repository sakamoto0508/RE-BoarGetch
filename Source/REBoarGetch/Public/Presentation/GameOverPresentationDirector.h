#pragma once

#include "CoreMinimal.h"
#include "Presentation/BoarPresentationDirector.h"
#include "GameOverPresentationDirector.generated.h"

class UAnimMontage;
class UAnimSequenceBase;
class UBoarGameOverEffectWidget;
class UCameraComponent;
class UCurveFloat;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class UPostProcessComponent;
class USpringArmComponent;

/** ガジェット発光の減衰中に一時的に変更したMeshの元Overlayです。 */
USTRUCT()
struct FGameOverGadgetMesh
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> OriginalOverlay;
};

/**
 * GameOverダウン演出の部品群です。順序とタイミング（赤フラッシュ・ヒットストップ・ダウン・カメラ・警告・SE）は
 * 派生BP（BP_GameOverPresentation）のEvent Graphで組み立てます。
 * このクラスは毎フレーム処理が必要な下位処理（実時間ヒットストップ、既存SpringArmの後退、ダウンMontageの保持、
 * 跳ね返り、発光減衰、効果Widgetの値）と、中断・Level遷移時の復元（時間倍率・カメラ・Overlay・HUD・Widget）だけを担当します。
 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API AGameOverPresentationDirector : public ABoarPresentationDirector
{
	GENERATED_BODY()

public:
	/** 画面効果Widgetを出し、赤フラッシュを開始します。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation")
	void BeginRedFlash(float PeakAlpha = .5f, float Duration = .24f);

	/** 実時間でDuration秒だけGlobal Time Dilationを下げ、必ず元の値へ戻します。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation")
	void BeginHitStop(float Duration = .09f, float TimeScale = .05f);

	/** 空中でHP0になった場合は、着地するまで重力だけを有効にします（入力は遮断済み）。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation")
	void SettlePlayerOnGround();

	/** 近くのBoarを跳ね返し、ダウンを隠さないようにします（カメラとの間にいる個体は横へ）。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation")
	void KnockBackNearbyBoars(float Radius = 260.f, float Distance = 170.f, float HopHeight = 35.f, float Duration = .4f);

	/** ダウンMontageを再生し最終姿勢で保持します（Slot非評価ならSingle Node）。再生した長さ（秒）を返します。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation")
	float PlayDownAnimation();

	/** 網などの装備へ発光Overlayを掛け、一瞬光ってから明滅しながらDecayTime秒で消します。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation")
	void BeginGadgetGlowDecay(float DecayTime = 1.1f);

	/** 通常HUDをDuration秒でフェードアウトします。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation")
	void FadeOutHUD(float Duration = .4f);

	/** 画面周辺の警告ラインと周辺減光をFadeInTime秒で表示します。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation")
	void BeginWarning(float FadeInTime = .35f);

	/** 短いデジタルノイズ（Duration秒で減衰）。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation")
	void PlayNoiseBurst(float Duration = .18f, float Intensity = .8f);

	/** 既存SpringArmで少し上昇→倒れた身体を中央に捉えたまま後退します（Ease Out、Yawは少しだけ回り込み）。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation")
	void BeginCameraPullBack(float Duration = 1.4f, float RiseDuration = .6f);

	/** GameOver UI表示中に残す警告ラインの強さを設定します。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation")
	void SetWarningAlphaForUI(float Alpha = .35f);

	/**
	 * カメラ（Post Process）のビネット＋減光・彩度低下をFadeInTime秒かけて掛けます。
	 * 既定では演出開始からCameraDarkenDelay秒後に自動で始まります（BPから早めに呼ぶことも可能）。
	 * RetroRenderのPost Process（Priority 1000）より上のUnbound Post Processで重ね、Directorの破棄（Retry・Level遷移）で必ず外れます。
	 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation")
	void BeginCameraDarken(float FadeInTime = -1.f);

	/** GameOver UI表示時に呼ばれます（敗北ジングルなど）。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "GameOver Presentation")
	void OnGameOverUIShown();

	// ---- Animation ----

	/** 上体が反る→よろける→尻もち→仰向けに倒れて停止するダウンMontage（FullBody Slot、最終姿勢で保持）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Animation")
	TObjectPtr<UAnimMontage> DownMontage;

	/** Slot非評価時のSingle Node再生用。未設定ならMontageの最初のSegment。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Animation")
	TObjectPtr<UAnimSequenceBase> DownAnimation;

	/** ダウンアニメーションの再生速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Animation", meta = (ClampMin = "0.1"))
	float DownPlayRate = 1.f;

	/** MontageのSlotがAnimGraphで評価されていないとき、Single Node再生へ切り替えるか。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Animation")
	bool bAllowSingleNodeFallback = true;

	// ---- Camera ----

	/** 最終的な見下ろし角（度）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "-80", ClampMax = "0"))
	float CameraEndPitch = -40.f;

	/** 後退しながらYawを回り込ませる量（度）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "-30", ClampMax = "30"))
	float CameraYawDrift = 12.f;

	/** SpringArmを伸ばす量（cm）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "0"))
	float PullBackDistance = 230.f;

	/** 注視点（倒れた身体の中心）へ加えるオフセット（cm、ワールド）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera")
	FVector FocusOffset = FVector(0, 0, 10);

	/** 注視点が倒れていく身体へ追従する速さ（VInterpToの速度）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "0"))
	float FocusInterpSpeed = 5.f;

	/** 後退し終えたときの視野角（度）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "20", ClampMax = "120"))
	float CameraEndFOV = 80.f;

	/** 演出中にSpringArmの壁判定で使うChannel。BoarのCapsuleはCameraをBlockするため既定はVisibility。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera")
	TEnumAsByte<ECollisionChannel> CameraProbeChannel = ECC_Visibility;

	/** 後退の進行カーブ（0～1）。未設定はEase Out。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera")
	TObjectPtr<UCurveFloat> CameraCurve;

	/** CameraCurve未設定時のEase Outの強さ。大きいほど最初に素早く下がります。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "1"))
	float CameraEaseExponent = 2.6f;

	/** 後退開始時、ACFの通常カメラからこの演出のSpringArmカメラへ切り替えるブレンド秒数。ACF未使用時は無関係です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "0"))
	float ACFHandOffBlendTime = .2f;

	// ---- Warning / VFX ----

	/** 赤フラッシュ・警告ライン・ノイズを描画するWidget（WBP_GameOverEffect）。未設定時はエフェクトを表示しません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Warning")
	TSubclassOf<UBoarGameOverEffectWidget> EffectWidgetClass;

	/** 警告表示中の周辺減光の最大の濃さ。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Warning", meta = (ClampMin = "0", ClampMax = "1"))
	float VignetteAlpha = .45f;

	/** 装備に掛ける発光Overlay（GlowAmount / GlowColor）。演出専用MIDを作り元Materialは変更しません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Warning")
	TObjectPtr<UMaterialInterface> GadgetGlowMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Warning")
	FLinearColor GadgetGlowColor = FLinearColor(.02f, .8f, 1.f);

	// ---- Camera Darken（視界が暗くなる演出） ----

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera Darken")
	bool bCameraDarken = true;

	/** 演出開始から自動で暗くし始めるまでの秒数（被弾の衝撃波・ヒットストップを見せてから）。負なら自動開始しません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera Darken", meta = (ClampMin = "0", Units = "s"))
	float CameraDarkenDelay = .25f;

	/** 暗くなりきるまでの秒数。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera Darken", meta = (ClampMin = "0.01", Units = "s"))
	float CameraDarkenFadeIn = 1.2f;

	/** Post ProcessのVignette Intensity（UE既定0.4）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera Darken", meta = (ClampMin = "0", ClampMax = "3"))
	float CameraVignetteIntensity = 1.4f;

	/** 画面全体に掛ける色（1で変化なし）。少し青みのある減光。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera Darken")
	FLinearColor CameraDarkenTint = FLinearColor(.58f, .6f, .7f);

	/** 彩度（1で変化なし）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera Darken", meta = (ClampMin = "0", ClampMax = "1"))
	float CameraDarkenSaturation = .6f;

	/** RetroRender（1000）より上にします。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera Darken")
	float CameraDarkenPriority = 2000.f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickPresentation(float Dt) override;

private:
	UBoarGameOverEffectWidget* EnsureEffectWidget();
	FVector ComputeBodyFocus() const;
	void EndHitStop();
	void UpdateDown();
	void UpdateCamera(float Dt);
	void UpdateGadgetGlow(float Dt);
	void UpdateKnockback(float Dt);
	void UpdateEffects(float Dt);
	void RestoreGadgetGlow();
	void UpdateCameraDarken(float Dt);

	// ---- 画面効果Widgetへ渡す値 ----
	float FlashPeak = 0.f;
	float FlashDuration = .24f;
	float FlashAge = 0.f;

	bool bWarning = false;
	float WarningFade = .35f;
	float WarningAge = 0.f;

	float NoiseDuration = .18f;
	float NoiseIntensity = 0.f;
	float NoiseAge = 1.f;

	float HUDFadeDuration = 0.f;
	float HUDFadeAge = 0.f;
	bool bHUDFading = false;

	/** GameOver UIが指定した警告の濃さ。負の間は演出側（UpdateEffects）が値を決めます。 */
	float UIWarningAlpha = -1.f;

	// ---- ヒットストップ ----
	bool bHitStopActive = false;
	float HitStopRemaining = 0.f;
	float OriginalTimeDilation = 1.f;
	bool bWaitingForLanding = false;

	// ---- ダウン ----
	bool bDownStarted = false;
	bool bDownHeld = false;
	bool bSingleNode = false;
	int32 MontageCheckFrames = 0;

	// ---- 周囲のBoarの跳ね返り ----
	struct FKnockback
	{
		TWeakObjectPtr<AActor> Actor;
		FVector Start;
		FVector Offset;
	};
	TArray<FKnockback> Knockbacks;
	float KnockAge = 0.f;
	float KnockDuration = .4f;
	float KnockHop = 35.f;

	// ---- ガジェットの発光 ----
	float GlowAge = 0.f;
	float GlowDecay = 1.1f;
	bool bGlowActive = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GadgetGlowMID;

	UPROPERTY(Transient)
	TArray<FGameOverGadgetMesh> GadgetMeshes;

	// ---- カメラ（既存SpringArmの元設定を保存し、中断時に戻します） ----
	bool bCameraStarted = false;
	float CameraAge = 0.f;
	float CameraDuration = 1.4f;
	float CameraRiseDuration = .6f;

	/** 後退中だけACFの出力を抑制し、SpringArm演出に最終カメラのAuthorityを渡しているか。 */
	bool bSuppressedACF = false;

	TWeakObjectPtr<USpringArmComponent> Arm;
	TWeakObjectPtr<UCameraComponent> Camera;
	float OrigArmLength = 0.f;
	float OrigFOV = 90.f;
	FVector OrigTargetOffset = FVector::ZeroVector;
	FVector OrigSocketOffset = FVector::ZeroVector;
	TEnumAsByte<ECollisionChannel> OrigProbeChannel = ECC_Camera;
	FRotator StartControlRotation = FRotator::ZeroRotator;
	FVector Focus = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UBoarGameOverEffectWidget> EffectWidget;

	/** EffectWidgetを生成できないと判明済み。毎フレーム同じErrorを出さないために使います。 */
	bool bEffectWidgetUnavailable = false;

	// ---- 画面の暗転 ----
	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> DarkenPostProcess;

	bool bDarkenStarted = false;
	float DarkenAge = 0.f;
	float DarkenFade = 1.2f;
};
