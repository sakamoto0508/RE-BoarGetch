#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CageVisualComponent.generated.h"
class ACage;
class UStaticMesh;
class UStaticMeshComponent;
class UWidgetComponent;
class UMaterialInstanceDynamic;
UCLASS(ClassGroup=(Visual),meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UCageVisualComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UCageVisualComponent();
	/** 檻の見た目に使う専用Meshです。設定時はこれを表示し、未設定時だけ仮の箱組みを表示します。Collisionは持たせません。 */
	UPROPERTY(EditAnywhere,Category="Visual") TObjectPtr<UStaticMesh> CageMesh;
	/** CageMeshの位置・回転・Scale調整です。Gameplay用のCollisionや収容範囲は動かしません。 */
	UPROPERTY(EditAnywhere,Category="Visual") FTransform CageMeshTransform;
	/** HPに応じて色を変えるランプのMaterial Slot番号です（CageMesh使用時）。Materialには"Tint"と"Intensity"が必要です。 */
	UPROPERTY(EditAnywhere,Category="Visual") TArray<int32> LampMaterialSlots;
	UPROPERTY(EditAnywhere,Category="Gauge",meta=(ClampMin="0")) float DisplayDistance=12000.f;
	UPROPERTY(EditAnywhere,Category="Gauge") float GaugeHeight=650.f;
	UPROPERTY(EditAnywhere,Category="Gauge") float GaugeWorldScale=.9f;
	UPROPERTY(EditAnywhere,Category="Gauge",meta=(ClampMin="0",ClampMax="1")) float MediumThreshold=.6f;
	UPROPERTY(EditAnywhere,Category="Gauge",meta=(ClampMin="0",ClampMax="1")) float LowThreshold=.25f;
protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
private:
	UPROPERTY() TObjectPtr<UWidgetComponent> Gauge;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CageMeshComponent;
	/** CageMesh未設定時の仮表示です。完成版の見た目には使いません。 */
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Shell;
	void ApplyVisualSource();
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Lamps;
	UPROPERTY(Transient) TObjectPtr<ACage> Cage;
	UFUNCTION() void HealthChanged(float Hp,float MaxHp);
	UFUNCTION() void Destroyed();
	UFUNCTION() void Respawned();
	float Fraction=1, Trail=1, FlashRemaining=0;
	bool bDestroyed=false;
	bool bRedraw=true;
};
