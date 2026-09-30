#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Camera/CameraShakeBase.h"
#include "CapturePresentationComponent.generated.h"

class ABoarBase;
class UCameraComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;

UCLASS()
class REBOARGETCH_API UCaptureSuccessCameraShake : public UCameraShakeBase
{
 GENERATED_BODY()
public:
 UCaptureSuccessCameraShake(const FObjectInitializer& ObjectInitializer);
};

USTRUCT()
struct FCaptureFragment
{
 GENERATED_BODY()
 UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
 FVector Start = FVector::ZeroVector;
 FVector Drift = FVector::ZeroVector;
 float Delay = 0.f;
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
 UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FragmentMaterial;
 UPROPERTY() TArray<FCaptureFragment> Fragments;
 UPROPERTY() TObjectPtr<UNiagaraComponent> VFX;
 bool bOriginalVisible = true;
 bool bDataStarted = false;
 FVector CaptureLocation = FVector::ZeroVector;
 float CaptureMinZ = 0.f;
 float CaptureHeight = 150.f;
 float Age = 0.f;
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
 GENERATED_BODY()
public:
 UCapturePresentationComponent();
 bool PresentCapturedBoar(ABoarBase* Boar, bool bSharedFeedback);
 void CancelPresentation();
 bool HasPendingCaptures() const { return bCommittingCaptures || !Targets.IsEmpty() || !Receptions.IsEmpty(); }
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation", meta=(ToolTip="捕獲成功演出を有効にします。OFFでも捕獲判定とCageへの収容は通常どおり行われます。")) bool bEnabled = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0",ClampMax="0.12", ToolTip="網が当たった直後に時間を止める長さ（実時間・秒）。推奨0.08～0.12秒。Net 1回につき1回だけ適用します。")) float HitStopDuration = .1f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.001",ClampMax="1", ToolTip="Hit Stop中の時間倍率。0に近いほど強く停止します。初期値0.01。")) float HitStopTimeScale = .01f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.01",ClampMax="1", ToolTip="スローモーション中の時間倍率。小さいほど周囲の動きが遅くなります。初期値0.3。")) float SlowTimeScale = .3f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0",ClampMax="0.6", ToolTip="Hit Stop後のスローモーションの長さ（実時間・秒）。足元リングとシアン発光を見せる時間です。")) float SlowDuration = .27f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.05",ClampMax="0.7", ToolTip="Boarが下から上へデータ化される長さ（実時間・秒）。長くするとScanと分解をゆっくり見せられます。")) float CaptureDissolveDuration = .3f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.05",ClampMax="0.4", ToolTip="データ化後、Boarの残像と破片が上昇して消えるまでの長さ（実時間・秒）。")) float TransferFinishDuration = .18f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.05",ClampMax="0.5", ToolTip="Cageへ収容した直後のシアン受信エフェクトの長さ（実時間・秒）。捕獲地点の演出時間には含まれません。")) float CageReceptionDuration = .18f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera", meta=(ClampMin="0",ClampMax="20", ToolTip="捕獲時に一時的に狭める視野角（度）。大きいほどPunch Inが強くなります。位置やCamera Actorは切り替えません。")) float CameraZoomAmount = 12.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera", meta=(ClampMin="0.01",ClampMax="1", ToolTip="Punch Inして元の視野角へ戻るまでの時間（実時間・秒）。")) float CameraDuration = .38f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ToolTip="転送中だけ表示するBoar用Material。下から上へのDissolve、シアンのScan境界、データ線を描きます。元のBoar Materialは変更しません。")) TObjectPtr<UMaterialInterface> DataMaterial;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ToolTip="転送前のシアン発光と、上昇する小さなデータ片に使うMaterial。未設定だと発光と破片が表示されません。")) TObjectPtr<UMaterialInterface> GlowMaterial;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ToolTip="捕獲地点のBoar足元に出すシアンリングのNiagara System。拡大してから薄くなります。")) TObjectPtr<UNiagaraSystem> CaptureVFX;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ToolTip="Cageへの収容直後に出す受信エフェクト。未設定の場合はCapture VFXを再利用します。")) TObjectPtr<UNiagaraSystem> CageReceptionVFX;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ClampMin="0.01", ToolTip="捕獲リングとCage受信エフェクトの基準Scale。大きくすると両方のリングが広がります。")) float VFXScale = .55f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ClampMin="0",ClampMax="32", ToolTip="Boarから上昇するデータ片の数。四角い破片と細い縦方向の光片を含みます。0で破片を非表示にできます。")) int32 FragmentCount = 16;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ClampMin="0",ClampMax="250", ToolTip="転送完了時にBoarのデータ残像と破片が上へ移動する距離（cm）。")) float TransferRiseDistance = 105.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Audio", meta=(ToolTip="網が命中した瞬間の効果音。Net 1回につき1回、既存AudioManager経由で再生します。未設定なら無音です。")) TObjectPtr<USoundBase> ImpactSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Audio", meta=(ToolTip="Boarのデータ化が始まる瞬間の転送音。既存AudioManager経由で再生します。未設定なら無音です。")) TObjectPtr<USoundBase> TransferStartSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Audio", meta=(ToolTip="捕獲地点での転送が完了した瞬間の確定音。Net 1回につき1回、既存AudioManager経由で再生します。未設定なら無音です。")) TObjectPtr<USoundBase> CaptureSuccessSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera", meta=(ToolTip="網の命中時に再生する小さなCamera Shake。Net 1回につき1回だけ再生します。未設定なら揺れません。")) TSubclassOf<UCameraShakeBase> CameraShake;
protected:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 void OnWorldTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
 void RestoreSharedState();
 void RestoreTarget(FCaptureVisualTarget& Target);
 void StartDataConversion(FCaptureVisualTarget& Target);
 void StartCageReception(const FVector& Location);
 float GetTotalDuration() const;
 void PlaySound(USoundBase* Sound);
 UFUNCTION() void OnTargetDestroyed(AActor* Actor);
 UPROPERTY(Transient) TArray<FCaptureVisualTarget> Targets;
 UPROPERTY(Transient) TArray<FCaptureReception> Receptions;
 UPROPERTY(Transient) TWeakObjectPtr<UCameraComponent> Camera;
 UPROPERTY(Transient) TObjectPtr<UCameraShakeBase> ActiveShake;
 FDelegateHandle TickHandle;
 double LastRealTime = 0.;
 float SharedAge = 0.f, OriginalTimeScale = 1.f, OriginalFOV = 90.f;
 bool bCommittingCaptures = false;
 bool bSharedActive = false, bOwnTimeScale = false, bOwnCamera = false, bConfirmed = false, bTransferSoundPlayed = false;
};
