// Phase 7A-4: the Profiler / Debugger / Composition views of SACFCameraTuningPanel (README 27 章). The data comes from
// FACFCameraDiagnosticsModel; this file only lays it out. Each view is a block of monospace text rebuilt by the panel's
// timer (4 Hz) only while that view is visible.

#include "SACFCameraTuningPanel.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ACFCameraTuningPanel"

namespace ACFTuningPanelDiagnosticsView
{
	TSharedRef<SWidget> MonoText(TFunction<FString()> Text)
	{
		return SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
			.Text_Lambda([Text = MoveTemp(Text)]() { return FText::FromString(Text()); });
	}

	FString Stats(const FACFCameraProfileStats& S)
	{
		return S.Samples > 0 ? FString::Printf(TEXT("mean %8.2f  P95 %8.2f  max %8.2f  n %5d"), S.Mean, S.P95, S.Max, S.Samples) : FString(TEXT("-"));
	}
}

void SACFCameraTuningPanel::SetActiveView(EView View)
{
	ActiveView = View;
	bProfileDirty = true;
	Refresh();
}

TSharedRef<SWidget> SACFCameraTuningPanel::MakeProfilerView()
{
	using namespace ACFTuningPanelDiagnosticsView;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton).Text(LOCTEXT("StartProfiling", "Start Profiling"))
				.ToolTipText(LOCTEXT("StartProfilingTip", "Records this player's camera with the B-1 profiler (ring buffer, from the next frame). Recording adds a small measuring cost."))
				.IsEnabled_Lambda([this]() { return Model->ResolveDirector() != nullptr && !Diagnostics->GetProfile().bForcedByPanel; })
				.OnClicked(this, &SACFCameraTuningPanel::OnStartProfiling)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton).Text(LOCTEXT("StopProfiling", "Stop Profiling"))
				.ToolTipText(LOCTEXT("StopProfilingTip", "Stops recording (the last statistics stay on screen)."))
				.IsEnabled_Lambda([this]() { return Diagnostics->GetProfile().bForcedByPanel; })
				.OnClicked(this, &SACFCameraTuningPanel::OnStopProfiling)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton).Text(LOCTEXT("ResetStatistics", "Reset Statistics"))
				.ToolTipText(LOCTEXT("ResetStatisticsTip", "Drops the recorded frames: the statistics start again from the next frame (e.g. after changing a value)."))
				.IsEnabled_Lambda([this]() { return Diagnostics->IsProfiling(); })
				.OnClicked(this, &SACFCameraTuningPanel::OnResetStatistics)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton).Text(LOCTEXT("MarkA", "Mark A (compare)"))
				.ToolTipText(LOCTEXT("MarkATip", "Remembers the camera pose and the current statistics; the views then show A against now."))
				.IsEnabled_Lambda([this]() { return Model->ResolveDirector() != nullptr; })
				.OnClicked(this, &SACFCameraTuningPanel::OnCaptureBaseline)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 2.f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return Diagnostics->IsPaused() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Diagnostics->SetPaused(State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock).Text(LOCTEXT("PauseDisplay", "Pause display"))
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[ MonoText([this]() { return ProfilerText; }) ]
		];
}

TSharedRef<SWidget> SACFCameraTuningPanel::MakeDebuggerView()
{
	using namespace ACFTuningPanelDiagnosticsView;
	return SNew(SScrollBox) + SScrollBox::Slot()[ MonoText([this]() { return DebuggerText; }) ];
}

