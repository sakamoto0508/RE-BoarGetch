#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFTransitionSettings.h"
#include "ACFTransitionTestUtils.h"
#include "ACFTransitionView.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/IConsoleManager.h"
#include "RHIGlobals.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"

/**
 * Phase 7D: the screen capture of the capture-based effects (README 33 章「キャプチャ」).
 *
 *  - Capture.Profile (rendered: RunTests.sh --context samples): the cost of starting a capture-based Close (04 Cross
 *    Dissolve) with the synchronous read back (bCaptureOnGpu = false, the Phase 7D first version) and with the GPU copy,
 *    under the same conditions (same map, window, effect, 10 rounds each, the first round reported apart). Measured: the
 *    time of the PlayClose call on the game thread, the longest frame of the 12 frames after it minus the median frame
 *    before it, and the frames the Close waited for its capture. The GPU copy is also read back once (test only) and
 *    compared with the read back capture of the same still screen (same colours: the sRGB handling is right).
 *  - Capture.FailureRecovers: with ACF.Transition.Debug.FailCapture 1 every capture fails; a capture-based Close and Open
 *    still play to Finished (procedural look) and nothing stays on screen.
 *  - Capture.CancelWhilePending: Cancel, Interrupt and Skip right after a capture-based Close started (the capture still
 *    pending) leave no pending capture, no capture and no overlay.
 */
namespace ACFTransitionCaptureTest
{
	struct FRound
	{
		double CallMs = 0.0;
		double SpikeMs = 0.0;
		int32 WaitFrames = 0;
		bool bFellBack = false;
		bool bCaptured = false;
		FACFTransitionView::FCaptureTiming Timing;
	};

	struct FCtx
	{
		TWeakObjectPtr<UACFTransitionSubsystem> S;
		int32 Method = 0;           // 0 = read back (before), 1 = GPU copy (after)
		int32 Round = 0;
		int32 Frame = 0;
		double LastTime = 0.0;
		TArray<double> Before;
		double MaxAfter = 0.0;
		FRound Current;
		TArray<FRound> Results[2];
		FACFTransitionHandle Handle;
		bool bOldGpu = true;
		TStrongObjectPtr<UTexture2D> Cpu;
		TSharedPtr<FACFTransitionGpuCapture> Gpu;
	};

	constexpr int32 Rounds = 10;

	static double Median(TArray<double> V)
	{
		if (V.Num() == 0) { return 0.0; }
		V.Sort();
		return V[V.Num() / 2];
	}

	static void SetFailCapture(int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("ACF.Transition.Debug.FailCapture")))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionCaptureProfileTest, "ActionCameraFramework.Transitions.Capture.Profile", ACFTransitionTest::Flags)
