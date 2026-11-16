#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Effects/ACFCameraEffectTypes.h"
#include "CapturePresentationComponent.generated.h"

class ABoarBase;
class ABoarGameMode;
class UCameraComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;

UCLASS(Abstract)
class REBOARGETCH_API UCaptureSuccessCameraShake : public UCameraShakeBase
{
 GENERATED_BODY()
public:
 UCaptureSuccessCameraShake(const FObjectInitializer& ObjectInitializer);
};

USTRUCT()
struct FCaptureVisualTarget
{
 GENERATED_BODY()
 UPROPERTY() TWeakObjectPtr<ABoarBase> Boar;
 UPROPERTY() TWeakObjectPtr<USkeletalMeshComponent> Mesh;
 UPROPERTY() TObjectPtr<UMaterialInterface> OriginalOverlay;
 UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Glow;
 UPROPERTY() TObjectPtr<USkeletalMeshComponent> DataMesh;
 UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DataMaterial;
 /** 足元の捕獲リング（CaptureVFX）。 */
 UPROPERTY() TObjectPtr<UNiagaraComponent> VFX;
 /** データ化中に上昇するデータ粒子（DataFragmentVFX）。捕獲リングとは別に保持します。 */
 UPROPERTY() TObjectPtr<UNiagaraComponent> DataVFX;
 bool bOriginalVisible = true;
 bool bDataStarted = false;
 /** RestoreTarget済み。復元は一度だけ行い、後から別処理が変えた見た目を戻さない。 */
 bool bRestored = false;
 FVector CaptureLocation = FVector::ZeroVector;
 float CaptureMinZ = 0.f;
 float CaptureHeight = 150.f;
 float Age = 0.f;
};

/** 捕獲地点の周囲で一時的に減速しているBoar。値はこのComponentが設定したものだけを元へ戻します。 */
USTRUCT()
struct FCaptureSlowedBoar
{
 GENERATED_BODY()
 UPROPERTY() TWeakObjectPtr<ABoarBase> Boar;
 /** 減速前のMeshのGlobalAnimRateScaleと、このComponentが設定した値。 */
 float OriginalAnimRate = 1.f;
 float AppliedAnimRate = 1.f;
 /** アニメーション倍率・移動時間倍率をこのComponentが設定中か。 */
 bool bAnimApplied = false;
 bool bMoveApplied = false;
 /** 他の処理がアニメーション倍率を変えたため、この演出では以後触らない。 */
 bool bAnimOwnershipLost = false;
};

USTRUCT()
struct FCaptureReception
{
 GENERATED_BODY()
 UPROPERTY() TObjectPtr<UNiagaraComponent> VFX;
 float Age = 0.f;
};

