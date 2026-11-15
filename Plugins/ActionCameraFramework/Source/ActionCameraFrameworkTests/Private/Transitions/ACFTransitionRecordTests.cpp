#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFTransitionTestUtils.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "Widgets/SViewport.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

/**
 * Phase 7D: frames of the real game viewport for a human to look at (README 33 章「目視確認用の出力」). Not part of the
 * regression: it runs only when the environment variable ACF_RECORD_TRANSITIONS is set (1 = all 42, or a list such as
 * "41,42"), rendered (RunTests.sh --context samples ActionCameraFramework.Transitions.Record).
 *
 * For every effect the real overlay is frozen (ShowStill) over the game's own frame at 17 Close points (0 .. 100 %) and 17
 * Open points (0 .. 100 % played), and each frame of the game viewport (scene, game UI and the transition overlay) is
 * saved as Saved/ACFTransitions/Record/<ID>_<C|O>_<NN>.png (480 x 270). Scripts/Transitions/MakeTransitionReview.ps1 then
 * puts the number and name on every frame, makes one MP4 of all of them and the sheets of representative frames.
 */
namespace ACFTransitionRecord
{
	constexpr int32 Steps = 16;
	constexpr int32 OutW = 480;
	constexpr int32 OutH = 270;

	struct FCtx
	{
		TWeakObjectPtr<UACFTransitionSubsystem> S;
		TArray<int32> Ids;
		int32 Index = 0;
		int32 Frame = -1; // -1 = clear and settle; 0 .. 2 * (Steps + 1) - 1 = Close then Open points
		int32 Wait = 0;
		int32 Saved = 0;
		FString Dir;
	};

