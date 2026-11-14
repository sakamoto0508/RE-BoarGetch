#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/ACFCameraTestUtils.h"
#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "ACFCameraZoneComponent.h"
#include "ACFCameraZoneRequests.h"
#include "Components/SceneComponent.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 7C-2B: returning from a camera above the zone (Lock-On, Finisher, a Sequencer camera) to the zone camera (README 32 章).
 * Standalone director, the zone update before the camera evaluation every frame (as in the game). The view given to the
 * zone is the pose on screen (the last evaluation; the Sequencer's camera while it owns the screen). Rotation continuity is
 * measured frame by frame as the angle between two consecutive poses.
 */
namespace ACFZoneReturnTests
{
	inline double Angle(const FRotator& A, const FRotator& B) { return FMath::RadiansToDegrees(A.Quaternion().AngularDistance(B.Quaternion())); }

	struct FRig
	{
		TStrongObjectPtr<UACFCameraDirector> Director;
		TStrongObjectPtr<USceneComponent> Target;
		TArray<TStrongObjectPtr<UObject>> Keep;
		FACFCameraZoneRequestState State;
		ACFTests::FFrameDriver Driver;
		FACFCameraEvaluationResult Last;
		float Dt = 1.f / 60.f;
		double Time = 100.0;
		int32 ControlRotationSets = 0;
		/** The Sequencer camera on screen while the driver evaluates as output-bypassed. */
		FRotator ExternalView = FRotator::ZeroRotator;
		/** Largest frame-to-frame rotation change since ResetSteps (degrees), and the frame it happened. */
		double MaxStep = 0.0;
		int32 MaxStepFrame = 0;
		int32 Frames = 0;

		explicit FRig(float InDt = 1.f / 60.f) : Dt(InDt)
		{
			Director.Reset(NewObject<UACFCameraDirector>());
			Target.Reset(NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient));
			Target->SetMobility(EComponentMobility::Movable);
			Target->SetWorldLocation(FVector(0.0, 0.0, 100.0));
			Driver.Upstream.Location = FVector(0.0, 0.0, 500.0);
			Driver.Upstream.FieldOfView = 90.f;
			Driver.bHasControlRotation = true;
			Driver.ControlRotation = FRotator(-15.0, 0.0, 0.0);
		}

		UACFCameraPreset* Orbit(float InitialYaw, EACFCameraOrbitInputSource Source = EACFCameraOrbitInputSource::CameraInput)
		{
			UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
			Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
			Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
			UACFCameraNode_OrbitalFollow* Node = NewObject<UACFCameraNode_OrbitalFollow>(Preset);
			Node->Distance = 400.f;
			Node->PivotOffset = FVector::ZeroVector;
			Node->InputSource = Source;
			Node->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
			Node->InitialYaw = InitialYaw;
			Node->InitialPitch = -15.f;
			Node->bEnableMovementAssistance = false;
			Node->bRecenterWhenIdle = false;
			Preset->Nodes.Add(Node);
			Keep.Emplace(Preset);
			return Preset;
		}

		UACFCameraZoneComponent* Zone(UACFCameraPreset* Preset, float BlendIn, EACFCameraZoneRotationHandoff Enter)
		{
			UACFCameraZoneComponent* Zone = NewObject<UACFCameraZoneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			Zone->Preset = Preset;
			Zone->bOverrideBlendIn = true;
			Zone->BlendIn = BlendIn > 0.f ? ACFTests::Linear(BlendIn) : FACFCameraBlendSettings::MakeCut();
			Zone->bOverrideBlendOut = true;
			Zone->BlendOut = ACFTests::Linear(0.4f);
			Zone->EnterRotation = Enter;
			Keep.Emplace(Zone);
			return Zone;
		}

		FACFCameraRequestHandle Game(UACFCameraPreset* Preset, int32 Priority, const FACFCameraBlendSettings* BlendIn = nullptr)
		{
			FACFCameraRequestParams Params;
			Params.Priority = Priority;
			Params.FollowTarget.Component = Target.Get();
			if (BlendIn)
			{
				Params.bOverrideBlendIn = true;
				Params.BlendIn = *BlendIn;
			}
			return Director->AddRequest(Preset, Params);
		}

		/** The view on screen: the Sequencer camera while bypassed, else the last ACF pose. */
		FRotator View() const { return Driver.bOutputBypassed ? ExternalView : Last.Pose.Rotation; }

		EACFCameraZoneRequestEvent Update(UACFCameraZoneComponent* Selected, bool bCut = false)
		{
			FACFCameraZoneRequestContext C;
			C.Director = Director.Get();
			C.FollowTarget.Component = Target.Get();
			C.bHasViewRotation = Last.FrameNumber != 0;
			C.ViewRotation = View();
			C.SetControlRotation = [this](const FRotator& Rotation) { Driver.ControlRotation = Rotation; ++ControlRotationSets; };
			C.Time = Time;
			C.bCut = bCut;
			return State.Update(C, Selected);
		}

		void Step()
		{
			const FRotator Before = View();
			const bool bHadFrame = Last.FrameNumber != 0;
			Time += Dt;
			Last = Driver.Step(Director.Get(), Dt);
			++Frames;
			const double Change = bHadFrame ? Angle(Before, View()) : 0.0;
			if (Change > MaxStep)
			{
				MaxStep = Change;
				MaxStepFrame = Frames;
			}
		}

		void Run(UACFCameraZoneComponent* Selected, float Seconds)
		{
			const int32 N = FMath::RoundToInt(Seconds / Dt);
			for (int32 I = 0; I < N; ++I)
			{
				Update(Selected);
				Step();
			}
		}

		void ResetSteps() { MaxStep = 0.0; MaxStepFrame = 0; }
	};

	/** The largest step a linear blend of TotalDegrees over Seconds may make in one frame of Dt (5% + 0.05 deg slack). */
	inline double StepBound(double TotalDegrees, float Seconds, float Dt) { return TotalDegrees * Dt / Seconds * 1.05 + 0.05; }
}