/** 捕獲成功時の見た目・音・カメラを制御します。捕獲判定とCageへの収容は既存のGameplay処理が担当します。 */
UCLASS(ClassGroup=(Presentation), meta=(BlueprintSpawnableComponent, ToolTip="捕獲成功演出の調整用Component。捕獲地点の演出時間はHit Stop、Slow、Dissolve、Transfer Finishの合計です。Cage受信演出はその後に再生します。"))
class REBOARGETCH_API UCapturePresentationComponent : public UActorComponent
{
 friend class FBoarStageEndRegressionTest;
 GENERATED_BODY()
public:
 UCapturePresentationComponent();
 bool PresentCapturedBoar(ABoarBase* Boar, bool bSharedFeedback);
 /** 演出を打ち切ります。bCommitCaptures時は演出待ちの捕獲をGameModeへ確定させます（Stage終了後はGameMode側で無視されます）。 */
 void CancelPresentation(bool bCommitCaptures = true);
 bool HasPendingCaptures() const { return bCommittingCaptures || !Targets.IsEmpty() || !Receptions.IsEmpty(); }
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation", meta=(ToolTip="捕獲成功演出を有効にします。OFFでも捕獲判定とCageへの収容は通常どおり行われます。")) bool bEnabled = true;
 // Hit Stop／Slowは、捕獲地点の周囲にいる未捕獲Boarだけを減速します。World全体の時間（Global Time Dilation）、
 // Player、AI（StateTree）、Timer、Stage進行、Cage、UI・SE・VFXの時間は変えません。
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0",ClampMax="0.12", ToolTip="網が当たった直後に周囲のBoarを止めて見せる長さ（実時間・秒）。推奨0.08～0.12秒。Net 1回につき1回だけ適用します。")) float HitStopDuration = .1f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0",ClampMax="0.6", ToolTip="Hit Stop後に周囲のBoarをゆっくり動かす長さ（実時間・秒）。足元リングとシアン発光を見せる時間です。")) float SlowDuration = .27f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Local Slow", meta=(ClampMin="0",ClampMax="5000",Units="cm", ToolTip="減速させるBoarの範囲（捕獲地点からの半径）。捕獲されたBoar自身とPlayerは対象外です。0なら周囲のBoarを減速しません。")) float LocalSlowRadius = 1500.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Local Slow", meta=(ClampMin="0.001",ClampMax="1", ToolTip="Hit Stop中の周囲Boarのアニメーション再生倍率（MeshのGlobalAnimRateScale）。0に近いほど止まって見えます。初期値0.01。")) float HitStopAnimRate = .01f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Local Slow", meta=(ClampMin="0.01",ClampMax="1", ToolTip="Slow中の周囲Boarのアニメーション再生倍率（MeshのGlobalAnimRateScale）。初期値0.3。")) float SlowAnimRate = .3f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Local Slow", meta=(ToolTip="ONなら移動の時間倍率にアニメーション再生倍率と同じ値を使います。ABPは実際の速度から足の再生速度を合わせるため、同じ値にすると足と移動がずれません。")) bool bMatchMovementToAnimation = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Local Slow", meta=(ClampMin="0.001",ClampMax="1",EditCondition="!bMatchMovementToAnimation", ToolTip="Hit Stop中の周囲Boarの移動時間倍率（移動・加速・旋回）。MaxWalkSpeedは変えません。")) float HitStopMoveScale = .01f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Local Slow", meta=(ClampMin="0.01",ClampMax="1",EditCondition="!bMatchMovementToAnimation", ToolTip="Slow中の周囲Boarの移動時間倍率（移動・加速・旋回）。MaxWalkSpeedは変えません。")) float SlowMoveScale = .3f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.05",ClampMax="0.7", ToolTip="Boarが下から上へデータ化される長さ（実時間・秒）。長くするとScanと分解をゆっくり見せられます。")) float CaptureDissolveDuration = .3f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.05",ClampMax="0.4", ToolTip="データ化後、Boarの残像と破片が上昇して消えるまでの長さ（実時間・秒）。")) float TransferFinishDuration = .18f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.05",ClampMax="0.5", ToolTip="Cageへ収容した直後のシアン受信エフェクトの長さ（実時間・秒）。捕獲地点の演出時間には含まれません。")) float CageReceptionDuration = .18f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera", meta=(ClampMin="0",ClampMax="20", ToolTip="捕獲時に一時的に狭める視野角（度）。大きいほどPunch Inが強くなります。位置やCamera Actorは切り替えません。")) float CameraZoomAmount = 12.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera", meta=(ClampMin="0.01",ClampMax="1", ToolTip="Punch Inして元の視野角へ戻るまでの時間（実時間・秒）。")) float CameraDuration = .38f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ToolTip="転送中だけ表示するBoar用Material。下から上へのDissolve、シアンのScan境界、データ線を描きます。元のBoar Materialは変更しません。")) TObjectPtr<UMaterialInterface> DataMaterial;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ToolTip="転送前のBoarのシアン発光に使うMaterial。未設定だと発光が表示されません。")) TObjectPtr<UMaterialInterface> GlowMaterial;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ToolTip="捕獲地点のBoar足元に出すシアンリングのNiagara System。拡大してから薄くなります。")) TObjectPtr<UNiagaraSystem> CaptureVFX;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ToolTip="Cageへの収容直後に出す受信エフェクト。未設定の場合は受信エフェクトを表示しません（Errorを出します）。")) TObjectPtr<UNiagaraSystem> CageReceptionVFX;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Color", meta=(ToolTip="捕獲リング・データ化Material・データ片に使う基本色です。")) FLinearColor CaptureColor = FLinearColor(.01f,.75f,1.f);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Color", meta=(ToolTip="命中直後のBoar発光の開始色です。データ化開始までにGlow Peak Colorへ変化します。")) FLinearColor GlowStartColor = FLinearColor(.01f,.52f,.9f);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Color", meta=(ToolTip="データ化直前のBoar発光の色です。")) FLinearColor GlowPeakColor = FLinearColor(.04f,.88f,1.f);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Color", meta=(ToolTip="Cageへ収容した直後の受信エフェクトの色です。")) FLinearColor ReceptionColor = FLinearColor(.02f,.9f,1.f);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ClampMin="0.01", ToolTip="捕獲リングとCage受信エフェクトの基準Scale。大きくすると両方のリングが広がります。")) float VFXScale = .55f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ToolTip="データ化開始時に捕獲地点から上昇するデータ粒子（四角い破片と縦長の光片）のNiagara System。粒子数・サイズ・速度・遅延・消失はこのSystemで調整します。未設定なら粒子を表示しません（捕獲処理には影響しません）。")) TObjectPtr<UNiagaraSystem> DataFragmentVFX;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ClampMin="0",ClampMax="250", ToolTip="転送完了時にBoarのデータ残像が上へ移動する距離（cm）。")) float TransferRiseDistance = 105.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Audio", meta=(ToolTip="網が命中した瞬間の効果音。Net 1回につき1回、既存AudioManager経由で再生します。未設定なら無音です。")) TObjectPtr<USoundBase> ImpactSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Audio", meta=(ToolTip="Boarのデータ化が始まる瞬間の転送音。既存AudioManager経由で再生します。未設定なら無音です。")) TObjectPtr<USoundBase> TransferStartSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Audio", meta=(ToolTip="捕獲地点での転送が完了した瞬間の確定音。Net 1回につき1回、既存AudioManager経由で再生します。未設定なら無音です。")) TObjectPtr<USoundBase> CaptureSuccessSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera", meta=(ToolTip="網の命中時に再生する小さなCamera Shake。Net 1回につき1回だけ再生します。振幅・周波数・Blend時間はBP_CaptureSuccessCameraShakeのClass Defaults（Root Shake Pattern）で調整します。未設定なら揺れません。")) TSubclassOf<UCameraShakeBase> CameraShake;
 // ---- 命中インパクト（AKI-137）。捕獲の確定とは別に、命中した瞬間だけを短く強調します。 ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact", meta=(ToolTip="網が命中した瞬間にBoarの体の位置で1回だけ出す短いNiagara（星形の弾け）。捕獲したBoarごとに出ます。粒子数・大きさ・寿命はこのSystemで調整します。未設定なら出しません。")) TObjectPtr<UNiagaraSystem> ImpactVFX;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact", meta=(ToolTip="命中インパクトの色（User.Colorへ渡します）。成功の瞬間なので白～黄色の明るい色を推奨します。")) FLinearColor ImpactColor = FLinearColor(1.f,.92f,.45f);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact", meta=(ClampMin="0.01",ClampMax="5", ToolTip="命中インパクトのScale。")) float ImpactVFXScale = .6f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact", meta=(ClampMin="0",ClampMax="1", ToolTip="命中インパクトを出す高さ（Boarの足元=0、頭頂=1）。")) float ImpactHeightRatio = .6f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact", meta=(ClampMin="0",ClampMax="0.3",Units="s", ToolTip="命中直後にBoarを白く光らせる長さ（実時間・秒）。その後はGlow Start Colorからの通常の発光へ移ります。0で無効。")) float HitFlashDuration = .08f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact", meta=(ToolTip="命中直後のフラッシュの色。")) FLinearColor HitFlashColor = FLinearColor(1.f,1.f,1.f);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact", meta=(ClampMin="0",ClampMax="5", ToolTip="命中直後のフラッシュの強さ（Glow MaterialのGlowAmount）。通常の発光の最大は1です。")) float HitFlashGlowAmount = 2.2f;
 // ---- 転送（吸い込み） ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ClampMin="0.05",ClampMax="1", ToolTip="転送完了時のデータ残像の横方向の大きさ（元=1）。上昇しながら細く吸い込まれて消えます。1で縮めません。元のBoarのMeshは変更しません。")) float TransferEndWidthScale = .35f;
 // ---- ACFカメラ演出。ACFの通常カメラが出ている間だけ使い、出ていなければ従来のFOV Punch In（FollowCamera）へ戻します。 ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera", meta=(ToolTip="ACFカメラ使用中に、Net 1回につき1回再生するCamera Effect Preset（FOV Punch・捕獲地点から押し返す方向性のあるImpulse）。強さ・時間はPresetのLayerで調整します。ACFのRequestが無いときと未設定のときは、Camera Zoom AmountのFOV Punch Inを使います。")) TObjectPtr<class UACFCameraEffectPreset> CameraEffect;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera", meta=(ToolTip="1回のNetでMulti Capture Threshold体以上を捕獲したときだけ、通常のCamera Effectと入れ替えて再生する強めのPreset。未設定なら通常のPresetのままです。")) TObjectPtr<class UACFCameraEffectPreset> MultiCaptureCameraEffect;
 // ---- 複数体捕獲 ----
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Multi Capture", meta=(ClampMin="2",ClampMax="10", ToolTip="1回のNetで何体以上捕獲したら複数体の強調（Multi Capture Sound・Multi Capture Camera Effect）を出すか。強調はNet 1回につき1回だけです。")) int32 MultiCaptureThreshold = 2;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Multi Capture", meta=(ToolTip="複数体捕獲になった瞬間に1回だけ鳴らす追加の効果音。未設定なら鳴らしません。")) TObjectPtr<USoundBase> MultiCaptureSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Multi Capture", meta=(ClampMin="0.5",ClampMax="2", ToolTip="複数体捕獲のとき、転送完了の確定音（Capture Success Sound）を鳴らすピッチ倍率。")) float MultiCaptureSuccessPitch = 1.12f;
protected:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 void OnWorldTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
 // OnWorldTickの各段階（この順で実行します）。
 void UpdateSharedFeedback(float RealDelta);
 void UpdateTargets(float RealDelta, TArray<TWeakObjectPtr<ABoarBase>>& OutFinished);
 bool UpdateTarget(FCaptureVisualTarget& Target, float RealDelta);
 void CommitFinishedCaptures(ABoarGameMode* Mode, const TArray<TWeakObjectPtr<ABoarBase>>& Finished);
 void UpdateReceptions(float RealDelta, ABoarGameMode* Mode);
 // 共有状態（周囲Boarの局所減速・Camera FOV・Camera Shake）の取得と解放。解放は冪等で、
 // このComponentが最後に設定した値のまま残っている場合だけ元の値へ戻します（他の演出が上書きした値は戻しません）。
 void CollectLocalSlowTargets(const FVector& Center);
 void ApplyLocalSlow(bool bHitStop);
 void ReleaseLocalSlow();
 void RestoreSlowedBoar(FCaptureSlowedBoar& Slowed);
 void ApplyFieldOfView(float FOV);
 void ReleaseCamera();
 void ReleaseCameraShake();
 void RestoreSharedState();
 void ReportMissingVisualAssets() const;
 void RestoreTarget(FCaptureVisualTarget& Target);
 void StartDataConversion(FCaptureVisualTarget& Target);
 void StartCageReception(const FVector& Location);
 float GetTotalDuration() const;
 void PlaySound(USoundBase* Sound, float Pitch = 1.f);
 void SpawnImpact(const FCaptureVisualTarget& Target);
 /** ACFが通常カメラを出していればCamera Effectを再生してtrue。出ていなければfalse（従来のFOV Punch Inを使う）。 */
 bool PlayCameraEffect(class UACFCameraEffectPreset* Preset, const FVector& Source, bool bReplaceImmediately);
 void ReleaseCameraEffect(bool bImmediate);
 void PlayMultiCaptureFeedback(const FVector& Source);
 class UACFCameraSubsystem* GetACFSubsystem() const;
 UFUNCTION() void OnTargetDestroyed(AActor* Actor);
 UPROPERTY(Transient) TArray<FCaptureVisualTarget> Targets;
 UPROPERTY(Transient) TArray<FCaptureReception> Receptions;
 UPROPERTY(Transient) TWeakObjectPtr<UCameraComponent> Camera;
 UPROPERTY(Transient) TObjectPtr<UCameraShakeBase> ActiveShake;
 /** 現在の共有演出で減速対象にしたBoar。演出終了時に空にします。 */
 UPROPERTY(Transient) TArray<FCaptureSlowedBoar> SlowedBoars;
 FDelegateHandle TickHandle;
 double LastRealTime = 0.;
 float SharedAge = 0.f, OriginalFOV = 90.f;
 /** このComponentが最後に設定したFOV。解放時の所有確認に使います。 */
 float AppliedFOV = 90.f;
 bool bCommittingCaptures = false;
 bool bSharedActive = false, bOwnCamera = false, bConfirmed = false, bTransferSoundPlayed = false;
 /** 再生中のACF Camera Effect。共有演出の終了・キャンセル・Pause・EndPlayで止めます。 */
 FACFCameraEffectHandle ActiveCameraEffect;
 /** 共有演出のカメラをACF Effectで出しているか（trueならFollowCameraのFOVには触れない）。 */
 bool bUsingACFEffect = false;
 /** 現在のNet使用で捕獲した数と、複数体の強調を出したか。 */
 int32 SameUseCaptureCount = 0;
 bool bMultiFeedbackPlayed = false;
};
