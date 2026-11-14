#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraDiagnosticsModel.h"
#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSubsystem.h"
#include "ACFCameraTuningPanelModel.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Nodes/ACFCameraFraming.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "SACFCameraTuningPanel.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"

/** Phase 7A-4: the Profiler / Debugger / Composition views of the tuning panel (README 27 章). */
namespace ACFDiagnosticsTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	using FDiag = FACFCameraDiagnosticsModel;

	/** Follow (offset) + Collision: one sweep per evaluation when a World is given. */
	inline UACFCameraPreset* MakePreset(float BlendIn = 0.f)
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		if (BlendIn > 0.f)
		{
			Preset->DefaultBlendIn.Curve = EACFCameraBlendCurve::Linear;
			Preset->DefaultBlendIn.Duration = BlendIn;
		}
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		UACFCameraNode_Follow* Follow = NewObject<UACFCameraNode_Follow>(Preset);
		Follow->Offset = FVector(-300.0, 0.0, 150.0);
		Preset->Nodes.Add(Follow);
		Preset->Nodes.Add(NewObject<UACFCameraNode_Collision>(Preset));
		return Preset;
	}

	/** A standalone director (a "player") stepped with the editor World, so its Collision really sweeps. */
	struct FPlayer
	{
		TStrongObjectPtr<UACFCameraDirector> Director;
		TStrongObjectPtr<USceneComponent> Target;
		TStrongObjectPtr<UACFCameraPreset> Preset;
		FACFCameraRequestHandle Handle;
		FACFCameraTuningPanelModel Model;
		uint64 Frame = 0;

		explicit FPlayer(uint64 FirstFrame) : Frame(FirstFrame)
		{
			Director.Reset(NewObject<UACFCameraDirector>());
			Target.Reset(NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient));
			Target->SetMobility(EComponentMobility::Movable);
			Target->SetWorldLocation(FVector(0.0, 0.0, 20000.0)); // far above the editor level: sweeps without hits
			Preset.Reset(MakePreset());
			FACFCameraRequestParams Params;
			Params.FollowTarget.Component = Target.Get();
			Params.DebugName = TEXT("Gameplay");
			Handle = Director->AddRequest(Preset.Get(), Params);
			Model.SetDirectorForTesting(Director.Get());
			Model.SelectRequest(Handle);
		}

		void Step(int32 Frames)
		{
			for (int32 I = 0; I < Frames; ++I)
			{
				FACFCameraEvaluationContext Context;
				Context.DeltaTime = 1.f / 60.f;
				Context.FrameNumber = ++Frame;
				Context.World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
				Context.UpstreamPose.FieldOfView = 90.f;
				Director->Evaluate(Context);
			}
		}

		~FPlayer()
		{
			Director->SetProfilerForced(false);
		}
	};
}

