#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFTransitionTestUtils.h"
#include "ACFTransitionView.h"
#include "Engine/GameViewportClient.h"
#include "RHI.h"
#include "RHIGlobals.h"

/**
 * Phase 7D: measured cost of the transitions (README 33 章「性能」). Rendered game (RunTests.sh --context samples
 * ActionCameraFramework.Transitions.Profile; the window is 1280x720). Skipped with -NullRHI.
 *
 *  - GPU: the GPU frame time (RHIGetGPUFrameCycles) of 120 frames with the screen clear, then with each representative
 *    effect frozen at coverage 0.5 (ShowStill: the full-screen material is drawn every frame); the difference is the
 *    effect's cost. Numbers are printed (not asserted: they depend on the GPU), with a generous sanity limit.
 *  - CPU: the subsystem's own Tick while a transition plays (UACFTransitionSubsystem::GetLastTickSeconds), and the one-off
 *    screen capture of the capture-based effects.
 *  - Idle: no tick and no overlay when nothing plays.
 */
namespace ACFTransitionProfile
{
	struct FCtx
	{
		TWeakObjectPtr<UACFTransitionSubsystem> S;
		TArray<int32> Effects;
		int32 Index = -1;          // -1 = baseline
		int32 Frame = 0;
		double SumGpu = 0.0;
		int32 Samples = 0;
		double BaselineMs = 0.0;
		TArray<FString> Lines;
		double CaptureMs = 0.0;
		double TickSum = 0.0, TickMax = 0.0;
		int32 TickN = 0;
		FACFTransitionHandle Handle;
	};

	static double GpuMs() { return FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionProfileTest, "ActionCameraFramework.Transitions.Profile.Cost", ACFTransitionTest::Flags)
bool FACFTransitionProfileTest::RunTest(const FString&)
{
	using namespace ACFTransitionProfile;
	UACFTransitionSubsystem* S = ACFTransitionTest::FindSubsystem();
	UWorld* World = ACFTransitionTest::FindGameWorld();
	if (GUsingNullRHI || !S || !World || !World->GetGameViewport())
	{
		AddWarning(TEXT("Skipped: needs a rendered game viewport (RunTests.sh --context samples)."));
		return true;
	}
	S->ClearScreen();
	TSharedPtr<FCtx> Ctx = MakeShared<FCtx>();
	Ctx->S = S;
	Ctx->Effects = { 1, 4, 12, 15, 18, 19, 22, 25, 30, 37, 41, 42 };
	TArray<TFunction<bool()>> Steps;
	// Idle: nothing ticks, nothing is drawn.
	Steps.Add([this, Ctx]()
	{
		TestFalse(TEXT("Idle: not tickable"), Ctx->S->IsTickable());
		TestFalse(TEXT("Idle: no overlay"), Ctx->S->IsOverlayAttached());
		return true;
	});
	// GPU: baseline, then each effect frozen at coverage 0.5.
	Steps.Add([this, Ctx]()
	{
		const int32 Warm = 30, Measure = 120;
		++Ctx->Frame;
		if (Ctx->Frame > Warm)
		{
			Ctx->SumGpu += GpuMs();
			++Ctx->Samples;
		}
		if (Ctx->Frame < Warm + Measure)
		{
			return false;
		}
		const double Avg = Ctx->SumGpu / FMath::Max(1, Ctx->Samples);
		if (Ctx->Index < 0)
		{
			Ctx->BaselineMs = Avg;
			Ctx->Lines.Add(FString::Printf(TEXT("GPU baseline (clear screen): %.3f ms"), Avg));
		}
		else
		{
			const int32 Id = Ctx->Effects[Ctx->Index];
			Ctx->Lines.Add(FString::Printf(TEXT("GPU %02d %-22s %.3f ms (+%.3f ms)"), Id, *ACFTransition::GetEffectInfo(ACFTransition::FromId(Id)).Name, Avg, Avg - Ctx->BaselineMs));
			TestTrue(*FString::Printf(TEXT("%02d GPU cost below 8 ms at 720p"), Id), Avg - Ctx->BaselineMs < 8.0);
		}
		Ctx->S->ClearScreen();
		++Ctx->Index;
		Ctx->Frame = 0;
		Ctx->SumGpu = 0.0;
		Ctx->Samples = 0;
		if (Ctx->Index >= Ctx->Effects.Num())
		{
			return true;
		}
		const EACFTransitionEffect Effect = ACFTransition::FromId(Ctx->Effects[Ctx->Index]);
		const double T0 = FPlatformTime::Seconds();
		Ctx->S->ShowStill(ACFTransition::MakeDefaultSettings(Effect), EACFTransitionPhase::Close, 0.5f);
		const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		if (ACFTransition::GetEffectInfo(Effect).bUsesSnapshot)
		{
			Ctx->CaptureMs = FMath::Max(Ctx->CaptureMs, Ms);
			TestTrue(TEXT("Capture made"), Ctx->S->HasCapture());
		}
		return false;
	});
	// CPU: the subsystem tick while a 2 s Spiral Close plays.
	Steps.Add([this, Ctx]()
	{
		Ctx->Handle = Ctx->S->PlayClose(ACFTransitionTest::Settings(EACFTransitionEffect::SpiralClose, 2.f), FACFTransitionOptions());
		return true;
	});
	Steps.Add([this, Ctx]()
	{
		if (Ctx->S->GetActiveHandle() == Ctx->Handle)
		{
			const double T = Ctx->S->GetLastTickSeconds() * 1000.0;
			Ctx->TickSum += T;
			Ctx->TickMax = FMath::Max(Ctx->TickMax, T);
			++Ctx->TickN;
			return false;
		}
		return true;
	});
	Steps.Add([this, Ctx]()
	{
		Ctx->Lines.Add(FString::Printf(TEXT("CPU subsystem tick while playing: avg %.4f ms, max %.4f ms (%d frames)"), Ctx->TickSum / FMath::Max(1, Ctx->TickN), Ctx->TickMax, Ctx->TickN));
		Ctx->Lines.Add(FString::Printf(TEXT("CPU screen capture at the start of a capture-based Close (720p, includes the GPU read back): max %.1f ms"), Ctx->CaptureMs));
		TestTrue(TEXT("Tick average below 0.2 ms"), Ctx->TickSum / FMath::Max(1, Ctx->TickN) < 0.2);
		Ctx->S->ClearScreen();
		TestFalse(TEXT("Cleared: not tickable"), Ctx->S->IsTickable());
		for (const FString& Line : Ctx->Lines)
		{
			AddInfo(Line);
		}
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(ACFTransitionTest::FSteps(this, MoveTemp(Steps), [Ctx]() { if (Ctx->S.IsValid()) { Ctx->S->ClearScreen(); } }, 180.0));
	return true;
}

#endif