// Zone A, Lock-On, the player moves to zone B under the Lock-On, the Lock-On ends: a Keep View zone B (never shown) appears
// where the Lock-On was looking, with no rotation at all; a Use Camera Setting B turns to its own rotation in one smooth blend.
// The same results at 30 / 60 / 120 fps. The Lock-On's own rotation is never changed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneReturnLockOnTest, "ActionCameraFramework.Zone.Return.LockOnToAnotherZone", ACFTests::Flags)
bool FACFZoneReturnLockOnTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneReturnTests;
	for (const int32 Fps : { 30, 60, 120 })
	{
		for (const EACFCameraZoneRotationHandoff Enter : { EACFCameraZoneRotationHandoff::KeepView, EACFCameraZoneRotationHandoff::UseCameraSetting })
		{
			const bool bKeep = Enter == EACFCameraZoneRotationHandoff::KeepView;
			const FString Case = FString::Printf(TEXT("%d fps, %s"), Fps, bKeep ? TEXT("Keep View") : TEXT("Use Camera Setting"));
			FRig R(1.f / Fps);
			R.Game(R.Orbit(0.f), 0);
			UACFCameraZoneComponent* A = R.Zone(R.Orbit(30.f), 0.3f, EACFCameraZoneRotationHandoff::UseCameraSetting);
			UACFCameraZoneComponent* B = R.Zone(R.Orbit(90.f), 0.3f, Enter);
			R.Run(A, 0.5f);
			const FACFCameraRequestHandle LockOn = R.Game(R.Orbit(150.f), 10);
			R.Run(A, 0.3f);
			const FRotator LockOnView = R.Last.Pose.Rotation;
			TestTrue(FString::Printf(TEXT("%s: Lock-On at yaw 150"), *Case), FMath::Abs(FRotator::NormalizeAxis(LockOnView.Yaw - 150.0)) < 0.5);
			R.Run(B, 0.3f); // moved to B under the Lock-On
			TestTrue(FString::Printf(TEXT("%s: the Lock-On untouched"), *Case), Angle(R.Last.Pose.Rotation, LockOnView) < 0.01);
			TestEqual(FString::Printf(TEXT("%s: B follows the view while hidden only with Keep View"), *Case), R.State.IsTrackingView(), bKeep);
			// The game ends the Lock-On with a 0.4 s return blend.
			const FACFCameraBlendSettings Return = ACFTests::Linear(0.4f);
			R.Director->RemoveRequest(LockOn, &Return, /*bOverrideAnyReturn*/ true);
			R.ResetSteps();
			R.Run(B, 0.6f);
			const FRotator Final = R.Last.Pose.Rotation;
			TestTrue(FString::Printf(TEXT("%s: B in control"), *Case), R.Last.ActiveRequest == R.State.GetHandle());
			TestFalse(FString::Printf(TEXT("%s: tracking ended once shown"), *Case), R.State.IsTrackingView());
			const double Expected = bKeep ? 150.0 : 90.0;
			TestTrue(FString::Printf(TEXT("%s: B's yaw %.2f == %.0f"), *Case, Final.Yaw, Expected), FMath::Abs(FRotator::NormalizeAxis(Final.Yaw - Expected)) < 0.5);
			const double Bound = StepBound(Angle(LockOnView, Final), 0.4f, R.Dt);
			TestTrue(FString::Printf(TEXT("%s: largest frame step %.3f deg (frame %d) <= %.3f (one linear blend, no jump at its start or end)"), *Case, R.MaxStep, R.MaxStepFrame, Bound), R.MaxStep <= Bound);
			R.ResetSteps();
			R.Run(B, 0.2f);
			TestTrue(FString::Printf(TEXT("%s: still after the blend (%.4f deg)"), *Case, R.MaxStep), R.MaxStep < 0.01);
			TestEqual(FString::Printf(TEXT("%s: no control rotation change"), *Case), R.ControlRotationSets, 0);
		}
	}
	return true;
}

