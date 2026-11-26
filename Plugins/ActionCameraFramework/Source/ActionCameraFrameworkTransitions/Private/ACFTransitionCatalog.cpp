#include "ACFTransitionCatalog.h"

#include "Curves/CurveFloat.h"

DEFINE_LOG_CATEGORY(LogACFTransition);

namespace ACFTransition
{
	namespace
	{
		using C = EACFTransitionCategory;
		using EZ = EACFTransitionEasing;

		/** One row of the catalog: identity + default look. */
		struct FRow
		{
			int32 Id;
			const TCHAR* Name;
			C Category;
			bool bSnap;
			bool bOpenSnap;
			const TCHAR* Description;
			float Duration;
			EZ Easing;
			FLinearColor Color;
			FLinearColor Color2;
			float Direction;
			float Count;
			float Intensity;
			float Softness;
			bool bReverse;
		};

		const FLinearColor Black(0.f, 0.f, 0.f);
		const FLinearColor White(1.f, 1.f, 1.f);

		// Defaults tuned in the Transition Preview (README 33 章). Colours are linear.
		const FRow Rows[NumEffects] = {
			{ 1, TEXT("Black Fade"), C::Fade, false, false, TEXT("Uniform fade to the colour."), 0.6f, EZ::EaseInOut, Black, White, 0, 1, 1, 0, false },
			{ 2, TEXT("White Flash"), C::Fade, false, false, TEXT("Over-exposed flash: the centre whites out first with a warm glare."), 0.35f, EZ::EaseIn, White, FLinearColor(1.f, 0.8f, 0.5f), 0, 1, 1, 0, false },
			{ 3, TEXT("Color Fade"), C::Fade, false, false, TEXT("Fade to a two-colour vertical gradient (Color -> Secondary)."), 0.7f, EZ::EaseInOut, FLinearColor(0.04f, 0.16f, 0.45f), FLinearColor(0.32f, 0.04f, 0.28f), 0, 1, 1, 0, false },
			{ 4, TEXT("Cross Dissolve"), C::CameraScreenshot, true, true, TEXT("Close freezes a capture of the old frame; Open dissolves it into the new live scene. Without a capture: noise dissolve to the colour."), 0.8f, EZ::EaseInOut, Black, White, 0, 1, 1, 0, false },
			{ 5, TEXT("Horizontal Wipe"), C::WipeMask, false, false, TEXT("Hard-edged directional wipe with a thin accent edge; Open continues in the same direction."), 0.55f, EZ::EaseInOut, Black, FLinearColor(0.3f, 0.6f, 1.f), 0, 1, 1, 0, false },
			{ 6, TEXT("Diagonal Wipe"), C::WipeMask, false, false, TEXT("The wipe base at 35 degrees with a feathered edge."), 0.6f, EZ::EaseInOut, Black, FLinearColor(0.9f, 0.9f, 1.f), 35, 1, 1, 0.4f, false },
			{ 7, TEXT("Circle Iris"), C::IrisSpiral, false, false, TEXT("Hard circular iris around Center."), 0.6f, EZ::EaseIn, Black, White, 0, 1, 1, 0, false },
			{ 8, TEXT("Iris Close / Open"), C::IrisSpiral, false, false, TEXT("Camera diaphragm: a rotating 6-blade polygon aperture with blade seams."), 0.7f, EZ::EaseInOut, FLinearColor(0.035f, 0.035f, 0.04f), FLinearColor(0.25f, 0.25f, 0.28f), 0, 6, 1, 0, false },
			{ 9, TEXT("Vertical Blinds"), C::WipeMask, false, false, TEXT("Vertical slats turn shut with a staggered order and cylinder shading."), 0.7f, EZ::EaseInOut, FLinearColor(0.05f, 0.05f, 0.06f), FLinearColor(0.6f, 0.6f, 0.65f), 0, 12, 1, 0, false },
			{ 10, TEXT("Horizontal Shutter"), C::WipeMask, false, false, TEXT("Two slatted metal shutters meet in the middle with a bounce."), 0.65f, EZ::Linear, FLinearColor(0.12f, 0.12f, 0.13f), FLinearColor(0.9f, 0.5f, 0.1f), 0, 1, 1, 0, false },
			{ 11, TEXT("Scanlines"), C::Digital, false, false, TEXT("Interlaced horizontal lines thicken (odd lines first) with line flicker."), 0.6f, EZ::Linear, Black, FLinearColor(0.2f, 0.9f, 0.6f), 0, 90, 1, 0, false },
			{ 12, TEXT("Digital Glitch"), C::Digital, true, false, TEXT("Displaced RGB-split bands of the capture and random blocks dropping to black (time-quantised)."), 0.7f, EZ::Linear, Black, FLinearColor(1.f, 0.1f, 0.6f), 0, 1, 1, 0, false },
			{ 13, TEXT("Pixel Disintegration"), C::Digital, false, false, TEXT("A square pixel grid: cells grow in a random order biased along Direction, with sparks."), 0.8f, EZ::Linear, Black, FLinearColor(0.4f, 0.8f, 1.f), 0, 40, 1, 0, false },
			{ 14, TEXT("Mosaic Transition"), C::Distortion, true, false, TEXT("The capture pixelates with growing blocks, then fades; Open: shrinking random mosaic tiles."), 0.8f, EZ::EaseIn, Black, FLinearColor(0.2f, 0.2f, 0.25f), 0, 1, 1, 0, false },
			{ 15, TEXT("Zoom Rush"), C::Distortion, true, false, TEXT("Radial zoom blur of the capture with speed lines; Open: spiky starburst iris with speed lines."), 0.55f, EZ::EaseIn, White, White, 0, 1, 1, 0, false },
			{ 16, TEXT("Rotation Spin"), C::Distortion, true, false, TEXT("The capture twists and shrinks into the centre; Open: curved pinwheel arms."), 0.7f, EZ::EaseIn, Black, White, 0, 4, 1, 0, false },
			{ 17, TEXT("Portal Ring"), C::OverlayAnimation, false, false, TEXT("A glowing noisy ring with a swirling vortex inside expands from Center."), 0.8f, EZ::EaseInOut, FLinearColor(0.01f, 0.0f, 0.04f), FLinearColor(0.4f, 0.2f, 1.f), 0, 1, 1, 0, false },
			{ 18, TEXT("Cell Disintegration"), C::Digital, false, false, TEXT("Voronoi cells fill from their centres with glowing cell borders."), 0.9f, EZ::Linear, Black, FLinearColor(1.f, 0.5f, 0.1f), 0, 9, 1, 0, false },
			{ 19, TEXT("Ink Spread"), C::OverlayAnimation, false, false, TEXT("Seven ink blots with noisy edges and splatter drops spread over the screen."), 1.0f, EZ::EaseIn, FLinearColor(0.005f, 0.005f, 0.015f), FLinearColor(0.1f, 0.1f, 0.3f), 0, 1, 1, 0, false },
			{ 20, TEXT("Slice Shift"), C::Distortion, true, false, TEXT("Horizontal slices slide alternately sideways, pushing the captured image out."), 0.65f, EZ::Linear, Black, FLinearColor(0.9f, 0.9f, 0.9f), 0, 8, 1, 0, false },
			{ 21, TEXT("Light Sweep"), C::OverlayAnimation, false, false, TEXT("A bright glare band sweeps across; behind it the screen becomes light."), 0.6f, EZ::EaseInOut, FLinearColor(1.f, 0.95f, 0.85f), FLinearColor(1.f, 0.8f, 0.4f), -20, 1, 1, 0, false },
			{ 22, TEXT("Film Burn"), C::OverlayAnimation, false, false, TEXT("Noise burn holes grow from one side with hot orange-yellow edges."), 1.0f, EZ::Linear, Black, FLinearColor(1.f, 0.35f, 0.05f), 0, 1, 1, 0, false },
			{ 23, TEXT("Spatial Distortion"), C::Distortion, true, false, TEXT("Wave + swirl warp with chromatic aberration of the capture; Open: warped noise front with RGB edges."), 0.8f, EZ::EaseIn, Black, FLinearColor(0.6f, 0.8f, 1.f), 0, 1, 1, 0, false },
			{ 24, TEXT("Loading Card"), C::CardLoading, false, false, TEXT("A slanted card slides up with stripes and a spinner; shows LoadingText while covered."), 0.5f, EZ::EaseOut, FLinearColor(0.02f, 0.03f, 0.07f), FLinearColor(0.3f, 0.6f, 1.f), 0, 1, 1, 0, false },
			{ 25, TEXT("Net Sweep"), C::OverlayAnimation, false, false, TEXT("A diamond rope net with a border rope sweeps diagonally and darkens behind it."), 0.9f, EZ::EaseInOut, FLinearColor(0.01f, 0.01f, 0.01f), FLinearColor(0.55f, 0.42f, 0.25f), 30, 1, 1, 0, false },
			{ 26, TEXT("Cage Door"), C::OverlayAnimation, false, false, TEXT("Shaded metal bars with cross bars drop with a bounce; then darkness fills behind."), 1.0f, EZ::Linear, Black, FLinearColor(0.55f, 0.55f, 0.6f), 0, 10, 1, 0, false },
			{ 27, TEXT("Capture Beam"), C::OverlayAnimation, false, false, TEXT("A tractor beam with scrolling rings and particles widens from Center.x and whites out."), 0.9f, EZ::EaseIn, White, FLinearColor(0.3f, 0.9f, 1.f), 0, 1, 1, 0, false },
			{ 28, TEXT("Boar Footprints"), C::OverlayAnimation, false, false, TEXT("Cloven hoof prints stamp along a walk, then darkness spreads from them."), 1.2f, EZ::Linear, FLinearColor(0.02f, 0.012f, 0.005f), FLinearColor(0.35f, 0.2f, 0.08f), 20, 1, 1, 0, false },
			{ 29, TEXT("Hologram Transfer"), C::Digital, true, false, TEXT("The capture turns into a cyan scan-lined hologram and beams up column by column."), 1.0f, EZ::Linear, FLinearColor(0.0f, 0.01f, 0.02f), FLinearColor(0.2f, 0.9f, 1.f), 0, 1, 1, 0, false },
			{ 30, TEXT("Wireframe Conversion"), C::CameraScreenshot, true, false, TEXT("Screen-space Sobel edges of the capture become glowing lines behind a scan front (not mesh wireframe)."), 1.1f, EZ::Linear, FLinearColor(0.0f, 0.01f, 0.015f), FLinearColor(0.2f, 1.f, 0.4f), 0, 1, 1, 0, false },
			{ 31, TEXT("Data Cubes"), C::Digital, false, false, TEXT("A grid of shaded cubes flips up in a random order."), 0.9f, EZ::Linear, FLinearColor(0.02f, 0.06f, 0.12f), FLinearColor(0.2f, 0.5f, 0.9f), 0, 14, 1, 0, false },
			{ 32, TEXT("CRT Power Off"), C::CameraScreenshot, true, false, TEXT("The capture squeezes to a bright line, then a dot; Open: power-on line."), 0.6f, EZ::Linear, Black, FLinearColor(0.7f, 0.85f, 1.f), 0, 1, 1, 0, false },
			{ 33, TEXT("Laboratory Auto Door"), C::OverlayAnimation, false, false, TEXT("Two sci-fi door panels slide in from the sides with hazard stripes and lamps."), 0.7f, EZ::Linear, FLinearColor(0.15f, 0.17f, 0.2f), FLinearColor(1.f, 0.75f, 0.05f), 0, 1, 1, 0, false },
			{ 34, TEXT("Stage Card"), C::CardLoading, false, false, TEXT("Four skewed two-tone bands slide in one after another; shows LoadingText (stage title)."), 0.8f, EZ::Linear, FLinearColor(0.35f, 0.02f, 0.02f), FLinearColor(0.02f, 0.02f, 0.03f), 0, 1, 1, 0, false },
			{ 35, TEXT("Manga Panels"), C::OverlayAnimation, false, false, TEXT("Five comic panels with white gutters fill with halftone dots, then black ink."), 1.0f, EZ::Linear, Black, FLinearColor(0.35f, 0.35f, 0.35f), 0, 1, 1, 0, false },
			{ 36, TEXT("Sword Slash"), C::CameraScreenshot, true, false, TEXT("A bright slash is drawn, then the captured image splits along it and slides apart."), 0.8f, EZ::Linear, Black, FLinearColor(0.9f, 0.95f, 1.f), -25, 1, 1, 0, false },
			{ 37, TEXT("Fog / Smoke"), C::OverlayAnimation, false, false, TEXT("Drifting two-layer fbm smoke rises from the bottom."), 1.1f, EZ::EaseIn, FLinearColor(0.6f, 0.62f, 0.65f), FLinearColor(0.25f, 0.27f, 0.3f), 0, 1, 1, 0.3f, false },
			{ 38, TEXT("Shockwave"), C::Distortion, true, false, TEXT("A refracting ring displaces the capture and leaves the colour behind; Open: ring with chromatic edge."), 0.7f, EZ::EaseOut, White, White, 0, 1, 1, 0, false },
			{ 39, TEXT("Match Cut"), C::CameraScreenshot, true, false, TEXT("The frozen frame zooms to a soft spotlight that holds on Center, then cuts; Open pops the same spot and widens."), 0.9f, EZ::Linear, Black, White, 0, 1, 1, 0, false },
			{ 40, TEXT("Whip Pan"), C::CameraScreenshot, true, false, TEXT("The capture pans away with a 12-tap motion blur; Open: fast horizontal streak rows."), 0.4f, EZ::EaseIn, FLinearColor(0.12f, 0.12f, 0.14f), FLinearColor(0.5f, 0.5f, 0.55f), 0, 1, 1, 0, false },
			{ 41, TEXT("Spiral Close"), C::IrisSpiral, false, false, TEXT("Spiral wipe: a hand turns around Center and blacks out everything beyond its shrinking radius, so the scene closes in one smooth spiral to a point. Same mask as 42."), 1.0f, EZ::Linear, Black, White, 0, 1.0f, 1, 0, false },
			{ 42, TEXT("Spiral Open"), C::IrisSpiral, false, false, TEXT("The exact reverse of 41 Spiral Close: the scene opens from a point along the same spiral while the hand turns back."), 0.9f, EZ::Linear, Black, White, 0, 1.0f, 1, 0, false },
			{ 43, TEXT("Focus Pull"), C::Fade, false, false, TEXT("Defocuses the live viewport before coverage, then brings the incoming scene back into focus."), 0.75f, EZ::EaseInOut, FLinearColor(0.008f, 0.014f, 0.022f), White, 0, 1, 1, 0, false },
			{ 44, TEXT("Luma Dissolve"), C::CameraScreenshot, true, true, TEXT("Bright pixels of the outgoing capture dissolve first on Open. Reverse makes dark pixels dissolve first."), 0.85f, EZ::Linear, Black, White, 0, 1, 1, 0.36f, false },
			{ 45, TEXT("Page Turn"), C::WipeMask, false, false, TEXT("An angled paper fold and its shadow sweep across the frame. Direction sets the travel direction."), 0.8f, EZ::EaseInOut, FLinearColor(0.68f, 0.49f, 0.28f), FLinearColor(0.97f, 0.83f, 0.56f), 158, 1, 1, 0, false },
			{ 46, TEXT("Quick Cross Dissolve"), C::CameraScreenshot, true, true, TEXT("A short Close limits the freeze. Open starts the blend immediately and reveals more of the live scene early."), 0.14f, EZ::Linear, Black, White, 0, 1, 1, 0, false },
			{ 47, TEXT("Crisp Wipe"), C::WipeMask, false, false, TEXT("A horizontal wipe with a crisp edge, antialiased to screen pixels."), 0.55f, EZ::EaseInOut, Black, White, 0, 1, 0, 0, false },
			{ 48, TEXT("Soft Wipe"), C::WipeMask, false, false, TEXT("A wide, feathered diagonal edge. Softness controls the width of the feather."), 0.6f, EZ::EaseInOut, Black, White, 35, 1, 0, 0.57f, false },
			{ 49, TEXT("Glow Wipe"), C::WipeMask, false, false, TEXT("A diagonal wipe with a luminous accent line. Intensity sets the glow strength and Secondary Color sets its colour."), 0.6f, EZ::EaseInOut, Black, FLinearColor(0.31f, 1.f, 0.73f), 35, 1, 1, 0.15f, false },
			{ 50, TEXT("Subject Iris"), C::IrisSpiral, false, false, TEXT("A flat diaphragm mask frames a subject. Set Center separately in Close and Open to frame the outgoing and incoming subjects."), 0.7f, EZ::EaseInOut, Black, White, 0, 6, 1, 0.08f, false },
		};