TSharedRef<SWidget> SACFCameraTuningPanel::MakeCompositionView()
{
	using namespace ACFTuningPanelDiagnosticsView;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
		[
			SNew(SCheckBox)
			.ToolTipText(LOCTEXT("GuidesTip", "Draws the center, rule of thirds, safe areas (90 / 80 %) and the projected targets on the selected player's PIE view (editor only, never in a packaged game)."))
			.IsChecked_Lambda([this]() { return Diagnostics->AreGuidesVisible() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Diagnostics->SetGuidesVisible(State == ECheckBoxState::Checked); })
			[
				SNew(STextBlock).Text(LOCTEXT("ShowGuides", "Draw composition guides in the PIE viewport"))
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[ MonoText([this]() { return CompositionText; }) ]
		];
}

FReply SACFCameraTuningPanel::OnStartProfiling()
{
	Diagnostics->StartProfiling();
	bProfileDirty = true;
	Refresh();
	return FReply::Handled();
}

FReply SACFCameraTuningPanel::OnStopProfiling()
{
	Diagnostics->StopProfiling();
	bProfileDirty = true;
	Refresh();
	return FReply::Handled();
}

FReply SACFCameraTuningPanel::OnResetStatistics()
{
	Diagnostics->ResetStatistics();
	bProfileDirty = true;
	Refresh();
	return FReply::Handled();
}

FReply SACFCameraTuningPanel::OnCaptureBaseline()
{
	Diagnostics->UpdateProfile();
	Diagnostics->CaptureBaseline();
	bProfileDirty = true;
	Refresh();
	return FReply::Handled();
}

void SACFCameraTuningPanel::RefreshDiagnostics()
{
	using namespace ACFTuningPanelDiagnosticsView;
	if (ActiveView == EView::Tuning)
	{
		return; // nothing to compute for the tuning view
	}
	++NumDiagnosticsRefreshes;
	TArray<FString> Lines;
	if (ActiveView == EView::Profiler)
	{
		// The statistics are recomputed once per second (Summarize sorts the whole ring buffer); right away after a button.
		const double Now = FPlatformTime::Seconds();
		if (bProfileDirty || Now - LastProfileUpdate >= 1.0)
		{
			Diagnostics->UpdateProfile();
			LastProfileUpdate = Now;
			bProfileDirty = false;
			++NumProfileComputations;
		}
		const FACFCameraDiagnosticsModel::FProfileView& P = Diagnostics->GetProfile();
		Lines.Add(FString::Printf(TEXT("Target: %s   %s%s"), P.Target.IsEmpty() ? TEXT("-") : *P.Target, P.bRecording ? TEXT("RECORDING") : TEXT("not recording"),
			Diagnostics->IsPaused() ? TEXT("  (display paused)") : TEXT("")));
		if (!P.bValid)
		{
			Lines.Add(TEXT("No frames recorded yet. Start Profiling records the selected player's camera (B-1 profiler); ACF.Profile 1 records every player."));
		}
		else
		{
			const FACFCameraProfileSummary& S = P.Summary;
			Lines.Add(FString::Printf(TEXT("Period: %d frame(s), engine frames %llu..%llu, %.2f s of game time (ring buffer of %d frames: older frames drop out)"),
				S.NumFrames, P.FirstFrame, P.LastFrame, P.Seconds, P.Capacity));
			Lines.Add(FString::Printf(TEXT("        %d evaluation(s), %d background frame(s) (an external view target was on screen)"), S.NumEvaluations, S.NumBackgroundFrames));
			if (P.bMixesTuningChange)
			{
				Lines.Add(FString::Printf(TEXT("NOTE: a tuning value changed at frame %llu, inside this period: the statistics mix before and after. Reset Statistics to measure only after."), P.TuningChangeFrame));
			}
			Lines.Add(TEXT(""));
			Lines.Add(TEXT("ACF time per frame (us)"));
			Lines.Add(FString::Printf(TEXT("  Total camera evaluation  %s"), *Stats(S.TotalUs)));
			for (int32 Section = 0; Section < FACFCameraProfileFrame::NumSections; ++Section)
			{
				Lines.Add(FString::Printf(TEXT("  %-24s %s"), LexToString(static_cast<EACFCameraProfileSection>(Section)), *Stats(S.SectionUs[Section])));
			}
			Lines.Add(TEXT(""));
			Lines.Add(TEXT("Nodes (us per frame where the node ran; share of the ACF time)"));
			for (const FACFCameraDiagnosticsModel::FNodeRow& Node : P.Nodes)
			{
				Lines.Add(FString::Printf(TEXT("  %-32s %s  %5.1f %%"), *Node.NodeClass.ToString(), *Stats(Node.Us), Node.Share * 100.0));
			}
			if (S.DroppedNodeSamples > 0)
			{
				Lines.Add(FString::Printf(TEXT("  (%d node run(s) not recorded: more than %d in one frame)"), S.DroppedNodeSamples, FACFCameraProfileFrame::MaxNodeSamples));
			}
			Lines.Add(TEXT(""));
			Lines.Add(TEXT("Collision queries per frame"));
			Lines.Add(FString::Printf(TEXT("  all                      %s"), *Stats(S.QueriesPerFrame)));
			Lines.Add(FString::Printf(TEXT("  sweeps                   %s"), *Stats(S.QueriesByShape[static_cast<int32>(EACFCameraQueryShape::Sweep)])));
			Lines.Add(FString::Printf(TEXT("  line traces              %s"), *Stats(S.QueriesByShape[static_cast<int32>(EACFCameraQueryShape::Line)])));
			for (int32 Purpose = 0; Purpose < FACFCameraProfileFrame::NumPurposes; ++Purpose)
			{
				Lines.Add(FString::Printf(TEXT("  %-24s %s"), LexToString(static_cast<EACFCameraQueryPurpose>(Purpose)), *Stats(S.QueriesByPurpose[Purpose])));
			}
			Lines.Add(TEXT(""));
			Lines.Add(P.bAllocationsCounted
				? FString::Printf(TEXT("Allocations: %llu (%llu bytes) in %d frame(s)"), S.Allocations, S.AllocatedBytes, S.FramesWithAllocations)
				: FString(TEXT("Allocations: not measured (the allocation counter is off: ACF.Profile.Allocations 1; it slows everything down)")));
			Lines.Add(FString::Printf(TEXT("Heaviest: %s"), *P.Hotspot));
		}
	}
	else if (ActiveView == EView::Debugger)
	{
		const FACFCameraDiagnosticsModel::FDebugView Debug = Diagnostics->ReadDebug();
		if (!Debug.bValid)
		{
			Lines.Add(TEXT("No target: start PIE and select a world / player above."));
		}
		FString Section;
		for (const FACFCameraDiagnosticsModel::FRow& Row : Debug.Rows)
		{
			if (Row.Section != Section)
			{
				Section = Row.Section;
				Lines.Add(FString::Printf(TEXT("[%s]"), *Section));
			}
			Lines.Add(FString::Printf(TEXT("  %-22s %s"), *Row.Label, *Row.Value));
		}
	}
	else
	{
		const FACFCameraDiagnosticsModel::FCompositionView C = Diagnostics->ReadComposition();
		if (!C.bValid)
		{
			Lines.Add(FString::Printf(TEXT("Not available: %s."), *C.Reason));
		}
		else
		{
			Lines.Add(FString::Printf(TEXT("On screen: %s"), *C.OutputState));
			Lines.Add(FString::Printf(TEXT("View target: %s"), *C.ViewTarget));
			Lines.Add(FString::Printf(TEXT("Player view: %d x %d at (%d, %d) in a %d x %d viewport, view aspect %.4f, projected with aspect %.4f%s, FOV %.2f"), C.ViewRect.Width(),
				C.ViewRect.Height(), C.ViewRect.Min.X, C.ViewRect.Min.Y, C.ViewportSize.X, C.ViewportSize.Y, C.AspectRatio, C.ProjectionAspect,
				FMath::IsNearlyEqual(C.AspectRatio, C.ProjectionAspect, 1e-3f) ? TEXT("") : TEXT(" (the camera constrains the aspect)"), C.Fov));
			Lines.Add(FString::Printf(TEXT("Rendered horizontal FOV %.3f (camera manager FOV %.3f)"), C.ProjectionFov, C.Fov));
			Lines.Add(TEXT(""));
			Lines.Add(TEXT("Targets projected with the rendered view (NDC -1..1, pixel in the player's view)"));
			for (const FACFCameraDiagnosticsModel::FProjectedPoint& Point : C.Points)
			{
				Lines.Add(FString::Printf(TEXT("  %-18s %s  ndc (%6.3f, %6.3f)  px (%6.0f, %6.0f)"), *Point.Label,
					!Point.bInFront ? TEXT("BEHIND   ") : Point.bOnScreen ? TEXT("on screen") : TEXT("OFF SCREEN"), Point.Ndc.X, Point.Ndc.Y, Point.Pixel.X, Point.Pixel.Y));
			}
			Lines.Add(FString::Printf(TEXT("Guides drawn: %d time(s), last canvas %d x %d"), Diagnostics->GetNumGuideDraws(), Diagnostics->GetLastGuideCanvasSize().X, Diagnostics->GetLastGuideCanvasSize().Y));
		}
	}
	const TArray<FString> Comparison = Diagnostics->DescribeComparison();
	if (Comparison.Num() > 0 && ActiveView != EView::Composition)
	{
		Lines.Add(TEXT(""));
		Lines.Add(TEXT("[Compare A / now]"));
		Lines.Append(Comparison);
	}
	FString& Out = ActiveView == EView::Profiler ? ProfilerText : ActiveView == EView::Debugger ? DebuggerText : CompositionText;
	Out = FString::Join(Lines, TEXT("\n"));
}

#undef LOCTEXT_NAMESPACE