// The Lock-On ends and the zone changes in the same frame, in both orders: one blend from the Lock-On to B (A never comes
// back on screen), B at the Lock-On's view (Keep View).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneReturnSameFrameTest, "ActionCameraFramework.Zone.Return.SameFrameReleaseAndZoneChange", ACFTests::Flags)
bool FACFZoneReturnSameFrameTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneReturnTests;
	for (const bool bReleaseFirst : { true, false })
	{
		const TCHAR* Case = bReleaseFirst ? TEXT("Lock-On released before the zone update") : TEXT("Lock-On released after the zone update");
		FRig R;
		R.Game(R.Orbit(0.f), 0);
		UACFCameraZoneComponent* A = R.Zone(R.Orbit(30.f), 0.3f, EACFCameraZoneRotationHandoff::KeepView);
		UACFCameraZoneComponent* B = R.Zone(R.Orbit(90.f), 0.3f, EACFCameraZoneRotationHandoff::KeepView);
		R.Run(A, 0.5f);
		const FACFCameraRequestHandle LockOn = R.Game(R.Orbit(150.f), 10);
		R.Run(A, 0.3f);
		const FRotator LockOnView = R.Last.Pose.Rotation;
		const FACFCameraBlendSettings Return = ACFTests::Linear(0.4f);
		if (bReleaseFirst)
		{
			R.Director->RemoveRequest(LockOn, &Return, true);
			R.Update(B);
		}
		else
		{
			R.Update(B);
			R.Director->RemoveRequest(LockOn, &Return, true);
		}
		R.ResetSteps();
		R.Step();
		TestTrue(FString::Printf(TEXT("%s: B active at once (A never back)"), Case), R.Last.ActiveRequest == R.State.GetHandle());
		R.Run(B, 0.6f);
		TestTrue(FString::Printf(TEXT("%s: B at the Lock-On's yaw (%.2f)"), Case, R.Last.Pose.Rotation.Yaw), Angle(R.Last.Pose.Rotation, LockOnView) < 0.5);
		TestTrue(FString::Printf(TEXT("%s: no rotation step (%.4f deg)"), Case, R.MaxStep), R.MaxStep < 0.05);
	}
	return true;
}