		TArray<FACFTransitionEffectInfo> BuildInfos()
		{
			TArray<FACFTransitionEffectInfo> Out;
			for (const FRow& Row : Rows)
			{
				FACFTransitionEffectInfo& Info = Out.AddDefaulted_GetRef();
				Info.Effect = FromId(Row.Id);
				Info.Id = Row.Id;
				Info.Name = Row.Name;
				Info.Category = Row.Category;
				Info.bUsesSnapshot = Row.bSnap;
				Info.bOpenUsesSnapshot = Row.bOpenSnap;
				Info.Description = Row.Description;
			}
			return Out;
		}

		FString Simplify(const FString& In)
		{
			FString Out;
			for (TCHAR Ch : In)
			{
				if (FChar::IsAlnum(Ch))
				{
					Out.AppendChar(FChar::ToLower(Ch));
				}
			}
			return Out;
		}
	}

	const TArray<FACFTransitionEffectInfo>& GetAllEffects()
	{
		static const TArray<FACFTransitionEffectInfo> Infos = BuildInfos();
		return Infos;
	}

	const FACFTransitionEffectInfo& GetEffectInfo(EACFTransitionEffect Effect)
	{
		static const FACFTransitionEffectInfo None;
		const int32 Id = static_cast<int32>(Effect);
		return (Id >= 1 && Id <= NumEffects) ? GetAllEffects()[Id - 1] : None;
	}

