#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFTransitionTestUtils.h"
#include "ACFTransitionView.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"
#endif

/**
 * Phase 7D: the real frames of the 42 effects (README 33 章「検証」). Needs a renderer (skipped with -NullRHI); run with
 * Scripts/RunTests.sh --context samples ActionCameraFramework.Transitions.Render.
 *
 *  - AllEffects: the transition widget (the same widget + M_ACFTransition as in the game) is rendered off-screen by Slate
 *    (FWidgetRenderer) for every effect at several coverages, Close and Open, over a known test picture that is also the
 *    screen capture. Checked: coverage 0 draws nothing, coverage 1 covers every pixel; every pair of effects differs (the
 *    mean difference of their frame sequences is above a threshold: no effect is another one under a second name),
 *    except 41 / 42, which share one spiral mask by design (42 is checked to be the exact reverse of 41).
 *    Contact sheets go to Saved/ACFTransitions/ (one cell per effect, ID order, 7 per row).
 *  - ViewportFullCover: in the real game viewport, each effect frozen at coverage 1 hides the world completely (the
 *    frame does not change when world rendering is switched off) and at coverage 0 shows it.
 */
namespace ACFTransitionRender
{
	constexpr int32 CellW = 256;
	constexpr int32 CellH = 144;
	constexpr int32 SigW = 48;
	constexpr int32 SigH = 27;

	/** A synthetic "game frame": sky gradient, ground, coloured blocks and a sun (the capture of the snapshot effects). */
	static TArray<FColor> MakeScene(int32 W, int32 H)
	{
		TArray<FColor> P;
		P.SetNumUninitialized(W * H);
		for (int32 y = 0; y < H; ++y)
		{
			for (int32 x = 0; x < W; ++x)
			{
				const float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
				FLinearColor C = v < 0.6f ? FMath::Lerp(FLinearColor(0.25f, 0.45f, 0.85f), FLinearColor(0.75f, 0.85f, 0.95f), v / 0.6f)
					: FMath::Lerp(FLinearColor(0.35f, 0.55f, 0.25f), FLinearColor(0.2f, 0.35f, 0.15f), (v - 0.6f) / 0.4f);
				if (FMath::Square(u - 0.8f) * 3.2f + FMath::Square(v - 0.2f) < 0.006f) { C = FLinearColor(1.f, 0.9f, 0.5f); }
				for (int32 k = 0; k < 5; ++k)
				{
					const float bx = 0.1f + k * 0.18f, bw = 0.06f + 0.02f * (k % 2), top = 0.62f - 0.08f * (k % 3) - 0.1f;
					if (u > bx && u < bx + bw && v > top && v < 0.66f)
					{
						static const FLinearColor Cols[5] = { {0.9f, 0.3f, 0.1f}, {0.95f, 0.8f, 0.1f}, {0.6f, 0.2f, 0.7f}, {0.9f, 0.9f, 0.9f}, {0.2f, 0.7f, 0.9f} };
						C = Cols[k] * (0.7f + 0.3f * (u - bx) / bw);
					}
				}
				if ((x / 16 + y / 16) % 2 == 0 && v > 0.66f) { C *= 0.85f; }
				P[y * W + x] = C.ToFColor(true);
			}
		}
		return P;
	}

	static UTexture2D* MakeTexture(const TArray<FColor>& Pixels, int32 W, int32 H)
	{
		UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		T->SRGB = true;
		T->AddressX = TA_Clamp;
		T->AddressY = TA_Clamp;
		void* Data = T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		T->GetPlatformData()->Mips[0].BulkData.Unlock();
		T->UpdateResource();
		return T;
	}

	static void WaitForShaders()
	{
#if WITH_EDITOR
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		FAssetCompilingManager::Get().FinishAllCompilation();
#endif
		FlushRenderingCommands();
	}

	/** Renders the view's widget into RT and reads it back (premultiplied RGBA). */
	struct FOffscreen
	{
		FWidgetRenderer Renderer{ false, true };
		UTextureRenderTarget2D* Target = nullptr;