// A finisher (its own Return Blend) while the zone changes: the new Keep View zone appears at the finisher's view, even though
// the finisher ends inside the camera update (after the zone update of that frame). 30 / 60 / 120 fps.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneReturnFinisherTest, "ActionCameraFramework.Zone.Return.FinisherToAnotherZone", ACFTests::Flags)
bool FACFZoneReturnFinisherTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneReturnTests;
	for (const int32 Fps : { 30, 60, 120 })
	{
		FRig R(1.f / Fps);
		R.Game(R.Orbit(0.f), 0);
		UACFCameraZoneComponent* A = R.Zone(R.Orbit(30.f), 0.3f, EACFCameraZoneRotationHandoff::KeepView);
		UACFCameraZoneComponent* B = R.Zone(R.Orbit(90.f), 0.3f, EACFCameraZoneRotationHandoff::KeepView);
		UACFCameraPreset* FinisherPreset = ACFTests::MakeFixedPreset(FVector(0.0, -1000.0, 300.0), 60.f); // rotation (-10, 45)
		R.Keep.Emplace(FinisherPreset);
		R.Run(A, 0.5f);
		const FACFCameraSequenceHandle Finisher = R.Director->PlayFinisherPreset(FinisherPreset, nullptr, nullptr, 0.5f, FACFCameraBlendSettings::MakeCut(), ACFTests::Linear(0.3f), 1000);
		R.Run(A, 0.2f);
		const FRotator FinisherView = R.Last.Pose.Rotation;
		R.Run(B, 0.1f);
		TestTrue(FString::Printf(TEXT("%d fps: finisher still playing"), Fps), R.Director->IsSequencePlaying(Finisher));
		R.ResetSteps();
		int32 Guard = 0;
		while (R.Director->IsSequencePlaying(Finisher) && Guard++ < 4 * Fps)
		{
			R.Run(B, R.Dt);
		}
		R.Run(B, 0.5f);
		const FRotator Final = R.Last.Pose.Rotation;
		TestTrue(FString::Printf(TEXT("%d fps: B in control"), Fps), R.Last.ActiveRequest == R.State.GetHandle());
		TestTrue(FString::Printf(TEXT("%d fps: B at the finisher's view (%s == %s)"), Fps, *Final.ToCompactString(), *FinisherView.ToCompactString()), Angle(Final, FinisherView) < 0.5);
		TestTrue(FString::Printf(TEXT("%d fps: no rotation step (%.4f deg)"), Fps, R.MaxStep), R.MaxStep < 0.05);
	}
	return true;
}

// A Sequencer camera owns the screen (output bypassed) and ACF blends back from its last pose (Blend From Pose, what the
// Sequencer integration does): a zone shown before the sequence comes back with its own rotation (the Sequencer's "Keep"
// policy, unchanged); a Keep View zone entered during the sequence appears at the Sequencer camera's rotation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneReturnSequencerTest, "ActionCameraFramework.Zone.Return.SequencerToZone", ACFTests::Flags)
bool FACFZoneReturnSequencerTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneReturnTests;
	for (const bool bNewZone : { false, true })
	{
		const TCHAR* Case = bNewZone ? TEXT("zone entered during the sequence") : TEXT("same zone as before the sequence");
		FRig R;
		R.Game(R.Orbit(0.f), 0);
		UACFCameraZoneComponent* A = R.Zone(R.Orbit(30.f), 0.3f, EACFCameraZoneRotationHandoff::KeepView);
		UACFCameraZoneComponent* B = R.Zone(R.Orbit(90.f), 0.3f, EACFCameraZoneRotationHandoff::KeepView);
		R.Run(A, 0.5f);
		const FRotator BeforeSequence = R.Last.Pose.Rotation;
		// The sequence: its camera looks at yaw -60, pitch -20.
		FACFCameraPose SequencerPose;
		SequencerPose.Location = FVector(800.0, 800.0, 400.0);
		SequencerPose.Rotation = FRotator(-20.0, -60.0, 0.0);
		SequencerPose.FieldOfView = 60.f;
		R.ExternalView = SequencerPose.Rotation;
		R.Driver.bOutputBypassed = true;
		R.Run(A, 0.2f); // the sequence has been on screen for a while
		UACFCameraZoneComponent* Zone = bNewZone ? B : A;
		R.Run(Zone, 0.5f);
		if (bNewZone)
		{
			TestTrue(FString::Printf(TEXT("%s: B follows the Sequencer view while the Sequencer owns the screen"), Case), R.State.IsTrackingView());
		}
		// The sequence ends: ACF returns from the Sequencer's last pose in 0.4 s.
		R.Driver.bOutputBypassed = false;
		R.Last.Pose = SequencerPose; // the screen showed the Sequencer camera until now
		TestTrue(TEXT("Blend From Pose"), R.Director->BlendFromPose(SequencerPose, ACFTests::Linear(0.4f)));
		R.ResetSteps();
		R.Run(Zone, 0.6f);
		const FRotator Final = R.Last.Pose.Rotation;
		const FRotator Expected = bNewZone ? SequencerPose.Rotation : BeforeSequence;
		TestTrue(FString::Printf(TEXT("%s: final %s == %s"), Case, *Final.ToCompactString(), *Expected.ToCompactString()), Angle(Final, Expected) < 0.5);
		const double Bound = StepBound(Angle(SequencerPose.Rotation, Final), 0.4f, R.Dt);
		TestTrue(FString::Printf(TEXT("%s: largest step %.3f deg <= %.3f"), Case, R.MaxStep, Bound), R.MaxStep <= Bound);
		TestEqual(FString::Printf(TEXT("%s: no control rotation change"), Case), R.ControlRotationSets, 0);
	}
	return true;
}

