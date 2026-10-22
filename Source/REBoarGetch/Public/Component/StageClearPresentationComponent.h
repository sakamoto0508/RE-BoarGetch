#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StageClearPresentationComponent.generated.h"

class ACameraActor;
class UAnimMontage;
class UCurveFloat;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UPrimitiveComponent;
class USoundBase;
class UStaticMeshComponent;

/** 消失演出中に一時的に置き換えたMeshの元状態です。 */
USTRUCT()
struct FStageClearHiddenMesh
{
 GENERATED_BODY()
 UPROPERTY() TWeakObjectPtr<UPrimitiveComponent> Source;
 UPROPERTY() TObjectPtr<UMaterialInterface> OriginalOverlay;
 UPROPERTY() TObjectPtr<UMeshComponent> DataProxy;
 bool bOriginalVisible = true;
 bool bOriginalHiddenInGame = false;
};

USTRUCT()
struct FStageClearFragment
{
 GENERATED_BODY()
 UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
 FVector Start = FVector::ZeroVector;
 FVector Drift = FVector::ZeroVector;
 float Delay = 0.f;
};

UENUM(BlueprintType)
enum class EStageClearPresentationPhase : uint8
{
 Idle,
 WaitingCapture,
 Victory,
 Retreat,
 FadeOut,
 Finished
};

/**
 * ステージクリア確定後、Result表示までの勝利演出を制御します。
 * 勝利カメラ → Victory Montage → 星VFX → データ化して消失 → 注視点を見ながら後退 → 暗転の順に再生し、
 * 完了時にGameModeのCompleteStageEndPresentationへ戻します。GameOverでは使用しません。
 * Player/網のActorはDestroyせず、Mesh表示とOverlayだけを一時的に変更し元の状態を保持します。
 */
