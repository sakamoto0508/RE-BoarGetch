#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameOverPresentationComponent.generated.h"

class UAnimMontage;
class UAnimSequenceBase;
class UBoarGameOverEffectWidget;
class UCurveFloat;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class USoundBase;
class USpringArmComponent;

/** ガジェット発光の減衰中に一時的に変更したMeshの元Overlayです。 */
USTRUCT()
struct FGameOverGadgetMesh
{
 GENERATED_BODY()
 UPROPERTY() TWeakObjectPtr<UMeshComponent> Mesh;
 UPROPERTY() TObjectPtr<UMaterialInterface> OriginalOverlay;
};

UENUM(BlueprintType)
enum class EGameOverPresentationPhase : uint8
{
 Idle,
 /** 赤フラッシュ・ヒットストップ・ダウンアニメ・カメラ後退・警告UI。 */
 Playing,
 /** 完了通知済み。GameOver UIが表示されています。 */
 Finished
};

/**
 * GameOver確定（HP0）後、GameOver UIを出すまでのダウン演出を制御します。
 * 赤フラッシュ＋短いヒットストップ＋低音SE → 上体が反って膝から崩れ、仰向けに倒れて停止するダウンアニメ →
 * 既存SpringArmを使って少し上昇し、倒れたプレイヤーを中央に捉えたまま後退 → 画面周辺の警告ライン・ノイズ・ガジェット発光の減衰・HUD/BGMフェード
 * の順に再生し、完了時にGameModeのCompleteStageEndPresentationへ戻します（Clearでは使用しません）。
 * 時間はすべてGameOver確定からの実時間（秒）です。ヒットストップ中も進み、SafetyTimeoutで必ずUIへ進みます。
 */