// A Lock-On interrupts the A -> B blend; after it, B (already shown) keeps its own rotation and blends back smoothly. A
// teleport into C under a Lock-On: C is not cut (the Lock-On has the screen) and appears at the Lock-On's view with a blend.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneReturnInterruptTest, "ActionCameraFramework.Zone.Return.InterruptAndTeleport", ACFTests::Flags)
bool FACFZoneReturnInterruptTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneReturnTests;
	FRig R;
	R.Game(R.Orbit(0.f), 0);
	UACFCameraZoneComponent* A = R.Zone(R.Orbit(30.f), 0.4f, EACFCameraZoneRotationHandoff::UseCameraSetting);
	UACFCameraZoneComponent* B = R.Zone(R.Orbit(90.f), 0.4f, EACFCameraZoneRotationHandoff::UseCameraSetting);
	UACFCameraZoneComponent* C = R.Zone(R.Orbit(-90.f), 0.4f, EACFCameraZoneRotationHandoff::KeepView);
	R.Run(A, 0.5f);
	R.Run(B, 0.2f); // half way through A -> B
	TestTrue(TEXT("A -> B blending"), R.Last.bIsBlending);
	const FACFCameraBlendSettings In = ACFTests::Linear(0.3f);
	FACFCameraRequestHandle LockOn = R.Game(R.Orbit(150.f), 10, &In);
	R.ResetSteps();
	const FRotator Before = R.Last.Pose.Rotation;
	R.Run(B, 0.5f);
	TestTrue(TEXT("Lock-On in control"), R.Last.ActiveRequest == LockOn);
	TestTrue(FString::Printf(TEXT("Into the Lock-On from the pose on screen: largest step %.3f"), R.MaxStep), R.MaxStep <= StepBound(Angle(Before, R.Last.Pose.Rotation), 0.3f, R.Dt) + 1.0);
	const FRotator LockOnView = R.Last.Pose.Rotation;
	const FACFCameraBlendSettings Return = ACFTests::Linear(0.4f);
	R.Director->RemoveRequest(LockOn, &Return, true);
	R.ResetSteps();
	R.Run(B, 0.6f);
	TestTrue(FString::Printf(TEXT("Back to B at its own yaw 90 (%.2f)"), R.Last.Pose.Rotation.Yaw), FMath::Abs(FRotator::NormalizeAxis(R.Last.Pose.Rotation.Yaw - 90.0)) < 0.5);
	TestTrue(FString::Printf(TEXT("One smooth return: largest step %.3f"), R.MaxStep), R.MaxStep <= StepBound(Angle(LockOnView, R.Last.Pose.Rotation), 0.4f, R.Dt));

	// Teleport into C under a new Lock-On.
	LockOn = R.Game(R.Orbit(150.f), 10);
	R.Run(B, 0.3f);
	const FRotator LockOnView2 = R.Last.Pose.Rotation;
	R.Update(C, /*bCut*/ true);
	R.Step();
	TestTrue(TEXT("Teleport under the Lock-On: the Lock-On keeps the screen"), R.Last.ActiveRequest == LockOn && !R.Last.bIsBlending);
	R.Run(C, 0.2f);
	R.Director->RemoveRequest(LockOn, &Return, true);
	R.ResetSteps();
	R.Update(C);
	R.Step();
	TestTrue(TEXT("After the Lock-On: C, blending (no teleport cut kept)"), R.Last.ActiveRequest == R.State.GetHandle() && R.Last.bIsBlending);
	R.Run(C, 0.6f);
	TestTrue(FString::Printf(TEXT("C (Keep View, never shown) at the Lock-On's view (%.2f)"), R.Last.Pose.Rotation.Yaw), Angle(R.Last.Pose.Rotation, LockOnView2) < 0.5);
	TestTrue(FString::Printf(TEXT("No rotation step (%.4f)"), R.MaxStep), R.MaxStep < 0.05);
	return true;
}

