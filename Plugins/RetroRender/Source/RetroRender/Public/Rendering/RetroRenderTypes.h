#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RetroRenderTypes.generated.h"

UENUM(BlueprintType)
enum class ERetroRenderPreset : uint8 { OFF, PS1, N64, PS2, CRT };

// Where a numeric parameter is consumed. Post = M_RetroPost scalar, Collection = MPC_Retro scalar (geometry).
enum class ERetroParameterTarget : uint8 { Post, Collection };

// All look controls of one preset. Float members are listed in GetRetroParameterDescriptors(),
// which is the single source of truth for material/MPC names, UI sliders and clamping.
USTRUCT(BlueprintType)
struct RETRORENDER_API FRetroRenderParameters
{
	GENERATED_BODY()

	// --- Screen (post process) ---
	// 0 = smooth, 1 = sample once per virtual pixel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen", meta=(ClampMin=0, ClampMax=1)) float Pixelation = 0.f;
	// 0 = square pixels: columns follow the viewport aspect (240 rows -> 427 cols on 16:9, 320 on 4:3).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen", meta=(ClampMin=0, ClampMax=1920)) float PixelResolutionX = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen", meta=(ClampMin=60, ClampMax=1080)) float PixelResolutionY = 240.f;
	// 1 = round rows so every virtual pixel covers the same whole number of screen pixels (1080p/240 -> 270 rows).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen", meta=(ClampMin=0, ClampMax=1)) float IntegerScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen", meta=(ClampMin=2, ClampMax=256)) float ColorLevels = 256.f;
	// Blend between the original and the quantised colour.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen", meta=(ClampMin=0, ClampMax=1)) float ColorQuantization = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen", meta=(ClampMin=0, ClampMax=1)) float DitheringStrength = 0.f;
	// 4 = Bayer 4x4 (PS1 hardware), 8 = Bayer 8x8.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen", meta=(ClampMin=4, ClampMax=8)) float DitherMatrixSize = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen") float Saturation = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen") float Contrast = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen") float Brightness = 1.f;
	// 0 = one sample per virtual pixel (crisp, aliased), 1 = average of 4 samples inside it (stable, no sparkle).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen", meta=(ClampMin=0, ClampMax=1)) float PixelFilter = 0.f;
	// N64 style 3-point filter between virtual pixels.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Screen", meta=(ClampMin=0, ClampMax=1)) float SoftSampling = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog", meta=(ClampMin=0, ClampMax=1)) float FogStrength = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog") float FogStart = 1500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog") float FogEnd = 9000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog") FLinearColor FogColor = FLinearColor(.72f, .84f, .95f);

	// --- CRT display layer (can be overlaid on any preset) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CRT", meta=(ClampMin=0, ClampMax=1)) float ScanlineStrength = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CRT", meta=(ClampMin=0, ClampMax=1)) float PhosphorMask = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CRT", meta=(ClampMin=0, ClampMax=0.2)) float CRTCurvature = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CRT", meta=(ClampMin=0, ClampMax=4)) float ChromaticAberration = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CRT", meta=(ClampMin=0, ClampMax=3)) float ColorBleed = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CRT", meta=(ClampMin=0, ClampMax=1)) float VignetteStrength = 0.f;

	// --- Geometry (Material Parameter Collection, read by patched surface materials) ---
	// Screen-space snapping of every vertex (PS1 integer vertex coordinates).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Geometry", meta=(ClampMin=0, ClampMax=1)) float VertexSnapStrength = 0.f;
	// 0 on X = follow the viewport aspect from SnapResolutionY.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Geometry", meta=(ClampMin=0, ClampMax=1920)) float SnapResolutionX = 160.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Geometry", meta=(ClampMin=16, ClampMax=1080)) float SnapResolutionY = 120.f;
	// Objects with a larger bounds radius (cm) fade out of snapping/affine. Huge 2-triangle ground or wall
	// boxes would otherwise tilt by tens of cm and expose coplanar geometry underneath. 0 = no limit.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Geometry", meta=(ClampMin=0, ClampMax=100000)) float SnapMaxObjectRadius = 1500.f;
	// Screen-linear (non perspective-correct) texture coordinates per triangle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Geometry", meta=(ClampMin=0, ClampMax=1)) float AffineAmount = 0.f;
	// Nearest-texel look on a virtual texel grid, independent of the source texture size.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Geometry", meta=(ClampMin=0, ClampMax=1)) float TexelSnapStrength = 0.f;
	// Virtual texels per UV unit (per tile for world-aligned materials).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Geometry", meta=(ClampMin=4, ClampMax=1024)) float TextureResolution = 64.f;
	// Positive = blurrier, lower mips (N64 low-res filtered textures).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Geometry", meta=(ClampMin=-2, ClampMax=6)) float TextureMipBias = 0.f;

	// --- Pipeline (runtime-only switches; every change is restored when the preset is turned off) ---
	// r.AntiAliasingMethod 0 while active. TSR/TAA otherwise smear the pixel grid and vertex snapping.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pipeline") bool bDisableTemporalAA = false;
	// Post-process override: reflections and ambient occlusion off for simple PS1-era shading
	// (Lumen GI stays on: without it shadowed faces lit only by the sky light become near-black).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pipeline") bool bSimplifyLighting = false;
	// Post-process override: bloom, motion blur, film grain, lens effects off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pipeline") bool bDisableModernPostFX = false;
	// Hides VolumetricCloud components in the world (restored on OFF).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pipeline") bool bHideVolumetricClouds = false;
	// Actually render at the virtual resolution (r.ScreenPercentage + nearest upscale). Game worlds only.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pipeline") bool bLowResolutionRender = false;
};

// One source of truth for parameter names, slider ranges and their consumer.
struct RETRORENDER_API FRetroParameterDescriptor
{
	const TCHAR* Name;
	float FRetroRenderParameters::* Member;
	float Min;
	float Max;
	ERetroParameterTarget Target;
	bool bCRTLayer;
};
RETRORENDER_API TConstArrayView<FRetroParameterDescriptor> GetRetroParameterDescriptors();
RETRORENDER_API FString RetroPresetName(ERetroRenderPreset Preset);
RETRORENDER_API bool RetroPresetFromName(const FString& Name, ERetroRenderPreset& OutPreset);
// Default values of each preset (estimated where RetroRender does not publish numbers).
RETRORENDER_API FRetroRenderParameters MakeDefaultRetroPreset(ERetroRenderPreset Preset);
// Copies the CRT display layer (scanlines, mask, curvature, bleed, vignette) from Source.
RETRORENDER_API void ApplyRetroCRTLayer(FRetroRenderParameters& Target, const FRetroRenderParameters& Source);

UCLASS(BlueprintType)
class RETRORENDER_API URetroRenderPresetData : public UDataAsset
{
	GENERATED_BODY()
public:
	URetroRenderPresetData();
	// Indexed by ERetroRenderPreset (OFF, PS1, N64, PS2, CRT).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retro", EditFixedSize) TArray<FRetroRenderParameters> Presets;
};
