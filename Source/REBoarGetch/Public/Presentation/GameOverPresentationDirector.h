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
class USpringArmComponent;

/** ガジェット発光の減衰中に一時的に変更したMeshの元Overlayです。 */
USTRUCT()
struct FGameOverGadgetMesh
{
	GENERATED_BODY()
	UPROPERTY() TWeakObjectPtr<UMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> OriginalOverlay;
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
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation") void BeginRedFlash(float PeakAlpha = .5f, float Duration = .24f);
	/** 実時間でDuration秒だけGlobal Time Dilationを下げ、必ず元の値へ戻します。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation") void BeginHitStop(float Duration = .09f, float TimeScale = .05f);
	/** 空中でHP0になった場合は、着地するまで重力だけを有効にします（入力は遮断済み）。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation") void SettlePlayerOnGround();
	/** 近くのBoarを跳ね返し、ダウンを隠さないようにします（カメラとの間にいる個体は横へ）。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation") void KnockBackNearbyBoars(float Radius = 260.f, float Distance = 170.f, float HopHeight = 35.f, float Duration = .4f);
	/** ダウンMontageを再生し最終姿勢で保持します（Slot非評価ならSingle Node）。再生した長さ（秒）を返します。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation") float PlayDownAnimation();
	/** 網などの装備へ発光Overlayを掛け、一瞬光ってから明滅しながらDecayTime秒で消します。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation") void BeginGadgetGlowDecay(float DecayTime = 1.1f);
	/** 通常HUDをDuration秒でフェードアウトします。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation") void FadeOutHUD(float Duration = .4f);
	/** 画面周辺の警告ラインと周辺減光をFadeInTime秒で表示します。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation") void BeginWarning(float FadeInTime = .35f);
	/** 短いデジタルノイズ（Duration秒で減衰）。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation") void PlayNoiseBurst(float Duration = .18f, float Intensity = .8f);
	/** 既存SpringArmで少し上昇→倒れた身体を中央に捉えたまま後退します（Ease Out、Yawは少しだけ回り込み）。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation") void BeginCameraPullBack(float Duration = 1.4f, float RiseDuration = .6f);
	/** GameOver UI表示中に残す警告ラインの強さを設定します。 */
	UFUNCTION(BlueprintCallable, Category = "GameOver Presentation") void SetWarningAlphaForUI(float Alpha = .35f);

	/** GameOver UI表示時に呼ばれます（敗北ジングルなど）。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "GameOver Presentation") void OnGameOverUIShown();

	// ---- Animation ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Animation", meta = (ToolTip = "上体が反る→よろける→尻もち→仰向けに倒れて停止するダウンMontage（FullBody Slot、最終姿勢で保持）。")) TObjectPtr<UAnimMontage> DownMontage;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Animation", meta = (ToolTip = "Slot非評価時のSingle Node再生用。未設定ならMontageの最初のSegment。")) TObjectPtr<UAnimSequenceBase> DownAnimation;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Animation", meta = (ClampMin = "0.1")) float DownPlayRate = 1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Animation") bool bAllowSingleNodeFallback = true;

	// ---- Camera ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "-80", ClampMax = "0", ToolTip = "最終的な見下ろし角（度）。")) float CameraEndPitch = -40.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "-30", ClampMax = "30")) float CameraYawDrift = 12.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "0", ToolTip = "SpringArmを伸ばす量（cm）。")) float PullBackDistance = 230.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ToolTip = "注視点（倒れた身体の中心）へ加えるオフセット（cm、ワールド）。")) FVector FocusOffset = FVector(0, 0, 10);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "0")) float FocusInterpSpeed = 5.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "20", ClampMax = "120")) float CameraEndFOV = 80.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ToolTip = "演出中にSpringArmの壁判定で使うChannel。BoarのCapsuleはCameraをBlockするため既定はVisibility。")) TEnumAsByte<ECollisionChannel> CameraProbeChannel = ECC_Visibility;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ToolTip = "後退の進行カーブ（0～1）。未設定はEase Out。")) TObjectPtr<UCurveFloat> CameraCurve;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Camera", meta = (ClampMin = "1")) float CameraEaseExponent = 2.6f;

	// ---- Warning / VFX ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Warning", meta = (ToolTip = "赤フラッシュ・警告ライン・ノイズを描画するWidget。未設定ならネイティブ既定。")) TSubclassOf<UBoarGameOverEffectWidget> EffectWidgetClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Warning", meta = (ClampMin = "0", ClampMax = "1")) float VignetteAlpha = .45f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Warning", meta = (ToolTip = "装備に掛ける発光Overlay（GlowAmount / GlowColor）。演出専用MIDを作り元Materialは変更しません。")) TObjectPtr<UMaterialInterface> GadgetGlowMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameOver Presentation|Warning") FLinearColor GadgetGlowColor = FLinearColor(.02f, .8f, 1.f);

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

	// Effect widget values
	float FlashPeak = 0.f, FlashDuration = .24f, FlashAge = 0.f;
	bool bWarning = false; float WarningFade = .35f, WarningAge = 0.f;
	float NoiseDuration = .18f, NoiseIntensity = 0.f, NoiseAge = 1.f;
	float HUDFadeDuration = 0.f, HUDFadeAge = 0.f; bool bHUDFading = false;
	float UIWarningAlpha = -1.f;
	// Hit stop
	bool bHitStopActive = false; float HitStopRemaining = 0.f; float OriginalTimeDilation = 1.f;
	bool bWaitingForLanding = false;
	// Down
	bool bDownStarted = false, bDownHeld = false, bSingleNode = false; int32 MontageCheckFrames = 0;
	// Knockback
	struct FKnockback { TWeakObjectPtr<AActor> Actor; FVector Start; FVector Offset; };
	TArray<FKnockback> Knockbacks; float KnockAge = 0.f, KnockDuration = .4f, KnockHop = 35.f;
	// Gadget glow
	float GlowAge = 0.f, GlowDecay = 1.1f; bool bGlowActive = false;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GadgetGlowMID;
	UPROPERTY(Transient) TArray<FGameOverGadgetMesh> GadgetMeshes;
	// Camera（既存SpringArmの元設定を保存し、中断時に戻します）
	bool bCameraStarted = false; float CameraAge = 0.f, CameraDuration = 1.4f, CameraRiseDuration = .6f;
	TWeakObjectPtr<USpringArmComponent> Arm;
	TWeakObjectPtr<UCameraComponent> Camera;
	float OrigArmLength = 0.f, OrigFOV = 90.f;
	FVector OrigTargetOffset = FVector::ZeroVector, OrigSocketOffset = FVector::ZeroVector;
	TEnumAsByte<ECollisionChannel> OrigProbeChannel = ECC_Camera;
	FRotator StartControlRotation = FRotator::ZeroRotator;
	FVector Focus = FVector::ZeroVector;
	UPROPERTY(Transient) TObjectPtr<UBoarGameOverEffectWidget> EffectWidget;
};