// 2, 3, 4, 5, 6: the statistics of the right player, node times, sweep counts, Start / Stop / Reset, mixed periods.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDiagnosticsProfilerTest, "ActionCameraFramework.Editor.Diagnostics.ProfilerStartStopResetAndPlayers", ACFDiagnosticsTests::Flags)
bool FACFDiagnosticsProfilerTest::RunTest(const FString& Parameters)
{
	using namespace ACFDiagnosticsTests;
	FPlayer A(100000);
	FPlayer B(900000);
	TUniquePtr<FDiag> DiagA = MakeUnique<FDiag>(A.Model);
	FDiag DiagB(B.Model);
	TestFalse(TEXT("Before Start: not recording"), DiagA->IsProfiling());
	TestFalse(TEXT("Before Start: no statistics"), DiagA->UpdateProfile().bValid);

	// 5. Start.
	TestTrue(TEXT("5. Start A"), DiagA->StartProfiling());
	TestTrue(TEXT("Start B"), DiagB.StartProfiling());
	A.Step(31);
	B.Step(57);
	const FDiag::FProfileView& PA = DiagA->UpdateProfile();
	const FACFCameraProfiler& RecorderA = A.Director->GetProfiler();
	TestTrue(TEXT("5. A recording"), PA.bRecording && PA.bForcedByPanel);
	TestEqual(TEXT("1. The view shows exactly A's ring buffer"), PA.Summary.NumFrames, RecorderA.Num());
	TestTrue(TEXT("2. Only A's frames (A's frame numbers)"), PA.FirstFrame > 100000 && PA.LastFrame <= 100031);
	TestTrue(TEXT("2. A and B differ (A 30 or 31 frames, B 56 or 57)"), PA.Summary.NumFrames >= 30 && PA.Summary.NumFrames <= 31
		&& DiagB.UpdateProfile().Summary.NumFrames >= 56 && DiagB.GetProfile().FirstFrame > 900000);

	// 3. Node CPU time: one row per node class of the preset, measured every frame.
	TArray<FName> Classes;
	for (const FDiag::FNodeRow& Row : PA.Nodes)
	{
		Classes.Add(Row.NodeClass);
		TestEqual(FString::Printf(TEXT("3. %s measured in every frame"), *Row.NodeClass.ToString()), Row.Us.Samples, PA.Summary.NumFrames);
		TestTrue(FString::Printf(TEXT("3. %s has a positive time"), *Row.NodeClass.ToString()), Row.Us.Mean > 0.0);
	}
	TestTrue(TEXT("3. Follow and Collision listed"), Classes.Num() == 2 && Classes.Contains(UACFCameraNode_Follow::StaticClass()->GetFName())
		&& Classes.Contains(UACFCameraNode_Collision::StaticClass()->GetFName()));
	TestTrue(TEXT("3. Sorted by time, highest first"), PA.Nodes.Num() == 2 && PA.Nodes[0].Us.Mean >= PA.Nodes[1].Us.Mean);
	TestFalse(TEXT("The heaviest section / node is named"), PA.Hotspot.IsEmpty());

	// 4. Queries: the Collision node sweeps once per evaluation, nothing else queries (no PCM: no output guard).
	const FACFCameraProfileStats& Sweeps = PA.Summary.QueriesByShape[static_cast<int32>(EACFCameraQueryShape::Sweep)];
	TestEqual(TEXT("4. One sweep per frame (mean)"), Sweeps.Mean, 1.0);
	TestEqual(TEXT("4. One sweep per frame (max)"), Sweeps.Max, 1.0);
	TestEqual(TEXT("4. No line trace"), PA.Summary.QueriesByShape[static_cast<int32>(EACFCameraQueryShape::Line)].Max, 0.0);
	TestEqual(TEXT("4. All by the Collision node"), PA.Summary.QueriesByPurpose[static_cast<int32>(EACFCameraQueryPurpose::Collision)].Mean, 1.0);
	TestEqual(TEXT("4. No output guard"), PA.Summary.QueriesByPurpose[static_cast<int32>(EACFCameraQueryPurpose::OutputGuard)].Max, 0.0);
	TestEqual(TEXT("Total = the B-1 summary of the same frames"), PA.Summary.TotalUs.Samples, PA.Summary.NumFrames);
	TestTrue(TEXT("The period has game time"), FMath::IsNearlyEqual(PA.Seconds, PA.Summary.NumFrames / 60.0, 1e-3));

	// A tuning change inside the period is flagged; Reset measures again (6).
	DiagA->NotifyTuningChanged();
	A.Step(5);
	TestTrue(TEXT("Mixed period flagged"), DiagA->UpdateProfile().bMixesTuningChange);
	DiagA->ResetStatistics();
	TestFalse(TEXT("6. Reset: no frames"), DiagA->UpdateProfile().bValid);
	TestFalse(TEXT("6. Reset: no mixing flag"), DiagA->GetProfile().bMixesTuningChange);
	A.Step(7);
	TestEqual(TEXT("6. After Reset: only the new frames"), DiagA->UpdateProfile().Summary.NumFrames, 7);
	TestTrue(TEXT("6. Still recording"), DiagA->IsProfiling());

	// Pause: the display freezes, the recording does not.
	DiagA->SetPaused(true);
	A.Step(4);
	TestEqual(TEXT("Paused: display frozen"), DiagA->UpdateProfile().Summary.NumFrames, 7);
	TestEqual(TEXT("Paused: the profiler kept recording"), A.Director->GetProfiler().Num(), 11);
	DiagA->SetPaused(false);

	// 5. Stop: the last statistics stay, the director stops recording at its next evaluation.
	DiagA->StopProfiling();
	TestEqual(TEXT("5. Stop: the last statistics stay"), DiagA->GetProfile().Summary.NumFrames, 11);
	A.Step(1);
	TestFalse(TEXT("5. Stop: A no longer records"), A.Director->GetProfiler().IsRecording());
	TestTrue(TEXT("Stop of A does not touch B"), B.Director->GetProfiler().IsRecording());

	// Closing the panel (destroying the model) gives the profiler back.
	DiagA->StartProfiling();
	A.Step(2);
	DiagA.Reset();
	A.Step(1);
	TestFalse(TEXT("Panel closed: A's profiler released"), A.Director->GetProfiler().IsRecording());
	return true;
}