		FOffscreen()
		{
			Target = FWidgetRenderer::CreateTargetFor(FVector2D(CellW, CellH), TF_Bilinear, false);
			Target->AddToRoot();
		}
		~FOffscreen()
		{
			if (Target) { Target->RemoveFromRoot(); }
		}
		TArray<FColor> Draw(FACFTransitionView& View)
		{
			Renderer.DrawWidget(Target, View.GetWidget(), FVector2D(CellW, CellH), 0.f);
			FlushRenderingCommands();
			TArray<FColor> Out;
			FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
			ReadFlags.SetLinearToGamma(false);
			Target->GameThread_GetRenderTargetResource()->ReadPixels(Out, ReadFlags);
			return Out;
		}
	};

	static int32 MaxAlphaDiff(const TArray<FColor>& A, const TArray<FColor>& B)
	{
		int32 M = A.Num() == B.Num() ? 0 : 255;
		for (int32 i = 0; i < A.Num() && i < B.Num(); ++i) { M = FMath::Max(M, FMath::Abs((int32)A[i].A - (int32)B[i].A)); }
		return M;
	}

	/**
	 * 4-connected regions of covered pixels (alpha > 127), ignoring specks under 6 pixels (anti-aliasing). The regions that
	 * touch the frame border count as one (a ribbon that leaves the screen and comes back is still one ribbon).
	 */
	static int32 CountRegions(const TArray<FColor>& P, int32 W, int32 H)
	{
		TArray<uint8> Seen;
		Seen.Init(0, W * H);
		int32 Regions = 0;
		bool bAnyBorder = false;
		TArray<int32> Stack;
		for (int32 i = 0; i < W * H; ++i)
		{
			if (Seen[i] || P[i].A <= 127) { continue; }
			int32 Size = 0;
			bool bBorder = false;
			Stack.Reset();
			Stack.Add(i);
			Seen[i] = 1;
			while (Stack.Num() > 0)
			{
				const int32 j = Stack.Pop(EAllowShrinking::No);
				++Size;
				const int32 x = j % W, y = j / W;
				bBorder |= x == 0 || y == 0 || x == W - 1 || y == H - 1;
				const int32 N[4] = { x > 0 ? j - 1 : -1, x < W - 1 ? j + 1 : -1, y > 0 ? j - W : -1, y < H - 1 ? j + W : -1 };
				for (int32 k : N)
				{
					if (k >= 0 && !Seen[k] && P[k].A > 127) { Seen[k] = 1; Stack.Add(k); }
				}
			}
			if (bBorder) { bAnyBorder = true; } else { Regions += Size >= 6 ? 1 : 0; }
		}
		return Regions + (bAnyBorder ? 1 : 0);
	}

	static float MeanDiff(const TArray<FColor>& A, const TArray<FColor>& B)
	{
		double Sum = 0.0;
		for (int32 i = 0; i < A.Num() && i < B.Num(); ++i)
		{
			Sum += FMath::Abs(A[i].R - B[i].R) + FMath::Abs(A[i].G - B[i].G) + FMath::Abs(A[i].B - B[i].B) + FMath::Abs(A[i].A - B[i].A);
		}
		return static_cast<float>(Sum / (FMath::Max(1, A.Num()) * 4.0));
	}

	static TArray<FColor> Downsample(const TArray<FColor>& In)
	{
		TArray<FColor> Out;
		Out.SetNumZeroed(SigW * SigH);
		for (int32 y = 0; y < SigH; ++y)
		{
			for (int32 x = 0; x < SigW; ++x)
			{
				int32 R = 0, G = 0, B = 0, A = 0, N = 0;
				for (int32 yy = y * CellH / SigH; yy < (y + 1) * CellH / SigH; ++yy)
				{
					for (int32 xx = x * CellW / SigW; xx < (x + 1) * CellW / SigW; ++xx)
					{
						const FColor& C = In[yy * CellW + xx];
						R += C.R; G += C.G; B += C.B; A += C.A; ++N;
					}
				}
				N = FMath::Max(N, 1);
				Out[y * SigW + x] = FColor(R / N, G / N, B / N, A / N);
			}
		}
		return Out;
	}