	FACFTransitionSettings MakeDefaultSettings(EACFTransitionEffect Effect, EACFTransitionPhase Phase)
	{
		FACFTransitionSettings S;
		const int32 Id = static_cast<int32>(Effect);
		if (Id < 1 || Id > NumEffects)
		{
			S.Effect = EACFTransitionEffect::None;
			return S;
		}
		const FRow& Row = Rows[Id - 1];
		S.Effect = Effect;
		S.Duration = Row.Duration;
		S.Easing = Row.Easing;
		// An ease-in close reads as an ease-out open (and back), so the open default mirrors it.
		if (Phase == EACFTransitionPhase::Open)
		{
			if (Row.Easing == EZ::EaseIn) { S.Easing = EZ::EaseOut; }
			else if (Row.Easing == EZ::EaseOut) { S.Easing = EZ::EaseIn; }
		}
		S.Color = Row.Color;
		S.SecondaryColor = Row.Color2;
		S.DirectionDegrees = Row.Direction;
		S.Count = Row.Count;
		S.Intensity = Row.Intensity;
		S.Softness = Row.Softness;
		S.bReverse = Row.bReverse;
		if (Effect == EACFTransitionEffect::LumaDissolve || Effect == EACFTransitionEffect::QuickCrossDissolve)
		{
			// Acquire the outgoing frame shortly before the cut, then spend the longer phase revealing the new scene.
			S.Duration = Phase == EACFTransitionPhase::Close ? 0.14f : Row.Duration;
			if (Effect == EACFTransitionEffect::QuickCrossDissolve && Phase == EACFTransitionPhase::Open)
			{
				S.Duration = 0.52f;
			}
		}
		if (Effect == EACFTransitionEffect::SubjectIris)
		{
			S.Center = Phase == EACFTransitionPhase::Close ? FVector2D(0.61, 0.44) : FVector2D(0.32, 0.34);
		}
		if (Effect == EACFTransitionEffect::LoadingCard)
		{
			S.LoadingText = NSLOCTEXT("ACFTransition", "Loading", "NOW LOADING");
		}
		else if (Effect == EACFTransitionEffect::StageCard)
		{
			S.LoadingText = NSLOCTEXT("ACFTransition", "Stage", "STAGE START");
		}
		return S;
	}

