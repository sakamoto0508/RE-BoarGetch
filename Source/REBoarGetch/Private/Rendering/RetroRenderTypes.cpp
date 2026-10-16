#include "Rendering/RetroRenderTypes.h"

TConstArrayView<FRetroParameterDescriptor> GetRetroParameterDescriptors()
{
 static const FRetroParameterDescriptor Values[] = {
  {TEXT("Pixelation"), &FRetroRenderParameters::Pixelation, 0, 1},
  {TEXT("TargetVerticalResolution"), &FRetroRenderParameters::TargetVerticalResolution, 120, 1080},
  {TEXT("ColorQuantization"), &FRetroRenderParameters::ColorQuantization, 0, 1},
  {TEXT("DitheringStrength"), &FRetroRenderParameters::DitheringStrength, 0, 1},
  {TEXT("Saturation"), &FRetroRenderParameters::Saturation, 0, 2},
  {TEXT("Contrast"), &FRetroRenderParameters::Contrast, .5f, 1.5f},
  {TEXT("Brightness"), &FRetroRenderParameters::Brightness, .5f, 1.5f},
  {TEXT("ScanlineStrength"), &FRetroRenderParameters::ScanlineStrength, 0, .5f},
  {TEXT("VignetteStrength"), &FRetroRenderParameters::VignetteStrength, 0, .5f},
  {TEXT("ChromaticAberration"), &FRetroRenderParameters::ChromaticAberration, 0, 3},
  {TEXT("CRTCurvature"), &FRetroRenderParameters::CRTCurvature, 0, .15f},
  {TEXT("SoftSampling"), &FRetroRenderParameters::SoftSampling, 0, 1},
  {TEXT("PhosphorMask"), &FRetroRenderParameters::PhosphorMask, 0, .3f}
 };
 return Values;
}
FString RetroPresetName(ERetroRenderPreset Preset)
{
 static const TCHAR* Names[] = {TEXT("OFF"),TEXT("PS1"),TEXT("N64"),TEXT("PS2"),TEXT("CRT")};
 return Names[FMath::Clamp(int32(Preset),0,4)];
}
URetroRenderPresetData::URetroRenderPresetData()
{
 Presets.SetNum(5);
 auto& PS1 = Presets[1];
 PS1.Pixelation=1; PS1.TargetVerticalResolution=240; PS1.ColorQuantization=.5f;
 PS1.DitheringStrength=.55f; PS1.Saturation=1.15f; PS1.Contrast=1.03f;
 auto& N64 = Presets[2];
 N64.Pixelation=1; N64.TargetVerticalResolution=240; N64.ColorQuantization=.25f;
 N64.SoftSampling=.7f; N64.Saturation=1.08f;
 auto& PS2 = Presets[3];
 PS2.Pixelation=.35f; PS2.TargetVerticalResolution=480; PS2.ColorQuantization=.1f;
 PS2.SoftSampling=.25f; PS2.Saturation=1.08f;
 auto& CRT = Presets[4];
 CRT.ScanlineStrength=.16f; CRT.VignetteStrength=.12f;
 CRT.ChromaticAberration=.65f; CRT.CRTCurvature=.025f; CRT.PhosphorMask=.035f;
 CRT.Saturation=1.08f;
}
