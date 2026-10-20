#include "Rendering/RetroRenderTypes.h"

TConstArrayView<FRetroParameterDescriptor> GetRetroParameterDescriptors()
{
	using P = FRetroRenderParameters;
	constexpr ERetroParameterTarget Post = ERetroParameterTarget::Post;
	constexpr ERetroParameterTarget MPC = ERetroParameterTarget::Collection;
	static const FRetroParameterDescriptor Values[] = {
		{TEXT("Pixelation"), &P::Pixelation, 0, 1, Post, false},
		{TEXT("PixelResolutionX"), &P::PixelResolutionX, 0, 1920, Post, false},
		{TEXT("PixelResolutionY"), &P::PixelResolutionY, 60, 1080, Post, false},
		{TEXT("IntegerScale"), &P::IntegerScale, 0, 1, Post, false},
		{TEXT("ColorLevels"), &P::ColorLevels, 2, 256, Post, false},
		{TEXT("ColorQuantization"), &P::ColorQuantization, 0, 1, Post, false},
		{TEXT("DitheringStrength"), &P::DitheringStrength, 0, 1, Post, false},
		{TEXT("DitherMatrixSize"), &P::DitherMatrixSize, 4, 8, Post, false},
		{TEXT("Saturation"), &P::Saturation, 0, 2, Post, false},
		{TEXT("Contrast"), &P::Contrast, .5f, 1.5f, Post, false},
		{TEXT("Brightness"), &P::Brightness, .5f, 1.5f, Post, false},
		{TEXT("SoftSampling"), &P::SoftSampling, 0, 1, Post, false},
		{TEXT("FogStrength"), &P::FogStrength, 0, 1, Post, false},
		{TEXT("FogStart"), &P::FogStart, 0, 50000, Post, false},
		{TEXT("FogEnd"), &P::FogEnd, 100, 100000, Post, false},
		{TEXT("ScanlineStrength"), &P::ScanlineStrength, 0, 1, Post, true},
		{TEXT("PhosphorMask"), &P::PhosphorMask, 0, 1, Post, true},
		{TEXT("CRTCurvature"), &P::CRTCurvature, 0, .2f, Post, true},
		{TEXT("ChromaticAberration"), &P::ChromaticAberration, 0, 4, Post, true},
		{TEXT("ColorBleed"), &P::ColorBleed, 0, 3, Post, true},
		{TEXT("VignetteStrength"), &P::VignetteStrength, 0, 1, Post, true},
		{TEXT("VertexSnapStrength"), &P::VertexSnapStrength, 0, 1, MPC, false},
		{TEXT("SnapResolutionX"), &P::SnapResolutionX, 0, 1920, MPC, false},
		{TEXT("SnapResolutionY"), &P::SnapResolutionY, 16, 1080, MPC, false},
		{TEXT("SnapMaxObjectRadius"), &P::SnapMaxObjectRadius, 0, 100000, MPC, false},
		{TEXT("AffineAmount"), &P::AffineAmount, 0, 1, MPC, false},
		{TEXT("TexelSnapStrength"), &P::TexelSnapStrength, 0, 1, MPC, false},
		{TEXT("TextureResolution"), &P::TextureResolution, 4, 1024, MPC, false},
		{TEXT("TextureMipBias"), &P::TextureMipBias, -2, 6, MPC, false},
	};
	return Values;
}

FString RetroPresetName(ERetroRenderPreset Preset)
{
	static const TCHAR* Names[] = {TEXT("OFF"), TEXT("PS1"), TEXT("N64"), TEXT("PS2"), TEXT("CRT")};
	return Names[FMath::Clamp(int32(Preset), 0, 4)];
}

bool RetroPresetFromName(const FString& Name, ERetroRenderPreset& OutPreset)
{
	for (uint8 i = 0; i < 5; ++i)
	{
		if (Name.Equals(RetroPresetName(ERetroRenderPreset(i)), ESearchCase::IgnoreCase))
		{
			OutPreset = ERetroRenderPreset(i);
			return true;
		}
	}
	return false;
}

void ApplyRetroCRTLayer(FRetroRenderParameters& Target, const FRetroRenderParameters& Source)
{
	for (const FRetroParameterDescriptor& D : GetRetroParameterDescriptors())
	{
		if (D.bCRTLayer) Target.*(D.Member) = Source.*(D.Member);
	}
}

// Resolution / colour levels / on-off columns follow the public RetroRender preset table.
// Strengths, fog, colour grading and texture values are estimates tuned on RE-BoarGetch Stage1.
FRetroRenderParameters MakeDefaultRetroPreset(ERetroRenderPreset Preset)
{
	FRetroRenderParameters P;
	switch (Preset)
	{
	case ERetroRenderPreset::PS1:
		P.Pixelation = 1; P.PixelResolutionY = 240; P.ColorLevels = 32; P.ColorQuantization = 1;
		P.DitheringStrength = .85f; P.DitherMatrixSize = 4; P.Saturation = 1.2f; P.Contrast = 1.06f; P.Brightness = 1.04f;
		P.VertexSnapStrength = 1; P.SnapResolutionX = 160; P.SnapResolutionY = 120;
		P.AffineAmount = 1; P.TexelSnapStrength = 1; P.TextureResolution = 64;
		P.bDisableTemporalAA = true; P.bSimplifyLighting = true; P.bDisableModernPostFX = true; P.bHideVolumetricClouds = true;
		break;
	case ERetroRenderPreset::N64:
		P.Pixelation = 1; P.PixelResolutionY = 240; P.ColorLevels = 32; P.ColorQuantization = 1;
		P.DitheringStrength = .3f; P.Saturation = 1.1f; P.SoftSampling = 1;
		P.FogStrength = .55f; P.FogStart = 1500; P.FogEnd = 9000;
		P.TextureMipBias = 1.5f;
		P.bDisableTemporalAA = true; P.bSimplifyLighting = true; P.bDisableModernPostFX = true; P.bHideVolumetricClouds = true;
		break;
	case ERetroRenderPreset::PS2:
		P.Pixelation = 1; P.PixelResolutionY = 448; P.ColorLevels = 256; P.ColorQuantization = 1;
		P.DitheringStrength = .5f; P.Saturation = 1.05f; P.SoftSampling = .25f;
		P.bDisableModernPostFX = true;
		break;
	case ERetroRenderPreset::CRT:
		P.Pixelation = 1; P.PixelResolutionY = 200; P.ColorLevels = 256; P.ColorQuantization = 1;
		P.DitheringStrength = 1; P.Saturation = 1.08f;
		P.ScanlineStrength = .35f; P.PhosphorMask = .08f; P.CRTCurvature = .03f;
		P.ChromaticAberration = .6f; P.ColorBleed = 1; P.VignetteStrength = .2f;
		P.bDisableTemporalAA = true;
		break;
	default:
		break;
	}
	return P;
}

URetroRenderPresetData::URetroRenderPresetData()
{
	Presets.SetNum(5);
	for (int32 i = 0; i < 5; ++i) Presets[i] = MakeDefaultRetroPreset(ERetroRenderPreset(i));
}