	/** Composites a premultiplied cell over the scene (what the player sees) into a sheet. */
	static void Blit(TArray<FColor>& Sheet, int32 SheetW, int32 Col, int32 Row, const TArray<FColor>& Cell, const TArray<FColor>& Scene)
	{
		for (int32 y = 0; y < CellH; ++y)
		{
			for (int32 x = 0; x < CellW; ++x)
			{
				const FColor& F = Cell[y * CellW + x];
				const FColor& Bg = Scene[y * CellW + x];
				const float A = F.A / 255.f;
				const int32 sx = Col * (CellW + 4) + x, sy = Row * (CellH + 4) + y;
				Sheet[sy * SheetW + sx] = FColor(FMath::Min(255, F.R + FMath::RoundToInt(Bg.R * (1.f - A))), FMath::Min(255, F.G + FMath::RoundToInt(Bg.G * (1.f - A))), FMath::Min(255, F.B + FMath::RoundToInt(Bg.B * (1.f - A))), 255);
			}
		}
	}

	static void SaveSheet(const FString& Name, const TArray<FColor>& Sheet, int32 W, int32 H)
	{
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(W, H, TArrayView64<const FColor>(Sheet.GetData(), Sheet.Num()), Png);
		FFileHelper::SaveArrayToFile(Png, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ACFTransitions"), Name + TEXT(".png")));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionRenderAllTest, "ActionCameraFramework.Transitions.Render.AllEffects", ACFTransitionTest::Flags)
bool FACFTransitionRenderAllTest::RunTest(const FString&)
{
	using namespace ACFTransitionRender;
	if (GUsingNullRHI || !FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped: needs a renderer (run without -NullRHI, e.g. RunTests.sh --context samples)."));
		return true;
	}
	const TArray<FColor> ScenePixels = MakeScene(CellW, CellH);
	UTexture2D* SceneTex = MakeTexture(ScenePixels, CellW, CellH);
	SceneTex->AddToRoot();
	FOffscreen Off;
	const double T0 = FPlatformTime::Seconds();

	struct FShot { EACFTransitionPhase Phase; float Coverage; const TCHAR* Name; };
	const FShot Shots[] = {
		{ EACFTransitionPhase::Close, 0.f, TEXT("Close000") }, { EACFTransitionPhase::Close, 0.25f, TEXT("Close025") },
		{ EACFTransitionPhase::Close, 0.5f, TEXT("Close050") }, { EACFTransitionPhase::Close, 0.75f, TEXT("Close075") },
		{ EACFTransitionPhase::Close, 1.f, TEXT("Close100") }, { EACFTransitionPhase::Open, 0.f, TEXT("Open000") },
		{ EACFTransitionPhase::Open, 0.35f, TEXT("Open035") }, { EACFTransitionPhase::Open, 0.7f, TEXT("Open070") },
		{ EACFTransitionPhase::Open, 1.f, TEXT("Open100") },
	};
	constexpr int32 NumShots = UE_ARRAY_COUNT(Shots);
	const int32 Cols = 7, Rows = 6;
	const int32 SheetW = Cols * (CellW + 4), SheetH = Rows * (CellH + 4);
	TArray<TArray<FColor>> Sheets;
	Sheets.SetNum(NumShots);
	for (TArray<FColor>& S : Sheets) { S.Init(FColor(40, 40, 40, 255), SheetW * SheetH); }
	TArray<TArray<FColor>> Signatures; // per effect: the middle shots, downsampled
	Signatures.SetNum(ACFTransition::NumEffects);

	{
		// Warm-up draw: compiles the material for this RHI before anything is measured.
		FACFTransitionView View;
		View.SetFallbackSize(FVector2D(CellW, CellH));
		View.Apply(ACFTransition::MakeDefaultSettings(EACFTransitionEffect::BlackFade), EACFTransitionPhase::Close, 0.5f, 0.f, nullptr);
		Off.Draw(View);
		WaitForShaders();
		Off.Draw(View);
	}

	int32 CoverFailures = 0;
	for (int32 Id = 1; Id <= ACFTransition::NumEffects; ++Id)
	{
		const EACFTransitionEffect Effect = ACFTransition::FromId(Id);
		const FACFTransitionEffectInfo& Info = ACFTransition::GetEffectInfo(Effect);
		FACFTransitionView View;
		View.SetFallbackSize(FVector2D(CellW, CellH));
		for (int32 s = 0; s < NumShots; ++s)
		{
			const FShot& Shot = Shots[s];
			FACFTransitionSettings Settings = ACFTransition::MakeDefaultSettings(Effect, Shot.Phase);
			const bool bCapture = Shot.Phase == EACFTransitionPhase::Close ? Info.bUsesSnapshot : Info.bOpenUsesSnapshot;
			// Open coverage runs 1 -> 0: Open035 = 35 % of the open played.
			const float Coverage = Shot.Phase == EACFTransitionPhase::Open ? 1.f - Shot.Coverage : Shot.Coverage;
			View.Apply(Settings, Shot.Phase, Coverage, 0.5f, bCapture ? SceneTex : nullptr);
			TArray<FColor> Pixels = Off.Draw(View);
			if (Pixels.Num() != CellW * CellH)
			{
				AddError(TEXT("Read back failed."));
				SceneTex->RemoveFromRoot();
				return false;
			}
			uint8 MinA = 255, MaxA = 0;
			for (const FColor& C : Pixels) { MinA = FMath::Min(MinA, C.A); MaxA = FMath::Max(MaxA, C.A); }
			if (Coverage <= 0.f && MaxA > 2)
			{
				AddError(FString::Printf(TEXT("%02d %s %s: coverage 0 draws something (max alpha %d)."), Id, *Info.Name, Shot.Name, MaxA));
				++CoverFailures;
			}
			if (Coverage >= 1.f && MinA < 253)
			{
				AddError(FString::Printf(TEXT("%02d %s %s: coverage 1 leaves the scene visible (min alpha %d)."), Id, *Info.Name, Shot.Name, MinA));
				++CoverFailures;
			}
			Blit(Sheets[s], SheetW, (Id - 1) % Cols, (Id - 1) / Cols, Pixels, ScenePixels);
			if (Coverage > 0.f && Coverage < 1.f)
			{
				Signatures[Id - 1].Append(Downsample(Pixels));
			}
		}
	}
	for (int32 s = 0; s < NumShots; ++s)
	{
		SaveSheet(FString::Printf(TEXT("Sheet_%s"), Shots[s].Name), Sheets[s], SheetW, SheetH);
	}

	// Every pair of effects must look different.
	const float Threshold = 3.0f; // mean |difference| per channel (0..255) over the 5 mid-transition frames
	float MinDiff = TNumericLimits<float>::Max();
	int32 MinA = 0, MinB = 0;
	TArray<FString> Close;
	for (int32 a = 0; a < ACFTransition::NumEffects; ++a)
	{
		for (int32 b = a + 1; b < ACFTransition::NumEffects; ++b)
		{
			if (a + 1 == 41 && b + 1 == 42)
			{
				continue; // one shared spiral mask by design (Close and its exact reverse); checked below
			}
			const float D = MeanDiff(Signatures[a], Signatures[b]);
			if (D < MinDiff) { MinDiff = D; MinA = a + 1; MinB = b + 1; }
			if (D < Threshold)
			{
				AddError(FString::Printf(TEXT("%02d and %02d look the same (mean difference %.2f < %.1f)."), a + 1, b + 1, D, Threshold));
			}
			if (D < 12.f)
			{
				Close.Add(FString::Printf(TEXT("%02d-%02d %.1f"), a + 1, b + 1, D));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("42 effects x %d frames rendered in %.1f s; most similar pair %02d / %02d (mean difference %.2f); pairs under 12: %s"),
		NumShots, FPlatformTime::Seconds() - T0, MinA, MinB, MinDiff, Close.IsEmpty() ? TEXT("none") : *FString::Join(Close, TEXT(", "))));
	AddInfo(FString::Printf(TEXT("Contact sheets: %s"), *FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ACFTransitions")))));

	// Spiral strips: 41 and 42 at 12 points each (shape review), plus the checks of the shared spiral mask:
	//  - 42 Spiral Open is the exact time reverse of 41 Spiral Close: Open with p played == Close at coverage 1 - p.
	//  - The black is one connected region (one ribbon, not petals or several shutters) from 30 % to 95 % coverage.
	//  - The start point setting (bFromCenter) changes the motion.
	int32 SpiralFailures = 0;
	TArray<TArray<FColor>> CloseFrames;
	for (const EACFTransitionEffect Effect : { EACFTransitionEffect::SpiralClose, EACFTransitionEffect::SpiralOpen })
	{
		const EACFTransitionPhase Phase = Effect == EACFTransitionEffect::SpiralClose ? EACFTransitionPhase::Close : EACFTransitionPhase::Open;
		TArray<FColor> Strip;
		const int32 StripW = 6 * (CellW + 4), StripH = 2 * (CellH + 4);
		Strip.Init(FColor(40, 40, 40, 255), StripW * StripH);
		FACFTransitionView View;
		View.SetFallbackSize(FVector2D(CellW, CellH));
		for (int32 k = 0; k < 12; ++k)
		{
			const float Played = k / 11.f;
			const float Coverage = Phase == EACFTransitionPhase::Close ? Played : 1.f - Played;
			View.Apply(ACFTransition::MakeDefaultSettings(Effect, Phase), Phase, Coverage, 0.f, nullptr);
			TArray<FColor> Pixels = Off.Draw(View);
			if (Phase == EACFTransitionPhase::Close)
			{
				CloseFrames.Add(Pixels);
				const int32 Regions = CountRegions(Pixels, CellW, CellH);
				if (Coverage > 0.3f && Coverage < 0.95f && Regions != 1) // earlier the ribbon leaves and re-enters the frame
				{
					AddError(FString::Printf(TEXT("41 at coverage %.2f: the black is %d separate regions (expected one ribbon)."), Coverage, Regions));
					++SpiralFailures;
				}
			}
			else
			{
				const int32 Mirror = 11 - k; // the Close frame with the same coverage
				const int32 Diff = MaxAlphaDiff(Pixels, CloseFrames[Mirror]);
				if (Diff > 2)
				{
					AddError(FString::Printf(TEXT("42 with %.2f played differs from 41 at coverage %.2f (max alpha difference %d): the masks are not shared."), Played, Coverage, Diff));
					++SpiralFailures;
				}
			}
			Blit(Strip, StripW, k % 6, k / 6, Pixels, ScenePixels);
		}
		SaveSheet(FString::Printf(TEXT("Spiral_%02d"), (int32)Effect), Strip, StripW, StripH);
	}
	{
		FACFTransitionView View;
		View.SetFallbackSize(FVector2D(CellW, CellH));
		FACFTransitionSettings FromCenter = ACFTransition::MakeDefaultSettings(EACFTransitionEffect::SpiralClose);
		FromCenter.bFromCenter = true;
		View.Apply(FromCenter, EACFTransitionPhase::Close, 5.f / 11.f, 0.f, nullptr);
		const int32 Diff = MaxAlphaDiff(Off.Draw(View), CloseFrames[5]);
		TestTrue(TEXT("bFromCenter changes the spiral"), Diff > 128);
	}
	SceneTex->RemoveFromRoot();
	return CoverFailures == 0 && SpiralFailures == 0;
}

namespace ACFTransitionViewport
{
	struct FCtx
	{
		TWeakObjectPtr<UACFTransitionSubsystem> S;
		TWeakObjectPtr<UGameViewportClient> Viewport;
		int32 Id = 1;
		int32 Phase = 0;
		int32 Wait = 0;
		TArray<FColor> WithWorld;
		int32 Failures = 0;
	};

	static bool Grab(UGameViewportClient* VC, TArray<FColor>& Out)
	{
		FIntVector Size;
		return VC && VC->GetGameViewportWidget().IsValid() && FSlateApplication::Get().TakeScreenshot(VC->GetGameViewportWidget().ToSharedRef(), Out, Size) && Out.Num() > 0;
	}

	static int32 CountDifferent(const TArray<FColor>& A, const TArray<FColor>& B)
	{
		int32 N = 0;
		for (int32 i = 0; i < A.Num() && i < B.Num(); ++i)
		{
			N += (FMath::Abs(A[i].R - B[i].R) + FMath::Abs(A[i].G - B[i].G) + FMath::Abs(A[i].B - B[i].B)) > 6 ? 1 : 0;
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionViewportCoverTest, "ActionCameraFramework.Transitions.Render.ViewportFullCover", ACFTransitionTest::Flags)
bool FACFTransitionViewportCoverTest::RunTest(const FString&)
{
	using namespace ACFTransitionViewport;
	UWorld* World = ACFTransitionTest::FindGameWorld();
	UACFTransitionSubsystem* S = ACFTransitionTest::FindSubsystem();
	if (GUsingNullRHI || !S || !World || !World->GetGameViewport())
	{
		AddWarning(TEXT("Skipped: needs a rendered game viewport (RunTests.sh --context samples)."));
		return true;
	}
	TSharedPtr<FCtx> Ctx = MakeShared<FCtx>();
	Ctx->S = S;
	Ctx->Viewport = World->GetGameViewport();
	S->ClearScreen();
	TArray<TFunction<bool()>> Steps;
	Steps.Add([this, Ctx]()
	{
		UGameViewportClient* VC = Ctx->Viewport.Get();
		UACFTransitionSubsystem* Sub = Ctx->S.Get();
		if (!VC || !Sub) { AddError(TEXT("Viewport lost.")); return true; }
		// Phase 0: show the still (coverage 1) with the world; 1: wait; 2: grab; 3: world off, wait; 4: grab + compare.
		switch (Ctx->Phase)
		{
		case 0:
			Sub->ClearScreen();
			Sub->ShowStill(ACFTransition::MakeDefaultSettings(ACFTransition::FromId(Ctx->Id)), EACFTransitionPhase::Close, 1.f);
			VC->bDisableWorldRendering = false;
			Ctx->Wait = 3; Ctx->Phase = 1; return false;
		case 1:
			if (--Ctx->Wait > 0) { return false; }
			Grab(VC, Ctx->WithWorld); Ctx->Phase = 2; return false;
		case 2:
			VC->bDisableWorldRendering = true;
			Ctx->Wait = 3; Ctx->Phase = 3; return false;
		case 3:
		{
			if (--Ctx->Wait > 0) { return false; }
			TArray<FColor> Without;
			Grab(VC, Without);
			VC->bDisableWorldRendering = false;
			const int32 Diff = CountDifferent(Ctx->WithWorld, Without);
			if (Ctx->WithWorld.Num() == 0 || Diff > Ctx->WithWorld.Num() / 2000)
			{
				AddError(FString::Printf(TEXT("%02d %s: at coverage 1 the world is still visible (%d of %d pixels change)."), Ctx->Id, *ACFTransition::GetEffectInfo(ACFTransition::FromId(Ctx->Id)).Name, Diff, Ctx->WithWorld.Num()));
				++Ctx->Failures;
			}
			Ctx->Phase = 0;
			return ++Ctx->Id > ACFTransition::NumEffects;
		}
		default: return true;
		}
	});
	// Sanity: with the screen clear, switching the world off does change the frame.
	Steps.Add([this, Ctx]()
	{
		UGameViewportClient* VC = Ctx->Viewport.Get();
		switch (Ctx->Phase)
		{
		case 0: Ctx->S->ClearScreen(); Ctx->Wait = 3; Ctx->Phase = 1; return false;
		case 1: if (--Ctx->Wait > 0) { return false; } Grab(VC, Ctx->WithWorld); VC->bDisableWorldRendering = true; Ctx->Wait = 3; Ctx->Phase = 2; return false;
		case 2:
		{
			if (--Ctx->Wait > 0) { return false; }
			TArray<FColor> Without;
			Grab(VC, Without);
			VC->bDisableWorldRendering = false;
			TestTrue(TEXT("Sanity: the world is visible when the screen is clear"), CountDifferent(Ctx->WithWorld, Without) > Ctx->WithWorld.Num() / 10);
			AddInfo(FString::Printf(TEXT("42 effects checked in the game viewport at coverage 1: %d failed."), Ctx->Failures));
			return true;
		}
		default: return true;
		}
	});
	ADD_LATENT_AUTOMATION_COMMAND(ACFTransitionTest::FSteps(this, MoveTemp(Steps), [Ctx]()
	{
		if (Ctx->Viewport.IsValid()) { Ctx->Viewport->bDisableWorldRendering = false; }
		if (Ctx->S.IsValid()) { Ctx->S->ClearScreen(); }
	}, 120.0));
	return true;
}

#endif
