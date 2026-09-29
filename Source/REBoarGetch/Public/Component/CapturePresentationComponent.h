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

/** Presentation owns temporary state; CaptureComponent/GameMode retain gameplay authority. */
UCLASS(ClassGroup=(Presentation), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UCapturePresentationComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UCapturePresentationComponent();
 bool PresentCapturedBoar(ABoarBase* Boar, bool bSharedFeedback);
 void CancelPresentation();
 bool HasPendingCaptures() const { return bCommittingCaptures || !Targets.IsEmpty() || !Receptions.IsEmpty(); }
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation") bool bEnabled = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0",ClampMax="0.12")) float HitStopDuration = .1f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation", meta=(ClampMin="0.001",ClampMax="1")) float HitStopTimeScale = .01f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation", meta=(ClampMin="0.01",ClampMax="1")) float SlowTimeScale = .3f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0",ClampMax="0.6")) float SlowDuration = .27f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.05",ClampMax="0.7")) float CaptureDissolveDuration = .3f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.05",ClampMax="0.4")) float TransferFinishDuration = .18f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.05",ClampMax="0.5")) float CageReceptionDuration = .18f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera", meta=(ClampMin="0",ClampMax="20")) float CameraZoomAmount = 12.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera", meta=(ClampMin="0.01",ClampMax="1")) float CameraDuration = .38f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX") TObjectPtr<UMaterialInterface> DataMaterial;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation") TObjectPtr<UMaterialInterface> GlowMaterial;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation") TObjectPtr<UNiagaraSystem> CaptureVFX;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX") TObjectPtr<UNiagaraSystem> CageReceptionVFX;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation", meta=(ClampMin="0.01")) float VFXScale = .55f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ClampMin="0",ClampMax="32")) int32 FragmentCount = 16;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ClampMin="0",ClampMax="250")) float TransferRiseDistance = 105.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation") TObjectPtr<USoundBase> ImpactSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Audio") TObjectPtr<USoundBase> TransferStartSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation") TObjectPtr<USoundBase> CaptureSuccessSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation") TSubclassOf<UCameraShakeBase> CameraShake;
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