// 7, 8, 10: requests, blend, health, pose in the debugger view.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDiagnosticsDebuggerTest, "ActionCameraFramework.Editor.Diagnostics.DebuggerRequestsBlendHealth", ACFDiagnosticsTests::Flags)
bool FACFDiagnosticsDebuggerTest::RunTest(const FString& Parameters)
{
	using namespace ACFDiagnosticsTests;
	FPlayer A(200000);
	FDiag Diag(A.Model);
	A.Step(2);
	FDiag::FDebugView View = Diag.ReadDebug();
	TestTrue(TEXT("Valid"), View.bValid);
	TestEqual(TEXT("10. Pose = the director's last result"), View.Result.Pose.Location, A.Director->GetLastResult().Pose.Location);
	TestEqual(TEXT("10. FOV = the director's last result"), View.Result.Pose.FieldOfView, A.Director->GetLastResult().Pose.FieldOfView);
	const FDiag::FRow* Active = FDiag::FindRow(View, TEXT("Active request"));
	TestTrue(TEXT("Active request row names the request"), Active && Active->Value.Contains(A.Handle.ToString()) && Active->Value.Contains(TEXT("Gameplay")));
	TestFalse(TEXT("Not blending"), View.Transition.bActive);
	TestTrue(TEXT("No PCM: the screen is reported as unknown, not as ACF"), !View.bAcfOnScreen && View.OutputState.Contains(TEXT("not known")));

	// 8. Switch to a higher request with a 0.5 s blend: the view follows.
	TStrongObjectPtr<UACFCameraPreset> Second(MakePreset(0.5f));
	FACFCameraRequestParams Params;
	Params.FollowTarget.Component = A.Target.Get();
	Params.Priority = 5;
	Params.DebugName = TEXT("LockOnLike");
	const FACFCameraRequestHandle HandleB = A.Director->AddRequest(Second.Get(), Params);
	A.Step(6);
	View = Diag.ReadDebug();
	TestEqual(TEXT("8. Active request switched"), View.Result.ActiveRequest, HandleB);
	TestTrue(TEXT("8. Priority row"), FDiag::FindRow(View, TEXT("Priority")) && FDiag::FindRow(View, TEXT("Priority"))->Value == TEXT("5"));
	TestTrue(TEXT("8. Blending"), View.Transition.bActive && View.Result.bIsBlending);
	TestTrue(TEXT("8. Blend duration 0.5 s, about 0.1 s in"), FMath::IsNearlyEqual(View.Transition.Duration, 0.5f) && FMath::IsNearlyEqual(View.Transition.Elapsed, 6.f / 60.f, 1e-3f));
	TestTrue(TEXT("8. Remaining time shown"), FDiag::FindRow(View, TEXT("Blend")) && FDiag::FindRow(View, TEXT("Blend"))->Value.Contains(TEXT("s left")));
	A.Step(40);
	View = Diag.ReadDebug();
	TestFalse(TEXT("8. Blend finished"), View.Transition.bActive);
	A.Director->RemoveRequest(HandleB, nullptr, false);
	A.Step(2);
	TestEqual(TEXT("8. Back to the first request"), Diag.ReadDebug().Result.ActiveRequest, A.Handle);

	// 7. Health.
	AddExpectedMessagePlain(TEXT("ACF7A4HealthProbe"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	A.Director->GetHealth().Report(EACFCameraHealthIssue::InvalidTarget, NAME_None, TEXT("ACF7A4HealthProbe"), TEXT("none"));
	View = Diag.ReadDebug();
	TestEqual(TEXT("7. Health count = the director's diagnostics"), View.Health.TotalIssues, A.Director->GetHealthDiagnostics().TotalIssues);
	TestTrue(TEXT("7. Health row shows the count"), View.Health.TotalIssues > 0 && FDiag::FindRow(View, TEXT("Health issues"))->Value.StartsWith(FString::FromInt(View.Health.TotalIssues)));
	TestTrue(TEXT("7. The last issue is shown"), FDiag::FindRow(View, TEXT("Last issue"))->Value.Contains(TEXT("ACF7A4HealthProbe")));
	return true;
}

// Phase 7B-1: the assistance rows show the state the node really has (mode, state, heading, target yaw, speed, waits,
// suppression, reason), formatted by the panel from the plain values the node published.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDiagnosticsAssistanceTest, "ActionCameraFramework.Editor.Diagnostics.DebuggerShowsAssistance", ACFDiagnosticsTests::Flags)
bool FACFDiagnosticsAssistanceTest::RunTest(const FString& Parameters)
{
	using namespace ACFDiagnosticsTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient));
	Target->SetMobility(EComponentMobility::Movable);
	TStrongObjectPtr<UACFCameraPreset> Preset(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
	Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
	Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
	UACFCameraNode_OrbitalFollow* Orbit = NewObject<UACFCameraNode_OrbitalFollow>(Preset.Get());
	Orbit->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
	Orbit->InitialYaw = 0.f;
	Orbit->bEnableMovementAssistance = true;
	Orbit->RecenterHeading = EACFCameraRecenterHeading::MovementDirection;
	Orbit->RecenterDelay = 0.3f;
	Preset->Nodes.Add(Orbit);
	FACFCameraRequestParams Params;
	Params.FollowTarget.Component = Target.Get();
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), Params);
	FACFCameraTuningPanelModel Model;
	Model.SetDirectorForTesting(Director.Get());
	Model.SelectRequest(Handle);
	FDiag Diag(Model);
	uint64 Frame = 300000;
	FVector Velocity = FVector::ZeroVector;
	auto Step = [&](int32 Frames)
	{
		for (int32 I = 0; I < Frames; ++I)
		{
			Target->SetWorldLocation(Target->GetComponentLocation() + Velocity / 60.0);
			FACFCameraEvaluationContext Context;
			Context.DeltaTime = 1.f / 60.f;
			Context.FrameNumber = ++Frame;
			Context.UpstreamPose.FieldOfView = 90.f;
			Director->Evaluate(Context);
		}
	};
	auto Row = [](const FDiag::FDebugView& View, const TCHAR* Label) { const FDiag::FRow* R = FDiag::FindRow(View, Label); return R ? R->Value : FString(TEXT("<missing>")); };
	auto Display = [](const UEnum* Enum, int64 Value) { return Enum->GetDisplayNameTextByValue(Value).ToString(); };
	auto Matches = [&](const TCHAR* When)
	{
		const FDiag::FDebugView View = Diag.ReadDebug();
		const FACFCameraRigDebug& D = Director->GetLastDebug();
		TestEqual(FString::Printf(TEXT("%s: state row = the node's state"), When), Row(View, TEXT("Assistance state")), Display(StaticEnum<EACFCameraAssistanceState>(), static_cast<int64>(D.AssistanceState)));
		TestEqual(FString::Printf(TEXT("%s: reason row = the node's reason"), When), Row(View, TEXT("Not assisting because")),
			D.AssistanceReason == EACFCameraAssistanceReason::None ? FString(TEXT("-")) : Display(StaticEnum<EACFCameraAssistanceReason>(), static_cast<int64>(D.AssistanceReason)));
		TestTrue(FString::Printf(TEXT("%s: speed row = the node's speed (%s)"), When, *Row(View, TEXT("Movement speed"))), Row(View, TEXT("Movement speed")).StartsWith(FString::Printf(TEXT("%.1f cm/s"), D.AssistanceMovementSpeed)));
		TestTrue(FString::Printf(TEXT("%s: waiting row (%s)"), When, *Row(View, TEXT("Waiting time"))), Row(View, TEXT("Waiting time")) == FString::Printf(TEXT("%.2f / %.2f s"), D.AssistanceWaitTime, 0.3f));
		return View;
	};

	Step(5);
	FDiag::FDebugView View = Matches(TEXT("Standing"));
	TestEqual(TEXT("Mode row"), Row(View, TEXT("Assistance mode")), FString(TEXT("Movement Assistance")));
	TestEqual(TEXT("Heading row"), Row(View, TEXT("Heading source")), Display(StaticEnum<EACFCameraRecenterHeading>(), static_cast<int64>(EACFCameraRecenterHeading::MovementDirection)));
	TestTrue(TEXT("Standing: Waiting"), Director->GetLastDebug().AssistanceState == EACFCameraAssistanceState::Waiting);
	TestEqual(TEXT("Standing: no target yaw"), Row(View, TEXT("Target yaw")), FString(TEXT("- (not moving)")));

	Velocity = FVector(0.0, 400.0, 0.0);
	Step(90);
	View = Matches(TEXT("Moving"));
	TestTrue(TEXT("Moving: Assisting"), Director->GetLastDebug().AssistanceState == EACFCameraAssistanceState::Assisting);
	TestEqual(TEXT("Moving: target yaw row = the movement direction"), Row(View, TEXT("Target yaw")), FString(TEXT("90.00 deg")));
	TestEqual(TEXT("Not suppressed"), Row(View, TEXT("Assistance suppression")), FString(TEXT("not suppressed")));

	// Phase 7B-2: the input activity row (deg/s against Input Activity Threshold, default 5).
	Director->SetCameraInputRate(FVector2D(2.0, 0.0));
	Step(1);
	View = Matches(TEXT("Drift"));
	TestEqual(TEXT("Drift: below threshold, ignored"), Row(View, TEXT("Input activity")), FString(TEXT("2.0 / 5.0 deg/s (below threshold: ignored)")));
	TestTrue(TEXT("Drift: still assisting"), Director->GetLastDebug().AssistanceState == EACFCameraAssistanceState::Assisting);
	Director->SetCameraInputRate(FVector2D(12.0, 0.0));
	Step(1);
	View = Matches(TEXT("Stick"));
	TestEqual(TEXT("Stick: manual input"), Row(View, TEXT("Input activity")), FString(TEXT("12.0 / 5.0 deg/s (manual input)")));
	TestTrue(TEXT("Stick: Manual Input reason"), Director->GetLastDebug().AssistanceReason == EACFCameraAssistanceReason::ManualInput);
	Step(30); // back to assisting after Recenter Delay (0.3 s)

	TStrongObjectPtr<USceneComponent> Source(NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient));
	Director->SuppressAssistance(Source.Get());
	Step(3);
	View = Matches(TEXT("Suppressed"));
	TestTrue(TEXT("Suppressed: state"), Director->GetLastDebug().AssistanceState == EACFCameraAssistanceState::Suppressed);
	TestEqual(TEXT("Suppression row"), Row(View, TEXT("Assistance suppression")), FString(TEXT("suppressed by 1 source(s)")));
	Director->ReleaseAssistanceSuppression(Source.Get());

	// A camera without recenter: one row says so.
	Orbit->bEnableMovementAssistance = false;
	Step(1);
	TestTrue(TEXT("No recenter: one 'none' row"), Row(Diag.ReadDebug(), TEXT("Assistance mode")).StartsWith(TEXT("none")));
	return true;
}