UCLASS(ClassGroup=(Presentation), meta=(BlueprintSpawnableComponent, ToolTip="ステージクリア勝利演出の調整用Component。時間はすべてVictory Montage開始からの秒数です。"))
class REBOARGETCH_API UStageClearPresentationComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UStageClearPresentationComponent();
 /** GameModeから1回だけ呼びます。開始できない場合はfalseを返し、呼び出し側は従来どおり即時完了します。 */
 bool StartPresentation();
 /** 再生中の演出を中断し、カメラ・Mesh・VFXを元に戻します。 */
 void CancelPresentation();
 UFUNCTION(BlueprintPure, Category="Stage Clear Presentation") bool IsPresentationActive() const { return Phase != EStageClearPresentationPhase::Idle && Phase != EStageClearPresentationPhase::Finished; }
 UFUNCTION(BlueprintPure, Category="Stage Clear Presentation") EStageClearPresentationPhase GetPhase() const { return Phase; }
 /** 消失地点として固定したワールド座標です。カメラ後退中も常にこの点を注視します。 */
 UFUNCTION(BlueprintPure, Category="Stage Clear Presentation") FVector GetVictoryFocusPoint() const { return VictoryFocusPoint; }

 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation", meta=(ToolTip="OFFの場合は演出を行わず、従来どおりClear確定後すぐResultへ進みます。")) bool bEnabled = true;

 // ---- Timing (seconds from Victory Montage start unless noted) ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Timing", meta=(ClampMin="0", ToolTip="最後の捕獲演出の完了を待つ最大時間（秒）。超えた場合は捕獲演出を中断して勝利演出へ進みます。")) float CaptureWaitTimeout = 2.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Timing", meta=(ClampMin="0", ToolTip="勝利カメラへBlendし始めてからMontageを開始するまでの時間（秒）。")) float MontageStartDelay = .25f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Timing", meta=(ClampMin="0", ToolTip="ジャンプSEの再生時刻（踏み切り）。")) float JumpSoundTime = .22f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Timing", meta=(ClampMin="0", ToolTip="網を最も高く掲げた瞬間。星VFXと星SEを出します。")) float StarBurstTime = .62f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Timing", meta=(ClampMin="0", ToolTip="身体と網がシアンに発光し始める時刻。")) float GlowStartTime = 1.05f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Timing", meta=(ClampMin="0", ToolTip="データ化（下から上への分解）を開始する時刻。転送開始SEを再生します。")) float DissolveStartTime = 1.4f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Timing", meta=(ClampMin="0.05", ToolTip="分解して完全に消えるまでの時間（秒）。完了時に転送完了SEを再生しカメラ後退を始めます。")) float DissolveDuration = .75f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Timing", meta=(ClampMin="0", ToolTip="カメラ後退の開始を消失完了から遅らせる時間（秒）。負の重なりは使いません。")) float RetreatDelay = 0.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Timing", meta=(ClampMin="0.1", ToolTip="カメラ後退の長さ（秒）。")) float RetreatDuration = 2.1f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Timing", meta=(ClampMin="0.05", ToolTip="残ったデータ粒子と星が消えるまでの時間（秒）。カメラ後退の終盤に重ねて消します。")) float ParticleFadeDuration = .7f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Timing", meta=(ClampMin="0", ToolTip="暗転の長さ（秒）。暗転完了後にResultを表示します。")) float FadeOutDuration = .6f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Timing", meta=(ClampMin="3", ToolTip="演出全体の上限時間（秒）。超えた場合は残りを省略してResultへ進み、無限待機を防ぎます。")) float SafetyTimeout = 14.f;

 // ---- Camera ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Camera", meta=(ToolTip="VictoryFocusPointをプレイヤー足元から見た位置（cm、プレイヤーの向き基準ではなくワールドZ）。")) FVector VictoryFocusOffset = FVector(0, 0, 125);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Camera", meta=(ClampMin="50", ToolTip="勝利カメラとVictoryFocusPointの水平距離（cm）。")) float CameraDistance = 450.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Camera", meta=(ToolTip="勝利カメラのVictoryFocusPointからの高さ（cm）。")) float CameraHeight = 15.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Camera", meta=(ClampMin="-90", ClampMax="90", ToolTip="プレイヤー正面から横へずらす角度（度）。正面すぎない斜めの構図にします。")) float CameraYawOffset = 28.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Camera", meta=(ClampMin="0", ToolTip="通常カメラから勝利カメラへのBlend時間（秒）。")) float CameraBlendTime = .5f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Camera", meta=(ClampMin="20", ClampMax="120", ToolTip="勝利カメラの視野角（度）。推奨50～60。")) float VictoryFOV = 55.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Camera", meta=(ClampMin="20", ClampMax="120", ToolTip="後退終了時の視野角（度）。")) float RetreatEndFOV = 58.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Camera", meta=(ClampMin="0", ToolTip="後退する水平距離（cm）。")) float RetreatDistance = 400.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Camera", meta=(ToolTip="後退中に上昇する高さ（cm）。")) float RetreatHeight = 100.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Camera", meta=(ToolTip="後退の進行カーブ（0～1の時間→0～1の進行）。未設定はEase In/Outです。")) TObjectPtr<UCurveFloat> RetreatCurve;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Camera", meta=(ClampMin="1", ToolTip="Curve未設定時のEase In/Outの強さ。")) float RetreatEaseExponent = 2.2f;

 // ---- Animation ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Animation", meta=(ToolTip="小ジャンプして網を高く掲げるVictory Montage。FullBody Slotで再生します。")) TObjectPtr<UAnimMontage> VictoryMontage;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Animation", meta=(ClampMin="0.1")) float MontagePlayRate = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Animation", meta=(ToolTip="AnimBPのMontage Slotが評価されない状態（例：Slotがガジェット使用Stateの中にある）では、同じアニメをSingle Nodeで再生します。未設定ならMontageの最初のSegmentを使います。")) TObjectPtr<class UAnimSequenceBase> VictoryAnimation;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Animation", meta=(ToolTip="Montage SlotがAnimGraphで有効でない場合にSingle Node再生へ切り替えます。演出終了・中断時にAnimBPへ戻します。")) bool bAllowSingleNodeFallback = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Animation", meta=(ClampMin="0", ToolTip="Montageを再生できない場合のFallback：Meshを上下させる小ジャンプの高さ（cm）。")) float FallbackJumpHeight = 30.f;

 // ---- VFX ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|VFX", meta=(ToolTip="網の先端から放つ星型Niagara。User.Colorで色を受け取ります。")) TObjectPtr<UNiagaraSystem> StarVFX;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|VFX", meta=(ToolTip="星VFXを色ごとに1つずつ生成します。既定は黄・白・シアン。")) TArray<FLinearColor> StarColors;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|VFX", meta=(ToolTip="星VFXの出現位置。網Meshの上端からのオフセット（cm）。")) FVector StarOffset = FVector(0, 0, 10);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|VFX", meta=(ClampMin="0.01")) float StarScale = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|VFX", meta=(ToolTip="分解中に身体と網へ使うデータ化Material（CaptureMinZ / CaptureHeight / DissolveProgress / GlowColor）。")) TObjectPtr<UMaterialInterface> DataMaterial;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|VFX", meta=(ToolTip="分解前のシアン発光Overlayとキューブ粒子のMaterial（GlowAmount / GlowColor / GlowStrength）。")) TObjectPtr<UMaterialInterface> GlowMaterial;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|VFX", meta=(ToolTip="消失時に足元へ出す転送リング（任意）。")) TObjectPtr<UNiagaraSystem> TransferVFX;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|VFX") FLinearColor DataColor = FLinearColor(.02f, .8f, 1.f);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|VFX", meta=(ClampMin="0", ClampMax="64", ToolTip="分解時に舞い上がるキューブ状データ粒子の数。")) int32 FragmentCount = 28;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|VFX", meta=(ClampMin="0", ToolTip="データ粒子の上昇距離（cm/秒の目安）。")) float FragmentRiseSpeed = 120.f;

 // ---- Audio (BoarAudioManagerSubsystem経由。音量設定は変更しません) ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Audio", meta=(ClampMin="0", ToolTip="ステージBGMをフェードアウトする時間（秒）。")) float StageBGMFadeOutTime = .45f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Audio", meta=(ToolTip="勝利ジングル。BGMチャンネルで1回再生します。")) TObjectPtr<USoundBase> VictoryJingle;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Audio", meta=(ClampMin="0", ToolTip="勝利ジングルの再生時刻（Montage開始から）。")) float VictoryJingleTime = .0f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Audio") TObjectPtr<USoundBase> JumpSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Audio") TObjectPtr<USoundBase> StarSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Audio") TObjectPtr<USoundBase> TransferStartSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Audio") TObjectPtr<USoundBase> TransferCompleteSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Audio", meta=(ToolTip="Result表示時に再生するBGM（任意）。未設定ならジングルの余韻を残します。")) TObjectPtr<USoundBase> ResultBGM;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage Clear Presentation|Audio", meta=(ClampMin="0")) float ResultBGMFadeInTime = .8f;

 /** Result表示時にControllerから呼び、Result BGMを開始します。 */
 void NotifyResultShown();

