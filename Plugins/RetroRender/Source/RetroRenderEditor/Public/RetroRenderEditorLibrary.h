#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RetroRenderEditorLibrary.generated.h"

class UTexture2D;
class UStaticMesh;
class UMaterial;
class UMaterialFunction;
class UMaterialExpression;

// Script/Blueprint access to the Retro Render editor tools (same code paths as the Retro Render panel).
UCLASS()
class URetroRenderEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// MaxSize: 64/128/256/512. Returns the per-texture plan/report. bApply=false only previews.
	UFUNCTION(BlueprintCallable, Category="Retro Render|Textures")
	static FString RetroTextures(const TArray<UTexture2D*>& Textures, int32 MaxSize = 256, bool bPointFiltering = true, bool bDisableMipmaps = true,
		bool bSkipNormalMaps = true, bool bApply = false, bool bSave = true);
	UFUNCTION(BlueprintCallable, Category="Retro Render|Textures")
	static FString RevertRetroTextures(const TArray<UTexture2D*>& Textures, bool bApply = false, bool bSave = true);
	// Unlinks and removes expressions from a material or material function (all of them when Expressions is empty).
	// Unlike MaterialEditingLibrary.DeleteMaterialExpression it does not force-mark the node as garbage, which asserts
	// for nodes that are referenced from the root set in UE 5.8; detached nodes are moved to the transient package.
	UFUNCTION(BlueprintCallable, Category="Retro Render|Materials")
	static int32 RemoveRetroExpressions(UObject* MaterialOrFunction, const TArray<UMaterialExpression*>& Expressions);
	// Pending shader jobs (0 = every material on screen uses its real shaders, not the default fallback).
	UFUNCTION(BlueprintCallable, Category="Retro Render|Verification")
	static int32 GetRemainingShaderJobs();
	// Blocks until all queued shader compilation is finished.
	UFUNCTION(BlueprintCallable, Category="Retro Render|Verification")
	static void FinishShaderCompilation();
	// Builds the Retro Render panel off-screen, optionally selects a preset (drives live components like the
	// button does) and fills the texture preview, then saves the drawn tab as PNG. Verification only.
	UFUNCTION(BlueprintCallable, Category="Retro Render|Verification")
	static bool RenderRetroPanelToPng(const FString& Path, int32 Tab, int32 SelectPreset, const TArray<UTexture2D*>& PreviewTextures, int32 Width = 900, int32 Height = 1100);
	// TargetTriangles > 0 switches to target-count mode. Returns the baked meshes (index-aligned with Meshes, null on failure).
	UFUNCTION(BlueprintCallable, Category="Retro Render|Meshes")
	static TArray<UStaticMesh*> BakeRetroMeshes(const TArray<UStaticMesh*>& Meshes, FString& Report, float PercentTriangles = 0.15f,
		int32 TargetTriangles = 0, int32 MinimumTriangles = 64, float HardAngleThreshold = 30.f, bool bRecalculateNormals = true,
		const FString& OutputFolder = TEXT(""));
};