// Control Rotation games: a Keep View zone camera driven by the control rotation, requested under a Lock-On, never changes
// the control rotation (neither under the Lock-On nor when it appears); a Control Rotation exploration camera after a zone
// left under the Lock-On is not changed either. Two players with their own Lock-Ons get their own results.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneReturnControlRotationTest, "ActionCameraFramework.Zone.Return.ControlRotationAndPlayers", ACFTests::Flags)
bool FACFZoneReturnControlRotationTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneReturnTests;
	{
		FRig R;
		R.Game(R.Orbit(0.f, EACFCameraOrbitInputSource::ControlRotation), 0);
		UACFCameraZoneComponent* A = R.Zone(R.Orbit(30.f), 0.3f, EACFCameraZoneRotationHandoff::KeepView);
		UACFCameraZoneComponent* B = R.Zone(R.Orbit(0.f, EACFCameraOrbitInputSource::ControlRotation), 0.3f, EACFCameraZoneRotationHandoff::KeepView);
		R.Run(A, 0.5f);
		const int32 SetsBefore = R.ControlRotationSets;
		FACFCameraRequestHandle LockOn = R.Game(R.Orbit(150.f), 10);
		R.Run(A, 0.2f);
		R.Run(B, 0.5f);
		TestEqual(TEXT("Control Rotation zone camera under the Lock-On: the control rotation is not changed"), R.ControlRotationSets, SetsBefore);
		R.Director->RemoveRequest(LockOn);
		R.Run(B, 0.6f);
		TestEqual(TEXT("... nor when it appears"), R.ControlRotationSets, SetsBefore);
		TestTrue(TEXT("It shows the control rotation (yaw 0)"), FMath::Abs(FRotator::NormalizeAxis(R.Last.Pose.Rotation.Yaw)) < 0.5);
		// Leave the zone under another Lock-On: the Control Rotation exploration camera returns with nothing changed.
		LockOn = R.Game(R.Orbit(150.f), 10);
		R.Run(B, 0.2f);
		R.Run(nullptr, 0.2f);
		R.Director->RemoveRequest(LockOn);
		R.Run(nullptr, 0.5f);
		TestEqual(TEXT("Zone left under the Lock-On: no control rotation change"), R.ControlRotationSets, SetsBefore);
	}
	{
		// Two players (two directors, two zone states) with different Lock-On views at the same time.
		FRig P1;
		FRig P2;
		FRig* Players[] = { &P1, &P2 };
		const float LockYaw[] = { 150.f, -120.f };
		UACFCameraZoneComponent* Zones[2][2];
		FACFCameraRequestHandle LockOns[2];
		for (int32 I = 0; I < 2; ++I)
		{
			FRig& R = *Players[I];
			R.Game(R.Orbit(0.f), 0);
			Zones[I][0] = R.Zone(R.Orbit(30.f), 0.3f, EACFCameraZoneRotationHandoff::KeepView);
			Zones[I][1] = R.Zone(R.Orbit(90.f), 0.3f, EACFCameraZoneRotationHandoff::KeepView);
		}
		auto RunBoth = [&](int32 ZoneIndex, float Seconds) { for (int32 I = 0; I < 2; ++I) { Players[I]->Run(Zones[I][ZoneIndex], Seconds); } };
		RunBoth(0, 0.5f);
		for (int32 I = 0; I < 2; ++I)
		{
			LockOns[I] = Players[I]->Game(Players[I]->Orbit(LockYaw[I]), 10);
		}
		RunBoth(0, 0.2f);
		RunBoth(1, 0.3f);
		for (int32 I = 0; I < 2; ++I)
		{
			Players[I]->Director->RemoveRequest(LockOns[I]);
		}
		RunBoth(1, 0.5f);
		for (int32 I = 0; I < 2; ++I)
		{
			TestTrue(FString::Printf(TEXT("Player %d: B at its own Lock-On's yaw %.0f (%.2f)"), I + 1, LockYaw[I], Players[I]->Last.Pose.Rotation.Yaw),
				FMath::Abs(FRotator::NormalizeAxis(Players[I]->Last.Pose.Rotation.Yaw - LockYaw[I])) < 0.5);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
