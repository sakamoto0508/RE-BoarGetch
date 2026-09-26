#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CageVisualComponent.generated.h"
class ACage;
class UStaticMeshComponent;
class UWidgetComponent;
class UMaterialInstanceDynamic;
UCLASS(ClassGroup=(Visual),meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UCageVisualComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UCageVisualComponent();
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
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Shell;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Lamps;
	UPROPERTY(Transient) TObjectPtr<ACage> Cage;
	UFUNCTION() void HealthChanged(float Hp,float MaxHp);
	UFUNCTION() void Destroyed();
	UFUNCTION() void Respawned();
	float Fraction=1, Trail=1, FlashRemaining=0;
	bool bDestroyed=false;
	bool bRedraw=true;
};