	static bool Grab(TArray<FColor>& Out)
	{
		UWorld* World = ACFTransitionTest::FindGameWorld();
		UGameViewportClient* VC = World ? World->GetGameViewport() : nullptr;
		TSharedPtr<SViewport> Widget = VC ? VC->GetGameViewportWidget() : nullptr;
		FIntVector Size(0, 0, 0);
		TArray<FColor> Pixels;
		if (!Widget.IsValid() || !FSlateApplication::Get().TakeScreenshot(Widget.ToSharedRef(), Pixels, Size) || Size.X <= 0 || Pixels.Num() != Size.X * Size.Y)
		{
			return false;
		}
		Out.SetNumUninitialized(OutW * OutH);
		FImageUtils::ImageResize(Size.X, Size.Y, Pixels, OutW, OutH, Out, /*bLinearSpace*/ false, /*bForceOpaqueOutput*/ true);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionRecordTest, "ActionCameraFramework.Transitions.Record.Frames", ACFTransitionTest::Flags)
bool FACFTransitionRecordTest::RunTest(const FString&)
{
	using namespace ACFTransitionRecord;
	const FString Which = FPlatformMisc::GetEnvironmentVariable(TEXT("ACF_RECORD_TRANSITIONS"));
	UACFTransitionSubsystem* S = ACFTransitionTest::FindSubsystem();
	if (Which.IsEmpty() || GUsingNullRHI || !S)
	{
		AddWarning(TEXT("Skipped: set ACF_RECORD_TRANSITIONS=1 (or e.g. 41,42) and run rendered (RunTests.sh --context samples)."));
		return true;
	}
	TSharedPtr<FCtx> Ctx = MakeShared<FCtx>();
	Ctx->S = S;
	if (Which == TEXT("1"))
	{
		for (int32 Id = 1; Id <= ACFTransition::NumEffects; ++Id) { Ctx->Ids.Add(Id); }
	}
	else
	{
		TArray<FString> Parts;
		Which.ParseIntoArray(Parts, TEXT(","));
		for (const FString& P : Parts) { const int32 Id = FCString::Atoi(*P); if (Id >= 1 && Id <= ACFTransition::NumEffects) { Ctx->Ids.Add(Id); } }
	}
	Ctx->Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ACFTransitions"), TEXT("Record")));
	IFileManager::Get().MakeDirectory(*Ctx->Dir, true);
	S->ClearScreen();

	TArray<TFunction<bool()>> StepList;
	// Warm-up: the transition material's shaders must be compiled before the first frame is saved (an editor build
	// compiles them on first use; until then the overlay draws nothing).
	StepList.Add([Ctx]()
	{
		if (UACFTransitionSubsystem* Sub = Ctx->S.Get())
		{
			Sub->ShowStill(ACFTransition::MakeDefaultSettings(EACFTransitionEffect::SpiralClose), EACFTransitionPhase::Close, 0.5f);
		}
#if WITH_EDITOR
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		FAssetCompilingManager::Get().FinishAllCompilation();
#endif
		FlushRenderingCommands();
		return ++Ctx->Wait > 5;
	});
	StepList.Add([Ctx]()
	{
		if (UACFTransitionSubsystem* Sub = Ctx->S.Get()) { Sub->ClearScreen(); }
		Ctx->Wait = 3;
		return true;
	});
	StepList.Add([this, Ctx]()
	{
		UACFTransitionSubsystem* Sub = Ctx->S.Get();
		if (!Sub || Ctx->Index >= Ctx->Ids.Num())
		{
			return true;
		}
		if (Ctx->Wait > 0)
		{
			--Ctx->Wait;
			return false;
		}
		const int32 Id = Ctx->Ids[Ctx->Index];
		const EACFTransitionEffect Effect = ACFTransition::FromId(Id);
		const int32 Points = Steps + 1;
		if (Ctx->Frame >= 0)
		{
			// The frame shown since the last step: save it.
			TArray<FColor> Pixels;
			if (!Grab(Pixels))
			{
				AddError(FString::Printf(TEXT("%02d: could not read the viewport."), Id));
				return true;
			}
			const bool bOpen = Ctx->Frame >= Points;
			const int32 N = bOpen ? Ctx->Frame - Points : Ctx->Frame;
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(OutW, OutH, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			FFileHelper::SaveArrayToFile(Png, *FPaths::Combine(Ctx->Dir, FString::Printf(TEXT("%02d_%s_%02d.png"), Id, bOpen ? TEXT("O") : TEXT("C"), N)));
			++Ctx->Saved;
		}
		++Ctx->Frame;
		if (Ctx->Frame >= 2 * Points)
		{
			Sub->ClearScreen();
			Ctx->Frame = -1;
			++Ctx->Index;
			Ctx->Wait = 3;
			return false;
		}
		if (Ctx->Frame == 0)
		{
			Sub->ClearScreen();
		}
		const bool bOpen = Ctx->Frame >= Points;
		const float Played = (bOpen ? Ctx->Frame - Points : Ctx->Frame) / float(Steps);
		const EACFTransitionPhase Phase = bOpen ? EACFTransitionPhase::Open : EACFTransitionPhase::Close;
		const float Coverage = bOpen ? 1.f - Played : Played;
		if (Coverage <= 0.f && !bOpen)
		{
			Sub->ClearScreen(); // 0 %: the game's own frame
		}
		else
		{
			Sub->ShowStill(ACFTransition::MakeDefaultSettings(Effect, Phase), Phase, Coverage);
		}
		Ctx->Wait = 2; // let the frame be drawn
		return false;
	});
	StepList.Add([this, Ctx]()
	{
		FString Index;
		for (int32 Id = 1; Id <= ACFTransition::NumEffects; ++Id) // all 42 (a partial run keeps the other frames)
		{
			const FACFTransitionEffectInfo& Info = ACFTransition::GetEffectInfo(ACFTransition::FromId(Id));
			Index += FString::Printf(TEXT("%02d\t%s\t%s\n"), Id, *Info.Name, Info.bUsesSnapshot ? TEXT("capture") : TEXT(""));
		}
		FFileHelper::SaveStringToFile(Index, *FPaths::Combine(Ctx->Dir, TEXT("index.tsv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		AddInfo(FString::Printf(TEXT("%d frames of %d effects saved to %s"), Ctx->Saved, Ctx->Ids.Num(), *Ctx->Dir));
		TestEqual(TEXT("Every frame saved"), Ctx->Saved, Ctx->Ids.Num() * 2 * (Steps + 1));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(ACFTransitionTest::FSteps(this, MoveTemp(StepList), [Ctx]() { if (Ctx->S.IsValid()) { Ctx->S->ClearScreen(); } }, 1800.0));
	return true;
}

#endif
