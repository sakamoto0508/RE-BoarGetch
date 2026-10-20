#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Rendering/RetroRenderTypes.h"
#include "RetroRenderComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UPostProcessComponent;
class USceneComponent;

/**
 * Applies one retro preset to its world:
 *  - one unbound post-process blendable (M_RetroPost MID) for pixelation / colour / dither / fog / CRT,
 *  - MPC_Retro scalars read by patched surface materials (vertex snap, affine UV, texel snap),
 *  - optional pipeline switches (AA, Lumen, modern post FX, clouds, low-res render) that are restored on OFF/unregister.
 * Works in the editor viewport (preview) and in PIE/game. Existing volumes, cameras and maps are not modified.
 */
UCLASS(ClassGroup=Rendering, meta=(BlueprintSpawnableComponent))
class RETRORENDER_API URetroRenderComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URetroRenderComponent();
	UPROPERTY(EditAnywhere, Category="Retro") TSoftObjectPtr<UMaterialInterface> Material;
	UPROPERTY(EditAnywhere, Category="Retro") TSoftObjectPtr<URetroRenderPresetData> PresetData;
	UPROPERTY(EditAnywhere, Category="Retro") TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection;
	// Other collections that also carry geometry scalars (for materials already at UE's 2-collection limit).
	// Only parameters that exist in a collection are written.
	UPROPERTY(EditAnywhere, Category="Retro") TArray<TSoftObjectPtr<UMaterialParameterCollection>> SharedCollections;
	// Preset used when the world starts (and shown in the editor viewport).
	UPROPERTY(EditAnywhere, Category="Retro") ERetroRenderPreset InitialPreset = ERetroRenderPreset::OFF;
	// Adds the CRT preset's display layer (scanlines, mask, curvature, bleed) on top of any preset.
	UPROPERTY(EditAnywhere, Category="Retro") bool bCRTOverlay = false;
	UPROPERTY(EditAnywhere, Category="Retro") bool bPreviewInEditor = true;

	UFUNCTION(BlueprintCallable, Category="Retro") void SetPreset(ERetroRenderPreset NewPreset);
	UFUNCTION(BlueprintCallable, Category="Retro") void SetCRTOverlay(bool bEnable);
	UFUNCTION(BlueprintCallable, Category="Retro") void SetParameter(FName Name, float Value);
	// Replaces the working values of the current preset (editor tool, Custom tuning).
	UFUNCTION(BlueprintCallable, Category="Retro") void SetPresetParameters(const FRetroRenderParameters& Values);
	UFUNCTION(BlueprintCallable, Category="Retro") void ResetCurrentPreset();
	UFUNCTION(BlueprintPure, Category="Retro") ERetroRenderPreset GetPreset() const { return CurrentPreset; }
	UFUNCTION(BlueprintPure, Category="Retro") bool IsCRTOverlay() const { return bCRTOverlay; }
	// Working values of the current preset (without CRT overlay).
	UFUNCTION(BlueprintPure, Category="Retro") FRetroRenderParameters GetParameters() const;
	// Values actually sent to the GPU (CRT overlay applied, zeros when OFF).
	UFUNCTION(BlueprintPure, Category="Retro") FRetroRenderParameters GetEffectiveParameters() const;
	UFUNCTION(BlueprintPure, Category="Retro") UMaterialInstanceDynamic* GetDynamicMaterial() const { return MID; }
	UFUNCTION(BlueprintPure, Category="Retro") bool IsReady() const { return MID != nullptr; }
	UFUNCTION(BlueprintPure, Category="Retro") bool IsRetroActive() const { return CurrentPreset != ERetroRenderPreset::OFF || bCRTOverlay; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
private:
	void Setup();
	void Teardown();
	void Apply();
	void ApplyPipeline(const FRetroRenderParameters& P, bool bActive);
	void RestorePipeline();
	// nullptr writes each collection's default values (= no effect).
	void WriteCollections(const FRetroRenderParameters* Values);
	bool IsGameWorld() const;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MID;
	UPROPERTY(Transient) TObjectPtr<UPostProcessComponent> PostProcess;
	UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> Collection;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialParameterCollection>> Collections;
	UPROPERTY(Transient) TArray<FRetroRenderParameters> Defaults;
	UPROPERTY(Transient) TArray<FRetroRenderParameters> Working;
	UPROPERTY(Transient) ERetroRenderPreset CurrentPreset = ERetroRenderPreset::OFF;
	TArray<TWeakObjectPtr<USceneComponent>> HiddenClouds;
	TMap<FString, FString> SavedCVars;
};