protected:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
 void BeginVictory();
 void SpawnStars();
 void BeginGlow();
 void BeginDissolve();
 void UpdateDissolve(float Progress);
 void FinishDissolve();
 void BeginRetreat();
 void UpdateCamera(float RetreatAlpha);
 void BeginFadeOut();
 void Finish();
 void RestoreVisuals();
 void RestoreCamera();
 void PlaySound(USoundBase* Sound) const;
 void CollectMeshes();
 float EvaluateRetreat(float T) const;
 class ABoarPlayerCharacter* GetPlayer() const;
 FVector ComputeCameraLocation(float RetreatAlpha) const;

 EStageClearPresentationPhase Phase = EStageClearPresentationPhase::Idle;
 float PhaseAge = 0.f, TotalAge = 0.f, VictoryAge = 0.f, RetreatAge = 0.f;
 bool bJumpSoundPlayed = false, bJinglePlayed = false, bStarsSpawned = false, bGlowStarted = false, bDissolveStarted = false, bDissolveFinished = false;
 bool bMontagePlaying = false, bMontageHeld = false, bFallbackJump = false, bSingleNodePlayback = false;
 int32 MontageCheckFrames = 0;
 FVector VictoryFocusPoint = FVector::ZeroVector;
 FVector CameraDirection = FVector::ForwardVector;
 float MaxRetreatDistance = 0.f, MaxRetreatHeight = 0.f;
 FVector MeshOriginalRelativeLocation = FVector::ZeroVector;
 float DissolveMinZ = 0.f, DissolveHeight = 150.f;
 UPROPERTY(Transient) TObjectPtr<ACameraActor> VictoryCamera;
 UPROPERTY(Transient) TWeakObjectPtr<AActor> OriginalViewTarget;
 UPROPERTY(Transient) TArray<FStageClearHiddenMesh> HiddenMeshes;
 UPROPERTY(Transient) TArray<FStageClearFragment> Fragments;
 UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> StarComponents;
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> TransferComponent;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GlowMID;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DataMID;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FragmentMID;
};