// 11, 12: the projection the guides use, against ACF's own pinhole projection, for several viewport sizes / aspects.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDiagnosticsProjectionTest, "ActionCameraFramework.Editor.Diagnostics.CompositionProjection", ACFDiagnosticsTests::Flags)
bool FACFDiagnosticsProjectionTest::RunTest(const FString& Parameters)
{
	using namespace ACFDiagnosticsTests;
	const FVector Location(100.0, -250.0, 180.0);
	const FRotator Rotation(-12.0, 35.0, 0.0);
	const float Fov = 75.f;
	const FVector Points[] = { Location + Rotation.Vector() * 800.0, Location + Rotation.RotateVector(FVector(600.0, 150.0, -80.0)),
		Location + Rotation.RotateVector(FVector(400.0, -380.0, 120.0)), Location + Rotation.RotateVector(FVector(300.0, 900.0, 0.0)),
		Location + Rotation.RotateVector(FVector(-200.0, 0.0, 0.0)) };
	const FIntPoint Sizes[] = { FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(1024, 768), FIntPoint(640, 720) }; // 16:9, 16:9, 4:3, split screen half
	for (const FIntPoint& Size : Sizes)
	{
		// The renderer's matrices for a horizontal FOV (ULocalPlayer: MaintainXFOV).
		const float Aspect = static_cast<float>(Size.X) / Size.Y;
		const float HalfFov = FMath::DegreesToRadians(Fov) * 0.5f;
		const FMatrix ViewRotation = FInverseRotationMatrix(Rotation) * FMatrix(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
		const FMatrix Projection = FReversedZPerspectiveMatrix(HalfFov, HalfFov, 1.f, Aspect, GNearClippingPlane, GNearClippingPlane);
		const FMatrix ViewProjection = FTranslationMatrix(-Location) * ViewRotation * Projection;
		const FIntRect Rect(0, 0, Size.X, Size.Y);
		const ACFFraming::FLens Lens = ACFFraming::MakeLens(Fov, Aspect);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Points); ++Index)
		{
			FVector2D Pixel, Ndc, Screen;
			double Depth = 0.0;
			const bool bFront = FDiag::ProjectPoint(ViewProjection, Rect, Points[Index], Pixel, Ndc);
			const bool bRefFront = ACFFraming::Project(Location, Rotation, Lens, Points[Index], Screen, Depth);
			const FString Case = FString::Printf(TEXT("%dx%d point %d"), Size.X, Size.Y, Index);
			TestEqual(Case + TEXT(": in front / behind"), bFront, bRefFront);
			if (bFront && bRefFront)
			{
				TestTrue(FString::Printf(TEXT("11. %s: NDC (%.4f, %.4f) = 2 x ACF screen (%.4f, %.4f)"), *Case, Ndc.X, Ndc.Y, Screen.X, Screen.Y),
					FMath::IsNearlyEqual(Ndc.X, Screen.X * 2.0, 1e-3) && FMath::IsNearlyEqual(Ndc.Y, Screen.Y * 2.0, 1e-3));
				TestTrue(Case + TEXT(": 12. pixel = NDC in this viewport (y down)"), FMath::IsNearlyEqual(Pixel.X, (Ndc.X * 0.5 + 0.5) * Size.X, 1e-3)
					&& FMath::IsNearlyEqual(Pixel.Y, (0.5 - Ndc.Y * 0.5) * Size.Y, 1e-3));
			}
		}
		FVector2D Center, CenterNdc;
		FDiag::ProjectPoint(ViewProjection, Rect, Points[0], Center, CenterNdc);
		TestTrue(FString::Printf(TEXT("12. %dx%d: the aimed point is the view center"), Size.X, Size.Y), Center.Equals(FVector2D(Size.X * 0.5, Size.Y * 0.5), 0.01));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------------------------
// 1, 9, 10, 12, 13, 14, 16: the real panel on the real PIE player (PCM, output guard, external view target, guides,
// PIE end / restart, references released).

namespace ACFDiagnosticsTests
{
	struct FPie
	{
		FAutomationTestBase* Test = nullptr;
		TSharedPtr<SACFCameraTuningPanel> Panel;
		TStrongObjectPtr<UACFCameraPreset> Preset;
		TWeakObjectPtr<UACFCameraDirector> FirstDirector;
		TWeakObjectPtr<AActor> Camera;
		FACFCameraRequestHandle Handle;
		uint64 Mark = 0;
		int32 Cycle = 0;
	};
	using FPieStep = TFunction<bool(FPie&)>;

	class FPieCommand : public IAutomationLatentCommand
	{
	public:
		FPieCommand(TSharedRef<FPie> InPie, TArray<FPieStep> InSteps) : Pie(InPie), Steps(MoveTemp(InSteps)) {}
		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			StepStart = StepStart == 0.0 ? Now : StepStart;
			if (Now - StepStart > 90.0)
			{
				Pie->Test->AddError(FString::Printf(TEXT("Diagnostics PIE step %d timed out."), Index));
				return true;
			}
			if (Index < Steps.Num() && Steps[Index](*Pie))
			{
				++Index;
				StepStart = 0.0;
			}
			return Index >= Steps.Num();
		}
	private:
		TSharedRef<FPie> Pie;
		TArray<FPieStep> Steps;
		int32 Index = 0;
		double StepStart = 0.0;
	};

	inline FPieStep Wait(int32 Frames)
	{
		return [Frames](FPie& P) { if (P.Mark == 0) { P.Mark = GFrameCounter; } if (GFrameCounter < P.Mark + Frames) { return false; } P.Mark = 0; return true; };
	}

	inline APlayerCameraManager* Pcm(FPie& P)
	{
		FACFCameraTuningPanelModel& Model = P.Panel->GetModel();
		const APlayerController* PC = Model.GetPlayer() ? Model.GetPlayer()->GetPlayerController(Model.GetWorld()) : nullptr;
		return PC ? PC->PlayerCameraManager.Get() : nullptr;
	}

	inline TArray<FPieStep> PieSteps()
	{
		TArray<FPieStep> Steps;
		Steps.Add([](FPie& P)
		{
			P.Panel->Refresh(); // auto-selects the only PIE world and its player
			FACFCameraTuningPanelModel& Model = P.Panel->GetModel();
			if (!Model.GetPlayer() || !Pcm(P) || !Model.ResolveDirector())
			{
				return false;
			}
			UACFCameraSubsystem* Subsystem = Model.GetPlayer()->GetSubsystem<UACFCameraSubsystem>();
			FACFCameraRequestParams Params;
			Params.FollowTarget.Actor = Model.GetPlayer()->GetPlayerController(Model.GetWorld())->GetPawn();
			Params.DebugName = TEXT("DiagPIE");
			P.Handle = Subsystem->RequestCamera(P.Preset.Get(), Params);
			if (P.Cycle == 0)
			{
				P.FirstDirector = Model.ResolveDirector();
			}
			P.Test->TestTrue(TEXT("13. A new PIE gives a new director"), P.Cycle == 0 || (Model.ResolveDirector() != P.FirstDirector.Get() && !P.FirstDirector.IsValid()));
			P.Test->TestTrue(TEXT("1. Start profiling the PIE player"), P.Panel->GetDiagnostics().StartProfiling());
			return true;
		});
		Steps.Add(Wait(30));
		Steps.Add([](FPie& P)
		{
			FACFCameraDiagnosticsModel& Diag = P.Panel->GetDiagnostics();
			APlayerCameraManager* PCM = Pcm(P);
			// 1. Profiler of the PIE player, through the Profiler view of the real panel.
			P.Panel->SetActiveView(SACFCameraTuningPanel::EView::Profiler);
			const FACFCameraDiagnosticsModel::FProfileView& Profile = Diag.GetProfile();
			const FString MapName = P.Panel->GetModel().GetWorld()->GetMapName();
			P.Test->TestTrue(FString::Printf(TEXT("1. Profile of %s / Player 0"), *Profile.Target), Profile.bValid && Profile.Target.Contains(MapName) && Profile.Target.Contains(TEXT("Player 0")));
			P.Test->TestTrue(TEXT("1. Frames recorded"), Profile.Summary.NumFrames > 10);
			P.Test->TestTrue(TEXT("4. Real PCM: the output guard sweeps once per frame"), Profile.Summary.QueriesByPurpose[static_cast<int32>(EACFCameraQueryPurpose::OutputGuard)].Mean >= 1.0);
			P.Test->TestTrue(TEXT("4. Real PCM: the Collision node sweeps"), Profile.Summary.QueriesByPurpose[static_cast<int32>(EACFCameraQueryPurpose::Collision)].Mean >= 1.0);
			P.Test->TestTrue(TEXT("The Profiler view shows the period"), P.Panel->GetProfilerText().Contains(TEXT("Period:")) && P.Panel->GetProfilerText().Contains(TEXT("Total camera evaluation")));

			// 10. Debugger: ACF on screen, rendered pose = ACF pose (weight 1, no effects), FOV = PCM.
			P.Panel->SetActiveView(SACFCameraTuningPanel::EView::Debugger);
			const FACFCameraDiagnosticsModel::FDebugView Debug = Diag.ReadDebug();
			P.Test->TestTrue(FString::Printf(TEXT("10. ACF on screen (%s)"), *Debug.OutputState), Debug.bAcfOnScreen);
			P.Test->TestTrue(TEXT("10. Rendered FOV = PCM FOV"), Debug.bHasRenderedView && Debug.RenderedFov == PCM->GetFOVAngle());
			P.Test->TestTrue(FString::Printf(TEXT("10. Rendered location = ACF pose (%.3f cm)"), FVector::Dist(Debug.RenderedLocation, Debug.Result.Pose.Location)),
				FVector::Dist(Debug.RenderedLocation, Debug.Result.Pose.Location) < 0.01);
			P.Test->TestTrue(TEXT("10. Rendered FOV = ACF FOV"), FMath::IsNearlyEqual(Debug.RenderedFov, Debug.Result.Pose.FieldOfView, 0.01f));
			P.Test->TestTrue(TEXT("The Debugger view names the request"), P.Panel->GetDebuggerText().Contains(TEXT("DiagPIE")));

			// 12 (real viewport): the projection of the player's view, if the PIE viewport has a size.
			P.Panel->SetActiveView(SACFCameraTuningPanel::EView::Composition);
			const FACFCameraDiagnosticsModel::FCompositionView Composition = Diag.ReadComposition();
			if (Composition.bValid)
			{
				const FVector Ahead = PCM->GetCameraLocation() + PCM->GetCameraRotation().Vector() * 500.0;
				FVector2D Pixel, Ndc;
				FACFCameraDiagnosticsModel::ProjectPoint(Composition.ViewProjection, Composition.ViewRect, Ahead, Pixel, Ndc);
				P.Test->TestTrue(FString::Printf(TEXT("12. Real view: the point ahead is the center (ndc %.4f, %.4f)"), Ndc.X, Ndc.Y), Ndc.Equals(FVector2D::ZeroVector, 1e-3));
				const FACFCameraDiagnosticsModel::FProjectedPoint* PawnPoint = Composition.Points.FindByPredicate([](const auto& Point) { return Point.Label == TEXT("Player pawn"); });
				FVector2D Screen;
				double Depth = 0.0;
				if (PawnPoint && ACFFraming::Project(PCM->GetCameraLocation(), PCM->GetCameraRotation(), ACFFraming::MakeLens(Composition.ProjectionFov, Composition.ProjectionAspect), PawnPoint->World, Screen, Depth))
				{
					P.Test->TestTrue(FString::Printf(TEXT("11. Real view: the pawn's projection (%.4f, %.4f) = ACF's pinhole projection x2 (%.4f, %.4f) with the PCM pose and the projection's FOV %.3f / aspect %.4f"),
						PawnPoint->Ndc.X, PawnPoint->Ndc.Y, Screen.X * 2.0, Screen.Y * 2.0, Composition.ProjectionFov, Composition.ProjectionAspect), PawnPoint->Ndc.Equals(Screen * 2.0, 2e-3));
				}
				// The guides of the previous frames were drawn with the canvas's own SceneView (what was rendered): same place.
				const TPair<FString, FVector2D>* Drawn = Diag.GetLastGuidePoints().FindByPredicate([](const TPair<FString, FVector2D>& Entry) { return Entry.Key == TEXT("Player pawn"); });
				if (Diag.GetNumGuideDraws() > 0 && PawnPoint && Drawn)
				{
					P.Test->TestTrue(FString::Printf(TEXT("12. Drawn guide (%.4f, %.4f) = the panel's projection (%.4f, %.4f)"), Drawn->Value.X, Drawn->Value.Y, PawnPoint->Ndc.X, PawnPoint->Ndc.Y),
						Drawn->Value.Equals(PawnPoint->Ndc, 2e-3));
					const FIntPoint Canvas = Diag.GetLastGuideCanvasSize();
					P.Test->TestTrue(FString::Printf(TEXT("12. The guide canvas (%d x %d, DPI-scaled) has the player view's aspect (%d x %d)"), Canvas.X, Canvas.Y, Composition.ViewRect.Width(), Composition.ViewRect.Height()),
						Canvas.Y > 0 && FMath::IsNearlyEqual(static_cast<float>(Canvas.X) / Canvas.Y, Composition.AspectRatio, 0.01f));
				}
				P.Test->TestTrue(TEXT("The projection is centred on the PCM camera"), Composition.ViewOrigin.Equals(PCM->GetCameraLocation(), 0.01));
				P.Test->AddInfo(FString::Printf(TEXT("Rendered horizontal FOV %.3f, camera manager FOV %.3f"), Composition.ProjectionFov, PCM->GetFOVAngle()));
				P.Test->AddInfo(FString::Printf(TEXT("Real PIE view %d x %d in a %d x %d viewport (view aspect %.4f, projected with %.4f), %d guide draw(s)"), Composition.ViewRect.Width(),
					Composition.ViewRect.Height(), Composition.ViewportSize.X, Composition.ViewportSize.Y, Composition.AspectRatio, Composition.ProjectionAspect, Diag.GetNumGuideDraws()));
			}
			else
			{
				P.Test->AddInfo(FString::Printf(TEXT("Composition on the real viewport not checked: %s (projection checked by CompositionProjection)"), *Composition.Reason));
			}
			Diag.SetGuidesVisible(true);

			// 9. An external view target (what a Sequencer camera cut registers) is on screen: ACF is not.
			UWorld* World = P.Panel->GetModel().GetWorld();
			ACameraActor* Camera = World->SpawnActor<ACameraActor>(PCM->GetCameraLocation() + FVector(0.0, 0.0, 300.0), FRotator(-40.0, 0.0, 0.0));
			P.Camera = Camera;
			UACFCameraSubsystem* Subsystem = P.Panel->GetModel().GetPlayer()->GetSubsystem<UACFCameraSubsystem>();
			Subsystem->AddExternalViewTarget(Camera, Camera);
			PCM->GetOwningPlayerController()->SetViewTarget(Camera);
			return true;
		});
		Steps.Add(Wait(5));
		Steps.Add([](FPie& P)
		{
			const FACFCameraDiagnosticsModel::FDebugView Debug = P.Panel->GetDiagnostics().ReadDebug();
			P.Test->TestFalse(TEXT("9. External view target: ACF not on screen"), Debug.bAcfOnScreen);
			P.Test->TestTrue(FString::Printf(TEXT("9. External view target named (%s)"), *Debug.OutputState), Debug.OutputState.Contains(TEXT("external view target")) && Debug.ViewTarget == P.Camera->GetActorNameOrLabel());
			P.Test->TestTrue(TEXT("9. The director reports the bypass"), Debug.Result.bOutputBypassed);
			P.Test->TestTrue(TEXT("9. Rendered pose = the camera actor, not ACF"), Debug.RenderedLocation.Equals(P.Camera->GetActorLocation(), 0.5));
			UACFCameraSubsystem* Subsystem = P.Panel->GetModel().GetPlayer()->GetSubsystem<UACFCameraSubsystem>();
			Subsystem->RemoveExternalViewTarget(P.Camera.Get(), P.Camera.Get());
			APlayerCameraManager* PCM = Pcm(P);
			PCM->GetOwningPlayerController()->SetViewTarget(PCM->GetOwningPlayerController()->GetPawn());
			P.Camera->Destroy();
			return true;
		});
		Steps.Add(Wait(5));
		Steps.Add([](FPie& P)
		{
			P.Test->TestTrue(TEXT("9. Back on ACF"), P.Panel->GetDiagnostics().ReadDebug().bAcfOnScreen);
			return true;
		});
		return Steps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDiagnosticsPieTest, "ActionCameraFramework.Editor.Diagnostics.RealPIEPanel", ACFDiagnosticsTests::Flags)
bool FACFDiagnosticsPieTest::RunTest(const FString& Parameters)
{
	using namespace ACFDiagnosticsTests;
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Skipped: Slate is not initialized."));
		return true;
	}
	TSharedRef<FPie> Pie = MakeShared<FPie>();
	Pie->Test = this;
	Pie->Panel = SNew(SACFCameraTuningPanel);
	Pie->Preset.Reset(MakePreset());
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/ActionCameraFramework/Samples/L_ACFCameraTest")));
	for (int32 Cycle = 0; Cycle < 2; ++Cycle)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		TArray<FPieStep> Steps;
		Steps.Add([Cycle](FPie& P) { P.Cycle = Cycle; return true; });
		Steps.Append(PieSteps());
		ADD_LATENT_AUTOMATION_COMMAND(FPieCommand(Pie, MoveTemp(Steps)));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		TArray<FPieStep> After;
		After.Add([](FPie& P) { return !(GEditor && GEditor->PlayWorld); });
		After.Add([](FPie& P)
		{
			// 13. PIE ended: the target, the profiling and the views are released.
			P.Panel->Refresh();
			FACFCameraDiagnosticsModel& Diag = P.Panel->GetDiagnostics();
			P.Test->TestFalse(TEXT("13. PIE ended: no profiling left"), Diag.IsProfiling() || Diag.GetProfile().bForcedByPanel);
			P.Test->TestFalse(TEXT("13. PIE ended: debugger has no target"), Diag.ReadDebug().bValid);
			P.Test->TestFalse(TEXT("13. PIE ended: composition has no target"), Diag.ReadComposition().bValid);
			P.Test->TestTrue(TEXT("13. PIE ended: the Composition view says so"), P.Panel->GetCompositionText().Contains(TEXT("Not available")));
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			P.Test->TestFalse(TEXT("14. The PIE director is gone (nothing of the panel kept it alive)"), P.FirstDirector.IsValid());
			return true;
		});
		ADD_LATENT_AUTOMATION_COMMAND(FPieCommand(Pie, MoveTemp(After)));
	}
	TArray<FPieStep> Close;
	Close.Add([](FPie& P)
	{
		// 14. Closing the panel: the guides are unregistered with it (no draw delegate is left behind).
		TWeakPtr<SACFCameraTuningPanel> Weak = P.Panel;
		P.Test->TestTrue(TEXT("Guides were on"), P.Panel->GetDiagnostics().AreGuidesVisible());
		P.Panel.Reset();
		P.Test->TestFalse(TEXT("14. Panel closed: destroyed"), Weak.IsValid());
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FPieCommand(Pie, MoveTemp(Close)));
	return true;
}