UCLASS(ClassGroup=(Presentation), meta=(BlueprintSpawnableComponent, ToolTip="GameOverダウン演出の調整用Component。時間はすべてGameOver確定（HP0）からの実時間（秒）です。"))
class REBOARGETCH_API UGameOverPresentationComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UGameOverPresentationComponent();
 /** GameModeから1回だけ呼びます。開始できない場合はfalseを返し、呼び出し側は従来どおり即時完了します。 */
 bool StartPresentation();
 /** 再生中の演出を中断し、時間倍率・カメラ・Overlay・HUD・Widgetを元に戻します。 */
 void CancelPresentation();
 /** GameOver UI表示時にControllerから呼びます（敗北ジングル、警告ラインを控えめに残す）。 */
 void NotifyGameOverUIShown();
 UFUNCTION(BlueprintPure, Category="GameOver Presentation") bool IsPresentationActive() const { return Phase == EGameOverPresentationPhase::Playing; }
 UFUNCTION(BlueprintPure, Category="GameOver Presentation") EGameOverPresentationPhase GetPhase() const { return Phase; }
 /** 検証用：演出開始からの経過秒数（実時間）。 */
 UFUNCTION(BlueprintPure, Category="GameOver Presentation") float GetPresentationAge() const { return Age; }

 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation", meta=(ToolTip="OFFの場合は演出を行わず、従来どおりGameOver確定後すぐUIを表示します。")) bool bEnabled = true;

 // ---- Impact (0～0.2秒) ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Impact", meta=(ClampMin="0", ClampMax="0.3", ToolTip="ヒットストップの長さ（実時間・秒）。0で無効。終了時・中断時・Level遷移で必ず元の時間倍率へ戻します。")) float HitStopDuration = .09f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Impact", meta=(ClampMin="0.01", ClampMax="1", ToolTip="ヒットストップ中のGlobal Time Dilation。")) float HitStopTimeScale = .05f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Impact", meta=(ClampMin="0", ClampMax="1", ToolTip="赤フラッシュの最大不透明度。")) float FlashPeakAlpha = .5f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Impact", meta=(ClampMin="0.02", ToolTip="赤フラッシュが消えるまでの時間（秒）。")) float FlashDuration = .24f;

 // ---- Down animation (0.2～1.5秒) ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Animation", meta=(ToolTip="上体が反る→よろける→尻もち→仰向けに倒れて停止するダウンMontage（FullBody Slot、最終姿勢で保持）。")) TObjectPtr<UAnimMontage> DownMontage;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Animation", meta=(ToolTip="Slotが評価されない場合のSingle Node再生用。未設定ならMontageの最初のSegmentを使います。")) TObjectPtr<UAnimSequenceBase> DownAnimation;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Animation", meta=(ClampMin="0", ToolTip="ダウンアニメの開始時刻。ヒットストップ明けに合わせます。")) float DownStartTime = .1f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Animation", meta=(ClampMin="0.1")) float DownPlayRate = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Animation", meta=(ClampMin="0", ToolTip="ダウンアニメ開始から地面に倒れ込む瞬間までの時間（倒れ込みSE・ノイズの時刻）。")) float BodyImpactAnimTime = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Animation") bool bAllowSingleNodeFallback = true;

 // ---- Knock-back (接触したBoarが跳ね返り、ダウンを隠さないようにします) ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Knockback", meta=(ClampMin="0", ToolTip="この距離（cm）以内のBoarを跳ね返します。0で無効。")) float KnockbackRadius = 260.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Knockback", meta=(ClampMin="0", ToolTip="跳ね返す距離（cm）。壁はSweepで止まります。")) float KnockbackDistance = 170.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Knockback", meta=(ClampMin="0", ToolTip="跳ね返りの小さな弧の高さ（cm）。")) float KnockbackHopHeight = 35.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Knockback", meta=(ClampMin="0.05")) float KnockbackDuration = .4f;

 // ---- Camera (0.8～2.2秒) ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Camera", meta=(ClampMin="0", ToolTip="カメラ演出の開始時刻。")) float CameraStartTime = .8f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Camera", meta=(ClampMin="0.1", ToolTip="カメラが後退して停止するまでの時間（秒）。")) float CameraDuration = 1.4f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Camera", meta=(ClampMin="0.05", ToolTip="先に少し上昇（見下ろし角へ移行）する時間（秒）。後退より早く落ち着きます。")) float CameraRiseDuration = .6f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Camera", meta=(ClampMin="-80", ClampMax="0", ToolTip="最終的な見下ろし角（度、負で下向き）。")) float CameraEndPitch = -40.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Camera", meta=(ClampMin="-30", ClampMax="30", ToolTip="後退中にゆっくり回り込む角度（度）。急回転を避けるため小さくします。")) float CameraYawDrift = 12.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Camera", meta=(ClampMin="0", ToolTip="SpringArmを伸ばす量（cm）。")) float PullBackDistance = 230.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Camera", meta=(ToolTip="注視点（倒れた身体の中心）へ加えるオフセット（cm、ワールド）。")) FVector FocusOffset = FVector(0, 0, 10);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Camera", meta=(ClampMin="0", ToolTip="注視点の追従速度。小さいほど滑らかです。")) float FocusInterpSpeed = 5.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Camera", meta=(ClampMin="20", ClampMax="120", ToolTip="停止時の視野角（度）。0以下や現在値と同じなら変更しません。")) float CameraEndFOV = 80.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Camera", meta=(ToolTip="演出中にSpringArmの壁判定で使うChannel。BoarのCapsuleはCameraをBlockするため、既定はBoarが無視するVisibilityです（壁・地形は判定）。")) TEnumAsByte<ECollisionChannel> CameraProbeChannel = ECC_Visibility;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Camera", meta=(ToolTip="後退の進行カーブ（0～1）。未設定はEase Outです。")) TObjectPtr<UCurveFloat> CameraCurve;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Camera", meta=(ClampMin="1", ToolTip="Curve未設定時のEase Outの強さ。")) float CameraEaseExponent = 2.6f;

 // ---- Warning UI / VFX (Phase 5) ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ToolTip="赤フラッシュ・警告ライン・ノイズを描画するWidget。未設定ならネイティブ既定Widgetを使います。")) TSubclassOf<UBoarGameOverEffectWidget> EffectWidgetClass;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ClampMin="0")) float WarningStartTime = .25f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ClampMin="0.01")) float WarningFadeInTime = .35f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ClampMin="0", ClampMax="1", ToolTip="GameOver UI表示中に残す警告ラインの強さ。")) float WarningAlphaDuringUI = .35f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ClampMin="0", ClampMax="1", ToolTip="周辺のDark Navy減光の最大値。暗くしすぎないよう控えめにします。")) float VignetteAlpha = .45f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ToolTip="デジタルノイズを出す時刻（秒）。短いバーストを各時刻に1回ずつ出します。")) TArray<float> NoiseBurstTimes;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ClampMin="0.02", ToolTip="ノイズ1回の長さ（秒）。長いグリッチは避けます。")) float NoiseBurstDuration = .18f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ClampMin="0", ClampMax="1")) float NoiseIntensity = .8f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ToolTip="ガジェットに掛ける発光Overlay（GlowAmount / GlowColor / GlowStrength）。演出専用MIDを作り元Materialは変更しません。")) TObjectPtr<UMaterialInterface> GadgetGlowMaterial;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning") FLinearColor GadgetGlowColor = FLinearColor(.02f, .8f, 1.f);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ClampMin="0", ToolTip="ガジェットが一瞬光ってから減衰し始める時刻。")) float GadgetGlowStartTime = .15f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ClampMin="0.05", ToolTip="ガジェット発光が明滅しながら消えるまでの時間（秒）。")) float GadgetGlowDecayTime = 1.1f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ClampMin="0")) float HUDFadeStartTime = .15f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Warning", meta=(ClampMin="0.01")) float HUDFadeOutTime = .4f;

 // ---- Audio (BoarAudioManagerSubsystem経由。音量設定は変更しません) ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Audio", meta=(ToolTip="被弾時の重い低音SE。")) TObjectPtr<USoundBase> ImpactSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Audio", meta=(ClampMin="0.1", ClampMax="2")) float ImpactPitch = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Audio", meta=(ToolTip="デジタルノイズ（各バースト）のSE。")) TObjectPtr<USoundBase> NoiseSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Audio", meta=(ToolTip="システム停止（電源が落ちる）SE。")) TObjectPtr<USoundBase> SystemDownSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Audio", meta=(ClampMin="0")) float SystemDownSoundTime = .45f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Audio", meta=(ToolTip="地面に倒れ込んだときのSE。")) TObjectPtr<USoundBase> BodyFallSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Audio", meta=(ToolTip="GameOver UI表示時の短い敗北ジングル（2D SEとして1回）。")) TObjectPtr<USoundBase> DefeatJingle;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Audio", meta=(ClampMin="0", ToolTip="ステージBGMのフェードアウト時間（秒）。既存StopBGMを使用します。")) float BGMFadeOutTime = .8f;

 // ---- Completion ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Timing", meta=(ClampMin="0", ToolTip="ダウン完了・カメラ停止の両方が終わってからUIを出すまでの余韻（秒）。")) float UIDelayAfterSettle = .3f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Timing", meta=(ClampMin="0.5", ToolTip="ダウンアニメが無い・Playerが消えた場合でもUIを出す時刻（秒）。")) float FallbackFinishTime = 2.4f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GameOver Presentation|Timing", meta=(ClampMin="1", ToolTip="演出全体の上限時間（秒）。超えたら残りを省略してUIへ進みます。")) float SafetyTimeout = 6.f;

