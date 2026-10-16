#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RetroRenderTypes.generated.h"

UENUM(BlueprintType)
enum class ERetroRenderPreset : uint8 { OFF, PS1, N64, PS2, CRT };

// Display-space controls, independent of preset selection and material ownership.
USTRUCT(BlueprintType)
struct REBOARGETCH_API FRetroRenderParameters
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float Pixelation = 0.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float TargetVerticalResolution = 240.f;
 // 0 bypasses quantization; 1 gives 4 levels/channel, 0.5 gives 32.
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float ColorQuantization = 0.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float DitheringStrength = 0.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float Saturation = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float Contrast = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float Brightness = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float ScanlineStrength = 0.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float VignetteStrength = 0.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float ChromaticAberration = 0.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float CRTCurvature = 0.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float SoftSampling = 0.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float PhosphorMask = 0.f;
};

// One source of truth for material parameter names and debug slider ranges.
struct REBOARGETCH_API FRetroParameterDescriptor
{
 const TCHAR* Name;
 float FRetroRenderParameters::* Member;
 float Min;
 float Max;
};
REBOARGETCH_API TConstArrayView<FRetroParameterDescriptor> GetRetroParameterDescriptors();
REBOARGETCH_API FString RetroPresetName(ERetroRenderPreset Preset);

UCLASS(BlueprintType)
class REBOARGETCH_API URetroRenderPresetData : public UDataAsset
{
 GENERATED_BODY()
public:
 URetroRenderPresetData();
 // Enum ordinal indexes these five entries. Runtime edits are kept separately.
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FRetroRenderParameters> Presets;
};
