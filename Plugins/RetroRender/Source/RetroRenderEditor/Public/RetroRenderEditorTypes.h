#pragma once
#include "CoreMinimal.h"
#include "Rendering/RetroRenderTypes.h"
#include "RetroRenderEditorTypes.generated.h"

class UTexture2D;
class UStaticMesh;

// Values shown on the Look tab; mirrored into every live URetroRenderComponent (editor viewport + PIE).
UCLASS(Transient)
class URetroLookEditorState : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Look", meta=(ShowOnlyInnerProperties)) FRetroRenderParameters Parameters;
};

UENUM()
enum class ERetroTextureSize : uint8 { S64 UMETA(DisplayName="64"), S128 UMETA(DisplayName="128"), S256 UMETA(DisplayName="256"), S512 UMETA(DisplayName="512") };

UCLASS(Transient)
class URetroTextureOptions : public UObject
{
	GENERATED_BODY()
public:
	// PS1 VRAM made 256px the practical maximum.
	UPROPERTY(EditAnywhere, Category="Texture") ERetroTextureSize MaxTextureSize = ERetroTextureSize::S256;
	UPROPERTY(EditAnywhere, Category="Texture") bool bPointFiltering = true;
	UPROPERTY(EditAnywhere, Category="Texture") bool bDisableMipmaps = true;
	UPROPERTY(EditAnywhere, Category="Safety") bool bSkipNormalMaps = true;
	UPROPERTY(EditAnywhere, Category="Safety") bool bSkipUITextures = true;
	// Masks / roughness / HDR data (non-sRGB) are skipped unless unchecked.
	UPROPERTY(EditAnywhere, Category="Safety") bool bSkipNonColorTextures = false;
	UPROPERTY(EditAnywhere, Category="Safety") bool bSaveAfterApply = true;
	int32 GetMaxSize() const { return 64 << int32(MaxTextureSize); }
};

UENUM()
enum class ERetroDecimateMode : uint8 { Percent, TargetTriangles };

UCLASS(Transient)
class URetroDecimateOptions : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Reduction") ERetroDecimateMode Mode = ERetroDecimateMode::Percent;
	UPROPERTY(EditAnywhere, Category="Reduction", meta=(ClampMin=0.01, ClampMax=1, EditCondition="Mode==ERetroDecimateMode::Percent")) float PercentTriangles = 0.15f;
	UPROPERTY(EditAnywhere, Category="Reduction", meta=(ClampMin=4, EditCondition="Mode==ERetroDecimateMode::TargetTriangles")) int32 TargetTriangleCount = 800;
	// Meshes already at or below this are copied without reduction (keeps small props readable).
	UPROPERTY(EditAnywhere, Category="Reduction", meta=(ClampMin=0)) int32 MinimumTriangles = 64;
	UPROPERTY(EditAnywhere, Category="Normals") bool bRecalculateNormals = true;
	// Smaller = more faceted (hard edges). PS1-like flat shading around 20-35 degrees.
	UPROPERTY(EditAnywhere, Category="Normals", meta=(ClampMin=0, ClampMax=180)) float HardAngleThreshold = 30.f;
	UPROPERTY(EditAnywhere, Category="Reduction") bool bKeepSymmetry = false;
	UPROPERTY(EditAnywhere, Category="Output") bool bDisableNanite = true;
	// Complex (per-poly) collision keeps using the original mesh so gameplay traces do not change.
	UPROPERTY(EditAnywhere, Category="Output") bool bKeepOriginalComplexCollision = true;
	// Empty = next to the source mesh.
	UPROPERTY(EditAnywhere, Category="Output", meta=(ContentDir)) FDirectoryPath OutputFolder;
	UPROPERTY(EditAnywhere, Category="Output") FString Suffix = TEXT("_Retro");
};

namespace RetroTextureUtility
{
	struct FPlanItem
	{
		TWeakObjectPtr<UTexture2D> Texture;
		bool bSkip = false;
		FString Note;
	};
	TArray<FPlanItem> Plan(const TArray<UTexture2D*>& Textures, const URetroTextureOptions& Options, bool bRevert);
	FString Describe(const TArray<FPlanItem>& Items, bool bRevert);
	int32 Apply(const TArray<FPlanItem>& Items, const URetroTextureOptions& Options);
	int32 Revert(const TArray<FPlanItem>& Items, bool bSave);
}

namespace RetroMeshBaker
{
	struct FResult
	{
		TWeakObjectPtr<UStaticMesh> Source;
		TWeakObjectPtr<UStaticMesh> Baked;
		int32 SourceTriangles = 0;
		int32 BakedTriangles = 0;
		FString Note;
	};
	FResult Bake(UStaticMesh* Source, const URetroDecimateOptions& Options);
	FString Describe(const TArray<FResult>& Results);
	// Swaps meshes on selected level actors (original path stored in a component tag, undoable).
	FString ReplaceSelected(const URetroDecimateOptions& Options);
	FString RestoreSelected();
}