protected:
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
 class ABoarPlayerCharacter* GetPlayer() const;
 void PlaySound(USoundBase* Sound, float Pitch = 1.f) const;
 void BeginHitStop();
 void EndHitStop();
 void StartDownAnimation();
 void UpdateDownAnimation();
 void BeginCamera();
 void UpdateCamera(float Dt);
 FVector ComputeBodyFocus() const;
 void BeginGadgetGlow();
 void UpdateGadgetGlow();
 void UpdateEffects();
 void UpdateLanding();
 void Finish();
 void RestoreAll();
 void BeginKnockback();
 void UpdateKnockback();

 struct FKnockback { TWeakObjectPtr<AActor> Actor; FVector Start; FVector Offset; };
 TArray<FKnockback> Knockbacks;

 EGameOverPresentationPhase Phase = EGameOverPresentationPhase::Idle;
 float Age = 0.f;
 double LastRealTime = 0.0;
 float FinishTime = 0.f;
 float DownLength = 0.f;
 bool bHitStopActive = false, bDownStarted = false, bDownHeld = false, bSingleNode = false, bBodyImpactPlayed = false;
 bool bCameraStarted = false, bGadgetGlowStarted = false, bSystemDownPlayed = false, bWaitingForLanding = false;
 int32 MontageCheckFrames = 0;
 TSet<int32> PlayedNoiseBursts;
 float OriginalTimeDilation = 1.f;
 // Camera（既存SpringArmの元設定を保存し、中断時に戻します）
 TWeakObjectPtr<USpringArmComponent> Arm;
 TWeakObjectPtr<class UCameraComponent> Camera;
 float OrigArmLength = 0.f, OrigFOV = 90.f;
 FVector OrigTargetOffset = FVector::ZeroVector, OrigSocketOffset = FVector::ZeroVector;
 bool bOrigCameraLag = false, bOrigRotationLag = false;
 TEnumAsByte<ECollisionChannel> OrigProbeChannel = ECC_Camera;
 FRotator StartControlRotation = FRotator::ZeroRotator;
 FVector Focus = FVector::ZeroVector;
 float CameraAge = 0.f;
 UPROPERTY(Transient) TObjectPtr<UBoarGameOverEffectWidget> EffectWidget;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GadgetGlowMID;
 UPROPERTY(Transient) TArray<FGameOverGadgetMesh> GadgetMeshes;
 float UIWarningAlpha = -1.f;
};