bool FACFTransitionCaptureProfileTest::RunTest(const FString&)
{
	using namespace ACFTransitionCaptureTest;
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
	Ctx->bOldGpu = GetDefault<UACFTransitionProjectSettings>()->bCaptureOnGpu;
	TArray<TFunction<bool()>> Steps;

	// Colour check: the GPU copy and the read back of the same still screen.
	Steps.Add([this, Ctx]()
	{
		UWorld* W = ACFTransitionTest::FindGameWorld();
		UGameViewportClient* VC = W ? W->GetGameViewport() : nullptr;
		Ctx->Cpu.Reset(FACFTransitionView::CaptureViewport(VC, 4096));
		Ctx->Gpu = FACFTransitionView::RequestGpuCapture(VC);
		if (!Ctx->Cpu.IsValid() || !Ctx->Gpu.IsValid())
		{
			AddError(TEXT("Could not start both captures."));
		}
		return true;
	});
	Steps.Add([this, Ctx]()
	{
		if (!Ctx->Cpu.IsValid() || !Ctx->Gpu.IsValid())
		{
			return true;
		}
		if (Ctx->Gpu->GetState() == FACFTransitionGpuCapture::EState::Pending && ++Ctx->Frame < 30)
		{
			return false; // the copy happens when the next frame is presented
		}
		Ctx->Frame = 0;
		TestEqual(TEXT("GPU copy ready"), (int32)Ctx->Gpu->GetState(), (int32)FACFTransitionGpuCapture::EState::Ready);
		UTextureRenderTarget2D* RT = Cast<UTextureRenderTarget2D>(Ctx->Gpu->GetTexture());
		TArray<FColor> GpuPixels;
		if (RT && RT->GameThread_GetRenderTargetResource())
		{
			RT->GameThread_GetRenderTargetResource()->ReadPixels(GpuPixels);
		}
		UTexture2D* Cpu = Ctx->Cpu.Get();
		FByteBulkData& Bulk = Cpu->GetPlatformData()->Mips[0].BulkData;
		TArray<FColor> CpuPixels;
		CpuPixels.SetNumZeroed(Cpu->GetSizeX() * Cpu->GetSizeY());
		if (const void* Data = Bulk.LockReadOnly())
		{
			FMemory::Memcpy(CpuPixels.GetData(), Data, FMath::Min<int64>(Bulk.GetBulkDataSize(), CpuPixels.Num() * sizeof(FColor)));
		}
		Bulk.Unlock();
		if (GpuPixels.Num() != CpuPixels.Num() || GpuPixels.Num() == 0)
		{
			AddError(FString::Printf(TEXT("Capture sizes differ: GPU %d px, read back %d x %d."), GpuPixels.Num(), Cpu->GetSizeX(), Cpu->GetSizeY()));
		}
		else
		{
			double Sum = 0.0;
			for (int32 i = 0; i < GpuPixels.Num(); ++i)
			{
				Sum += FMath::Abs(GpuPixels[i].R - CpuPixels[i].R) + FMath::Abs(GpuPixels[i].G - CpuPixels[i].G) + FMath::Abs(GpuPixels[i].B - CpuPixels[i].B);
			}
			const double Mean = Sum / (3.0 * GpuPixels.Num());
			AddInfo(FString::Printf(TEXT("GPU copy vs read back of the same screen (%d x %d): mean difference %.2f / 255"), (int32)RT->SizeX, (int32)RT->SizeY, Mean));
			TestTrue(TEXT("GPU copy has the same colours as the read back (mean difference < 6 / 255)"), Mean < 6.0);
		}
		// Through the material: Cross Dissolve at 30 % drawn over each capture (the 10-bit copy is decoded by the material,
		// the read back texture by the sampler) gives the same picture.
		if (RT)
		{
			const FVector2D Size(320.0, 180.0);
			UTextureRenderTarget2D* Out = FWidgetRenderer::CreateTargetFor(Size, TF_Bilinear, false);
			FWidgetRenderer Renderer(false, true);
			TArray<FColor> Drawn[2];
			for (int32 k = 0; k < 2; ++k)
			{
				FACFTransitionView View;
				View.SetFallbackSize(Size);
				View.Apply(ACFTransition::MakeDefaultSettings(EACFTransitionEffect::CrossDissolve), EACFTransitionPhase::Close, 0.3f, 0.f, k == 0 ? (UTexture*)Cpu : (UTexture*)RT);
				Renderer.DrawWidget(Out, View.GetWidget(), Size, 0.f);
				FlushRenderingCommands();
				FReadSurfaceDataFlags Flags(RCM_UNorm);
				Flags.SetLinearToGamma(false);
				Out->GameThread_GetRenderTargetResource()->ReadPixels(Drawn[k], Flags);
				View.Release();
			}
			double Sum = 0.0;
			for (int32 i = 0; i < Drawn[0].Num() && i < Drawn[1].Num(); ++i)
			{
				Sum += FMath::Abs(Drawn[0][i].R - Drawn[1][i].R) + FMath::Abs(Drawn[0][i].G - Drawn[1][i].G) + FMath::Abs(Drawn[0][i].B - Drawn[1][i].B);
			}
			const double Mean = Drawn[0].Num() > 0 ? Sum / (3.0 * Drawn[0].Num()) : 255.0;
			AddInfo(FString::Printf(TEXT("Cross Dissolve at 30 %% over the GPU copy vs over the read back: mean difference %.2f / 255"), Mean));
			TestTrue(TEXT("The material shows the GPU copy with the read back's colours (mean difference < 4 / 255)"), Mean < 4.0);
			Out->MarkAsGarbage();
		}
		Ctx->Gpu->Cancel();
		if (RT) { RT->MarkAsGarbage(); }
		Cpu->MarkAsGarbage();
		Ctx->Gpu.Reset();
		Ctx->Cpu.Reset();
		return true;
	});

	// Rounds: method 0 (read back) then method 1 (GPU copy), the same effect, map and window.
	Steps.Add([this, Ctx]()
	{
		UACFTransitionSubsystem* Sub = Ctx->S.Get();
		if (!Sub)
		{
			return true;
		}
		const double Now = FPlatformTime::Seconds();
		const double Dt = Ctx->LastTime > 0.0 ? (Now - Ctx->LastTime) * 1000.0 : 0.0;
		Ctx->LastTime = Now;
		++Ctx->Frame;
		const int32 Quiet = 15, After = 12;
		if (Ctx->Frame <= Quiet)
		{
			if (Ctx->Frame > 2) { Ctx->Before.Add(Dt); }
			return false;
		}
		if (Ctx->Frame == Quiet + 1)
		{
			GetMutableDefault<UACFTransitionProjectSettings>()->bCaptureOnGpu = Ctx->Method == 1;
			FACFTransitionSettings Settings = ACFTransitionTest::Settings(EACFTransitionEffect::CrossDissolve, 0.4f);
			const double T0 = FPlatformTime::Seconds();
			Ctx->Handle = Sub->PlayClose(Settings, FACFTransitionOptions());
			Ctx->Current = FRound();
			Ctx->Current.CallMs = (FPlatformTime::Seconds() - T0) * 1000.0;
			Ctx->Current.Timing = FACFTransitionView::GetLastCaptureTiming();
			Ctx->MaxAfter = 0.0;
			Ctx->LastTime = FPlatformTime::Seconds();
			return false;
		}
		Ctx->MaxAfter = FMath::Max(Ctx->MaxAfter, Dt);
		if (!Sub->IsCapturePending() && !Ctx->Current.bCaptured && Sub->HasCapture())
		{
			Ctx->Current.bCaptured = true;
			Ctx->Current.WaitFrames = Sub->GetLastCaptureWaitFrames();
			Ctx->Current.bFellBack = Sub->DidLastCaptureFallBack();
		}
		if (Ctx->Frame < Quiet + 1 + After)
		{
			return false;
		}
		if (Sub->GetActiveHandle() == Ctx->Handle)
		{
			return false; // wait for the Close to finish (covered)
		}
		Ctx->Current.SpikeMs = Ctx->MaxAfter - Median(Ctx->Before);
		Ctx->Results[Ctx->Method].Add(Ctx->Current);
		Sub->ClearScreen();
		Ctx->Before.Reset();
		Ctx->Frame = 0;
		Ctx->LastTime = 0.0;
		if (++Ctx->Round >= Rounds)
		{
			Ctx->Round = 0;
			if (++Ctx->Method >= 2)
			{
				return true;
			}
		}
		return false;
	});

	Steps.Add([this, Ctx]()
	{
		GetMutableDefault<UACFTransitionProjectSettings>()->bCaptureOnGpu = Ctx->bOldGpu;
		const TCHAR* Names[2] = { TEXT("read back (before)"), TEXT("GPU copy (after)") };
		for (int32 M = 0; M < 2; ++M)
		{
			const TArray<FRound>& R = Ctx->Results[M];
			if (R.Num() < 2)
			{
				AddError(TEXT("Not enough rounds."));
				return true;
			}
			double CallSum = 0.0, CallMax = 0.0, SpikeSum = 0.0, SpikeMax = 0.0, Shot = 0.0, Resize = 0.0, Upload = 0.0;
			int32 WaitMax = 0, Captured = 0, FellBack = 0;
			for (int32 i = 1; i < R.Num(); ++i) // round 0 = first use, reported apart
			{
				CallSum += R[i].CallMs; CallMax = FMath::Max(CallMax, R[i].CallMs);
				SpikeSum += R[i].SpikeMs; SpikeMax = FMath::Max(SpikeMax, R[i].SpikeMs);
				Shot += R[i].Timing.ScreenshotMs; Resize += R[i].Timing.ResizeMs; Upload += R[i].Timing.UploadMs;
				WaitMax = FMath::Max(WaitMax, R[i].WaitFrames);
			}
			for (const FRound& X : R) { Captured += X.bCaptured ? 1 : 0; FellBack += X.bFellBack ? 1 : 0; }
			const int32 N = R.Num() - 1;
			AddInfo(FString::Printf(TEXT("Capture %s, %d x %d: first round: call %.2f ms, frame spike %.2f ms; next %d rounds: call avg %.2f / max %.2f ms, frame spike avg %.2f / max %.2f ms, capture wait max %d frames; captured %d / %d, fell back %d"),
				Names[M], R[0].Timing.Width, R[0].Timing.Height, R[0].CallMs, R[0].SpikeMs, N, CallSum / N, CallMax, SpikeSum / N, SpikeMax, WaitMax, Captured, R.Num(), FellBack));
			if (M == 0)
			{
				AddInfo(FString::Printf(TEXT("  read back breakdown (avg): TakeScreenshot (Slate redraw + GPU read back + render flush) %.2f ms, halving %.2f ms, texture create + upload %.2f ms"), Shot / N, Resize / N, Upload / N));
			}
			TestEqual(*FString::Printf(TEXT("%s: every round captured"), Names[M]), Captured, R.Num());
			if (M == 1)
			{
				TestEqual(TEXT("GPU copy: no fall back"), FellBack, 0);
				TestTrue(TEXT("GPU copy: the call stays under 2 ms"), CallMax < 2.0);
				TestTrue(TEXT("GPU copy: ready within MaxCaptureWaitFrames"), WaitMax <= GetDefault<UACFTransitionProjectSettings>()->MaxCaptureWaitFrames);
			}
		}
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(ACFTransitionTest::FSteps(this, MoveTemp(Steps), [Ctx]()
	{
		GetMutableDefault<UACFTransitionProjectSettings>()->bCaptureOnGpu = Ctx->bOldGpu;
		if (Ctx->S.IsValid()) { Ctx->S->ClearScreen(); }
	}, 240.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionCaptureFailureTest, "ActionCameraFramework.Transitions.Capture.FailureRecovers", ACFTransitionTest::Flags)
bool FACFTransitionCaptureFailureTest::RunTest(const FString&)
{
	using namespace ACFTransitionCaptureTest;
	UACFTransitionSubsystem* S = ACFTransitionTest::FindSubsystem();
	if (!S)
	{
		AddError(TEXT("No transition subsystem (needs a game world)."));
		return false;
	}
	S->ClearScreen();
	SetFailCapture(1);
	TSharedPtr<ACFTransitionTest::FRecorder> Rec = MakeShared<ACFTransitionTest::FRecorder>();
	Rec->Bind(S);
	TSharedPtr<FACFTransitionHandle> Close = MakeShared<FACFTransitionHandle>();
	TSharedPtr<FACFTransitionHandle> Open = MakeShared<FACFTransitionHandle>();
	TWeakObjectPtr<UACFTransitionSubsystem> Weak = S;
	TArray<TFunction<bool()>> Steps;
	Steps.Add([=]()
	{
		*Close = Weak->PlayClose(ACFTransitionTest::Settings(EACFTransitionEffect::ZoomRush, 0.25f), FACFTransitionOptions());
		return true;
	});
	Steps.Add([=, this]()
	{
		if (!Rec->Has(*Close, EACFTransitionEvent::Finished) && !Rec->Has(*Close, EACFTransitionEvent::Failed))
		{
			return false;
		}
		TestEqual(TEXT("Close with a failed capture finishes"), Rec->Of(*Close), FString(TEXT("Started,Covered,Finished")));
		TestFalse(TEXT("No capture"), Weak->HasCapture());
		TestFalse(TEXT("No pending capture"), Weak->IsCapturePending());
		TestEqual(TEXT("Covered"), Weak->GetCoverage(), 1.f);
		*Open = Weak->PlayOpen(ACFTransitionTest::Settings(EACFTransitionEffect::CrossDissolve, 0.25f, EACFTransitionPhase::Open), FACFTransitionOptions());
		return true;
	});
	Steps.Add([=, this]()
	{
		if (!Rec->Has(*Open, EACFTransitionEvent::Finished) && !Rec->Has(*Open, EACFTransitionEvent::Failed))
		{
			return false;
		}
		TestEqual(TEXT("Open finishes"), Rec->Of(*Open), FString(TEXT("Started,Finished")));
		TestFalse(TEXT("Nothing on screen after the open"), Weak->IsOverlayAttached());
		TestEqual(TEXT("Clear"), Weak->GetCoverage(), 0.f);
		TestFalse(TEXT("Not tickable"), Weak->IsTickable());
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(ACFTransitionTest::FSteps(this, MoveTemp(Steps), [Weak, Rec]()
	{
		SetFailCapture(0);
		if (Weak.IsValid()) { Weak->ClearScreen(); }
	}, 20.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionCaptureCancelTest, "ActionCameraFramework.Transitions.Capture.CancelWhilePending", ACFTransitionTest::Flags)
bool FACFTransitionCaptureCancelTest::RunTest(const FString&)
{
	UACFTransitionSubsystem* S = ACFTransitionTest::FindSubsystem();
	if (!S)
	{
		AddError(TEXT("No transition subsystem (needs a game world)."));
		return false;
	}
	S->ClearScreen();
	const bool bRendered = !GUsingNullRHI;
	auto CheckClean = [this, S](const TCHAR* What)
	{
		TestFalse(*FString::Printf(TEXT("%s: no pending capture"), What), S->IsCapturePending());
		TestFalse(*FString::Printf(TEXT("%s: no capture"), What), S->HasCapture());
		TestFalse(*FString::Printf(TEXT("%s: no overlay"), What), S->IsOverlayAttached());
		TestFalse(*FString::Printf(TEXT("%s: no view"), What), S->HasView());
	};

	// Cancel in the same frame as the start.
	FACFTransitionHandle H = S->PlayClose(ACFTransitionTest::Settings(EACFTransitionEffect::CrossDissolve, 0.5f), FACFTransitionOptions());
	if (bRendered)
	{
		TestTrue(TEXT("Rendered: the capture is pending right after the start"), S->IsCapturePending() || S->HasCapture());
	}
	TestTrue(TEXT("Cancel"), S->Cancel(H, true));
	CheckClean(TEXT("Cancel"));

	// Interrupt by an Open that does not use the capture.
	H = S->PlayClose(ACFTransitionTest::Settings(EACFTransitionEffect::MosaicTransition, 0.5f), FACFTransitionOptions());
	FACFTransitionHandle O = S->PlayOpen(ACFTransitionTest::Settings(EACFTransitionEffect::BlackFade, 0.05f, EACFTransitionPhase::Open), FACFTransitionOptions());
	TestFalse(TEXT("Interrupt: the capture request is dropped"), S->IsCapturePending());
	for (int32 i = 0; i < 20 && S->GetActiveHandle() == O; ++i)
	{
		S->AdvanceForTest(0.02f);
	}
	CheckClean(TEXT("Interrupt then Open finished"));

	// Skip: the Close jumps to covered; ClearScreen afterwards leaves nothing.
	H = S->PlayClose(ACFTransitionTest::Settings(EACFTransitionEffect::ZoomRush, 0.5f), FACFTransitionOptions());
	TestTrue(TEXT("Skip"), S->Skip(H));
	for (int32 i = 0; i < 10 && S->GetActiveHandle() == H; ++i)
	{
		S->AdvanceForTest(0.02f);
	}
	S->ClearScreen();
	CheckClean(TEXT("Skip then ClearScreen"));
	return true;
}

#endif
