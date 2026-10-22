#pragma once
#include "CoreMinimal.h"
#include "Presentation/BoarPresentationDirector.h"
#include "StageClearPresentationDirector.generated.h"

class ACameraActor;
class UAnimMontage;
class UAnimSequenceBase;
class UCurveFloat;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UPrimitiveComponent;
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

/**
 * Stage Clear勝利演出の部品群です。順序とタイミング（Montage開始・星・発光・分解・後退・暗転）、SE、BGMは
 * 派生BP（BP_StageClearPresentation）のEvent Graphで組み立てます。
 * このクラスは毎フレーム処理が必要な下位処理（勝利カメラの選定と後退、Montageの最終ポーズ保持、データ化Proxy、粒子）と、
 * Level遷移・中断時の見た目の復元だけを担当します。Player/網のActorはDestroyせず、Mesh表示とOverlayだけを一時変更します。
 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API AStageClearPresentationDirector : public ABoarPresentationDirector
{
	GENERATED_BODY()
public:
	/** HUDを隠し、注視点を固定して遮蔽の少ない斜め前方へ勝利カメラを置き、通常カメラからBlendします。 */
	UFUNCTION(BlueprintCallable, Category = "Stage Clear Presentation") void BeginVictoryShot();
	/** Victory Montageを再生し、最終ポーズで保持します（Slot非評価ならSingle Node、Montage不可ならMeshを上下させるFallback）。 */
	UFUNCTION(BlueprintCallable, Category = "Stage Clear Presentation") void PlayVictoryMontage();
	/** 網の先端から星VFXを色ごとに放ちます。 */
	UFUNCTION(BlueprintCallable, Category = "Stage Clear Presentation") void SpawnStarBurst();
	/** 身体と網へシアンOverlayを掛け、RampDuration秒で最大まで強めます（分解開始まで）。 */
	UFUNCTION(BlueprintCallable, Category = "Stage Clear Presentation") void BeginGlow(float RampDuration = .25f);
	/** 同じ姿勢のProxyへデータ化Materialを掛けて下→上へ分解し、キューブ粒子を上昇させます。Duration秒で完全に消えます。 */
	UFUNCTION(BlueprintCallable, Category = "Stage Clear Presentation") void BeginDissolve(float Duration = .75f);
	/** 注視点を見たまま後退・上昇・FOV変更します。終盤ParticleFadeDuration秒で星と粒子を消します。 */
	UFUNCTION(BlueprintCallable, Category = "Stage Clear Presentation") void BeginCameraRetreat(float Duration = 2.1f, float ParticleFadeDuration = .7f);
	/** 残った星・粒子を片付け、画面を黒へフェードして保持します。 */
	UFUNCTION(BlueprintCallable, Category = "Stage Clear Presentation") void BeginScreenFadeOut(float Duration = .6f);

	/** Result表示時に呼ばれます（Result BGMなど）。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Stage Clear Presentation") void OnResultShown();

	/** 消失地点として固定したワールド座標です。カメラ後退中も常にこの点を注視します。 */
	UFUNCTION(BlueprintPure, Category = "Stage Clear Presentation") FVector GetVictoryFocusPoint() const { return VictoryFocusPoint; }
	UFUNCTION(BlueprintPure, Category = "Stage Clear Presentation") bool IsRetreating() const { return bRetreating; }

	// ---- Camera ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Camera", meta = (ToolTip = "注視点をプレイヤー足元から見た位置（cm、ワールドZ）。")) FVector VictoryFocusOffset = FVector(0, 0, 125);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Camera", meta = (ClampMin = "50")) float CameraDistance = 450.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Camera") float CameraHeight = 15.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Camera", meta = (ClampMin = "-90", ClampMax = "90", ToolTip = "プレイヤー正面から横へずらす角度（度）。")) float CameraYawOffset = 28.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Camera", meta = (ClampMin = "0")) float CameraBlendTime = .5f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Camera", meta = (ClampMin = "20", ClampMax = "120")) float VictoryFOV = 55.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Camera", meta = (ClampMin = "20", ClampMax = "120")) float RetreatEndFOV = 58.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Camera", meta = (ClampMin = "0")) float RetreatDistance = 400.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Camera") float RetreatHeight = 100.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Camera", meta = (ToolTip = "後退の進行カーブ（0～1）。未設定はEase In/Outです。")) TObjectPtr<UCurveFloat> RetreatCurve;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Camera", meta = (ClampMin = "1")) float RetreatEaseExponent = 2.2f;

	// ---- Animation ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Animation") TObjectPtr<UAnimMontage> VictoryMontage;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Animation", meta = (ClampMin = "0.1")) float MontagePlayRate = 1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Animation", meta = (ToolTip = "Slot非評価時のSingle Node再生用。未設定ならMontageの最初のSegment。")) TObjectPtr<UAnimSequenceBase> VictoryAnimation;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Animation") bool bAllowSingleNodeFallback = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Animation", meta = (ClampMin = "0")) float FallbackJumpHeight = 30.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|Animation", meta = (ClampMin = "0", ToolTip = "Fallbackの小ジャンプをMontage開始から何秒後に踏み切るか。")) float FallbackJumpDelay = .2f;

	// ---- VFX ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|VFX") TObjectPtr<UNiagaraSystem> StarVFX;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|VFX") TArray<FLinearColor> StarColors;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|VFX") FVector StarOffset = FVector(0, 0, 10);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|VFX", meta = (ClampMin = "0.01")) float StarScale = 1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|VFX") TObjectPtr<UMaterialInterface> DataMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|VFX") TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|VFX") TObjectPtr<UNiagaraSystem> TransferVFX;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|VFX") FLinearColor DataColor = FLinearColor(.02f, .8f, 1.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|VFX", meta = (ClampMin = "0", ClampMax = "64")) int32 FragmentCount = 28;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Clear Presentation|VFX", meta = (ClampMin = "0")) float FragmentRiseSpeed = 120.f;

	AStageClearPresentationDirector();
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickPresentation(float Dt) override;
private:
	void CollectMeshes();
	void FinishDissolve();
	FVector ComputeCameraLocation(float RetreatAlpha) const;
	float EvaluateRetreat(float T) const;
	void UpdateCamera(float RetreatAlpha);
	void UpdateMontage();
	void DestroyParticles();
	void RestoreVisuals();
	void RestoreCamera();

	bool bVictoryShot = false, bMontagePlaying = false, bMontageHeld = false, bFallbackJump = false, bSingleNodePlayback = false;
	bool bGlowing = false, bDissolving = false, bDissolveFinished = false, bRetreating = false;
	int32 MontageCheckFrames = 0;
	float MontageAge = 0.f, GlowAge = 0.f, GlowRamp = .25f, DissolveAge = 0.f, DissolveDuration = .75f;
	float RetreatAge = 0.f, RetreatDuration = 2.1f, ParticleFadeDuration = .7f;
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
