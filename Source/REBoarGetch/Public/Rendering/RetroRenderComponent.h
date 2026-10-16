#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Rendering/RetroRenderTypes.h"
#include "RetroRenderComponent.generated.h"

class UMaterialInstanceDynamic;
class UPostProcessComponent;

UCLASS(ClassGroup=Rendering, meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API URetroRenderComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 URetroRenderComponent();
 UPROPERTY(EditAnywhere, Category="Retro") TSoftObjectPtr<UMaterialInterface> Material;
 UPROPERTY(EditAnywhere, Category="Retro") TSoftObjectPtr<URetroRenderPresetData> PresetData;
 UFUNCTION(BlueprintCallable) void SetPreset(ERetroRenderPreset NewPreset);
 UFUNCTION(BlueprintCallable) void SetParameter(FName Name, float Value);
 UFUNCTION(BlueprintCallable) void ResetCurrentPreset();
 UFUNCTION(BlueprintPure) ERetroRenderPreset GetPreset() const { return CurrentPreset; }
 UFUNCTION(BlueprintPure) FRetroRenderParameters GetParameters() const;
 UFUNCTION(BlueprintPure) UMaterialInstanceDynamic* GetDynamicMaterial() const { return MID; }
 UFUNCTION(BlueprintPure) bool IsReady() const { return MID != nullptr; }
protected:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 void Apply();
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MID;
 UPROPERTY(Transient) TObjectPtr<UPostProcessComponent> PostProcess;
 UPROPERTY(Transient) TArray<FRetroRenderParameters> Defaults;
 UPROPERTY(Transient) TArray<FRetroRenderParameters> Working;
 UPROPERTY(Transient) ERetroRenderPreset CurrentPreset = ERetroRenderPreset::OFF;
};