// 15: the camera evaluation does not get slower with the panel open (any view), and the diagnostics are computed only for
// the visible view, at the panel's rate. Prints the UI cost per refresh separately from the camera cost.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDiagnosticsCostTest, "ActionCameraFramework.Editor.Diagnostics.CostOfTheViews", ACFDiagnosticsTests::Flags)
bool FACFDiagnosticsCostTest::RunTest(const FString& Parameters)
{
	using namespace ACFDiagnosticsTests;
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Skipped: Slate is not initialized."));
		return true;
	}
	FPlayer A(300000);
	A.Step(30);
	auto TimeEvaluation = [&A]()
	{
		TArray<double> Samples;
		for (int32 Round = 0; Round < 7; ++Round)
		{
			const double Start = FPlatformTime::Seconds();
			A.Step(400);
			Samples.Add((FPlatformTime::Seconds() - Start) * 1e6 / 400.0);
		}
		Samples.Sort();
		return Samples[3];
	};
	const double Closed = TimeEvaluation();
	TSharedRef<SACFCameraTuningPanel> Panel = SNew(SACFCameraTuningPanel).DirectorForTesting(A.Director.Get());
	struct FMode { const TCHAR* Name; SACFCameraTuningPanel::EView View; bool bProfile; };
	const FMode Modes[] = { { TEXT("open, Tuning view"), SACFCameraTuningPanel::EView::Tuning, false }, { TEXT("open, Debugger view"), SACFCameraTuningPanel::EView::Debugger, false },
		{ TEXT("open, Composition view"), SACFCameraTuningPanel::EView::Composition, false }, { TEXT("open, Profiler view, profiling"), SACFCameraTuningPanel::EView::Profiler, true } };
	AddInfo(FString::Printf(TEXT("panel closed: camera evaluation %.3f us / frame"), Closed));
	for (const FMode& Mode : Modes)
	{
		Panel->SetActiveView(Mode.View);
		if (Mode.bProfile)
		{
			Panel->OnStartProfiling();
		}
		const int32 RefreshesBefore = Panel->GetNumDiagnosticsRefreshes();
		const double Evaluation = TimeEvaluation(); // the panel is not refreshed by camera frames
		TestEqual(FString::Printf(TEXT("%s: camera frames do not refresh the panel"), Mode.Name), Panel->GetNumDiagnosticsRefreshes(), RefreshesBefore);
		const double Start = FPlatformTime::Seconds();
		for (int32 I = 0; I < 50; ++I)
		{
			Panel->Refresh();
		}
		const double Refresh = (FPlatformTime::Seconds() - Start) * 1e6 / 50.0;
		TestEqual(FString::Printf(TEXT("%s: diagnostics computed only for a diagnostics view"), Mode.Name), Panel->GetNumDiagnosticsRefreshes() - RefreshesBefore,
			Mode.View == SACFCameraTuningPanel::EView::Tuning ? 0 : 50);
		AddInfo(FString::Printf(TEXT("%s: camera evaluation %.3f us / frame, panel refresh %.1f us / call (4 calls per second)"), Mode.Name, Evaluation, Refresh));
	}
	// The profile statistics themselves (Summarize over the ring buffer): measured alone, and throttled to 1 Hz in the panel.
	A.Step(1100); // a full ring buffer
	const int32 ComputationsBefore = Panel->GetNumProfileComputations();
	for (int32 I = 0; I < 20; ++I)
	{
		Panel->Refresh(); // 20 quick refreshes: at most one recomputation (or two across a second boundary)
	}
	TestTrue(TEXT("Profile statistics throttled to once per second"), Panel->GetNumProfileComputations() - ComputationsBefore <= 2);
	const double StatsStart = FPlatformTime::Seconds();
	for (int32 I = 0; I < 10; ++I)
	{
		Panel->GetDiagnostics().UpdateProfile();
	}
	AddInfo(FString::Printf(TEXT("profile statistics of a full ring buffer (%d frames): %.1f us per computation (the panel does it once per second)"),
		Panel->GetDiagnostics().GetProfile().Summary.NumFrames, (FPlatformTime::Seconds() - StatsStart) * 1e6 / 10.0));
	Panel->OnStopProfiling();
	A.Step(1);
	TestFalse(TEXT("Profiling released"), A.Director->GetProfiler().IsRecording());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