	EACFTransitionEffect ParseEffect(const FString& Text)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		if (Trimmed.IsNumeric())
		{
			return FromId(FCString::Atoi(*Trimmed));
		}
		const FString Key = Simplify(Trimmed);
		for (const FACFTransitionEffectInfo& Info : GetAllEffects())
		{
			if (Simplify(Info.Name) == Key || Simplify(StaticEnum<EACFTransitionEffect>()->GetNameStringByValue(Info.Id)) == Key)
			{
				return Info.Effect;
			}
		}
		return EACFTransitionEffect::None;
	}

	float Ease(const FACFTransitionSettings& Settings, float Alpha)
	{
		const float A = FMath::Clamp(Alpha, 0.f, 1.f);
		switch (Settings.Easing)
		{
		case EACFTransitionEasing::EaseIn: return A * A;
		case EACFTransitionEasing::EaseOut: return 1.f - (1.f - A) * (1.f - A);
		case EACFTransitionEasing::EaseInOut: return A * A * (3.f - 2.f * A);
		case EACFTransitionEasing::Curve: return Settings.Curve ? FMath::Clamp(Settings.Curve->GetFloatValue(A), 0.f, 1.f) : A;
		default: return A;
		}
	}

	const TCHAR* LexEvent(EACFTransitionEvent Event)
	{
		switch (Event)
		{
		case EACFTransitionEvent::Started: return TEXT("Started");
		case EACFTransitionEvent::Covered: return TEXT("Covered");
		case EACFTransitionEvent::LoadStarted: return TEXT("LoadStarted");
		case EACFTransitionEvent::LoadCompleted: return TEXT("LoadCompleted");
		case EACFTransitionEvent::Finished: return TEXT("Finished");
		case EACFTransitionEvent::Failed: return TEXT("Failed");
		case EACFTransitionEvent::Cancelled: return TEXT("Cancelled");
		case EACFTransitionEvent::Interrupted: return TEXT("Interrupted");
		default: return TEXT("?");
		}
	}
}

bool FACFTransitionSettings::IsValid() const
{
	return ACFTransition::FromId(static_cast<int32>(Effect)) != EACFTransitionEffect::None;
}
