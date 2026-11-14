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
 * Phase 7C-1B: the zone camera request of one player (FACFCameraZoneRequestState, README 32 章) on a standalone director.
 * Every frame does what the zone subsystem does: the request state is updated first, then the camera evaluates (the
 * zone subsystem ticks before the camera update). The view rotation given to the state is the last evaluated pose.
 */
namespace ACFZoneRequestTests
{
	constexpr float Dt = 0.1f;
	const FVector LocA(1000.0, 0.0, 200.0);
	const FVector LocB(0.0, 1000.0, 200.0);
	const FVector LocC(-1000.0, 0.0, 200.0);
	const FVector LocFinisher(0.0, -1000.0, 200.0);

	inline FACFCameraBlendSettings LinearBlend(float Seconds)
	{
		FACFCameraBlendSettings Blend;
		Blend.Curve = EACFCameraBlendCurve::Linear;
		Blend.Duration = Seconds;
		return Blend;
	}

	inline float YawDelta(float A, float B) { return FMath::Abs(FRotator::NormalizeAxis(A - B)); }

	struct FRig
	{
		TStrongObjectPtr<UACFCameraDirector> Director;
		TStrongObjectPtr<USceneComponent> Target;
		TArray<TStrongObjectPtr<UObject>> Keep;
		FACFCameraZoneRequestState State;
		ACFTests::FFrameDriver Driver;
		FACFCameraEvaluationResult Last;
		double Time = 100.0;
		int32 ControlRotationSets = 0;
		float RetryDelay = 1.f;

		FRig()
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

		/** An Orbital Follow camera (no assistance) starting at world yaw InitialYaw, pitch -15. */
		UACFCameraPreset* Orbit(EACFCameraOrbitInputSource Source, float InitialYaw)
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

		/** A zone (no World: only its camera settings are used here) with a fixed camera at Location, rotation (-10, 45). */
		UACFCameraZoneComponent* Zone(const FVector& Location, float BlendIn, float BlendOut)
		{
			UACFCameraPreset* Preset = ACFTests::MakeFixedPreset(Location, 70.f);
			Keep.Emplace(Preset);
			return Zone(Preset, BlendIn, BlendOut);
		}

		UACFCameraZoneComponent* Zone(UACFCameraPreset* Preset, float BlendIn, float BlendOut)
		{
			UACFCameraZoneComponent* Zone = NewObject<UACFCameraZoneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			Zone->Preset = Preset;
			Zone->bOverrideBlendIn = true;
			Zone->BlendIn = BlendIn > 0.f ? LinearBlend(BlendIn) : FACFCameraBlendSettings::MakeCut();
			Zone->bOverrideBlendOut = true;
			Zone->BlendOut = BlendOut > 0.f ? LinearBlend(BlendOut) : FACFCameraBlendSettings::MakeCut();
			Keep.Emplace(Zone);
			return Zone;
		}

		/** A game request (exploration: priority 0; Lock-On: 10) following the target. */
		FACFCameraRequestHandle Game(UACFCameraPreset* Preset, int32 Priority, FName Name = NAME_None)
		{
			FACFCameraRequestParams Params;
			Params.Priority = Priority;
			Params.FollowTarget.Component = Target.Get();
			Params.DebugName = Name;
			return Director->AddRequest(Preset, Params);
		}

		FACFCameraZoneRequestContext Context()
		{
			FACFCameraZoneRequestContext C;
			C.Director = Director.Get();
			C.FollowTarget.Component = Target.Get();
			C.bHasViewRotation = Last.FrameNumber != 0;
			C.ViewRotation = Last.Pose.Rotation;
			C.SetControlRotation = [this](const FRotator& Rotation)
			{
				Driver.ControlRotation = Rotation;
				++ControlRotationSets;
			};
			C.Time = Time;
			C.Priority = 5;
			C.RetryDelay = RetryDelay;
			C.RetryMaxDelay = 8.f;
			return C;
		}

		EACFCameraZoneRequestEvent Update(UACFCameraZoneComponent* Selected) { return State.Update(Context(), Selected); }

		const FACFCameraEvaluationResult& Step()
		{
			Time += Dt;
			Last = Driver.Step(Director.Get(), Dt);
			return Last;
		}

		/** One frame: zone update, then the camera. Returns the update's event. */
		EACFCameraZoneRequestEvent Frame(UACFCameraZoneComponent* Selected)
		{
			const EACFCameraZoneRequestEvent Event = Update(Selected);
			Step();
			return Event;
		}

		void Frames(UACFCameraZoneComponent* Selected, int32 Count)
		{
			for (int32 I = 0; I < Count; ++I)
			{
				Frame(Selected);
			}
		}
	};

	inline const TCHAR* EventName(EACFCameraZoneRequestEvent Event)
	{
		switch (Event)
		{
		case EACFCameraZoneRequestEvent::Requested: return TEXT("Requested");
		case EACFCameraZoneRequestEvent::Switched: return TEXT("Switched");
		case EACFCameraZoneRequestEvent::Released: return TEXT("Released");
		case EACFCameraZoneRequestEvent::Lost: return TEXT("Lost");
		case EACFCameraZoneRequestEvent::Retried: return TEXT("Retried");
		case EACFCameraZoneRequestEvent::Failed: return TEXT("Failed");
		default: return TEXT("None");
		}
	}

	inline bool ExpectEvent(FAutomationTestBase& Test, const TCHAR* What, EACFCameraZoneRequestEvent Actual, EACFCameraZoneRequestEvent Expected)
	{
		return Test.TestEqual(FString::Printf(TEXT("%s: event"), What), FString(EventName(Actual)), FString(EventName(Expected)));
	}

	inline bool PoseAt(FAutomationTestBase& Test, const TCHAR* What, const FACFCameraEvaluationResult& Result, const FVector& Expected)
	{
		return Test.TestTrue(FString::Printf(TEXT("%s: pose %s == %s"), What, *Result.Pose.Location.ToString(), *Expected.ToString()), Result.Pose.Location.Equals(Expected, 0.5));
	}
}

// No zone -> A -> B -> no zone (exploration camera below), and the return to the UE camera when nothing is below.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneRequestEnterSwitchLeaveTest, "ActionCameraFramework.Zone.Requests.EnterSwitchLeave", ACFTests::Flags)
bool FACFZoneRequestEnterSwitchLeaveTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneRequestTests;
	FRig R;
	const FACFCameraRequestHandle Explore = R.Game(R.Orbit(EACFCameraOrbitInputSource::CameraInput, 0.f), 0, TEXT("Explore"));
	R.Frames(nullptr, 2);
	TestTrue(TEXT("Exploration camera at yaw 0"), YawDelta(R.Last.Pose.Rotation.Yaw, 0.f) < 0.5f);
	UACFCameraZoneComponent* A = R.Zone(LocA, 0.4f, 0.5f);
	UACFCameraZoneComponent* B = R.Zone(LocB, 0.2f, 0.5f);

	// No zone -> A: one request at the zone priority, A's Blend In.
	ExpectEvent(*this, TEXT("Enter A"), R.Update(A), EACFCameraZoneRequestEvent::Requested);
	const FACFCameraRequestHandle HandleA = R.State.GetHandle();
	TestTrue(TEXT("A's request is the best one at once"), HandleA.IsValid() && R.Director->GetBestRequest() == HandleA);
	TestTrue(TEXT("Active Zone A"), R.State.GetActiveZone() == A);
	R.Step();
	TestTrue(TEXT("A active after the camera update"), R.Last.ActiveRequest == HandleA);
	TestEqual(TEXT("A's Blend In (0.4 s): alpha after one frame"), R.Last.BlendAlpha, 0.25f, 1e-3f);
	R.Frames(A, 4);
	PoseAt(*this, TEXT("A on screen"), R.Last, LocA);
	ExpectEvent(*this, TEXT("Staying in A"), R.Update(A), EACFCameraZoneRequestEvent::None);
	TestTrue(TEXT("Staying in A: the same request"), R.State.GetHandle() == HandleA);

	// A -> B: B requested, A released; B's Blend In (not A's Blend Out).
	ExpectEvent(*this, TEXT("A to B"), R.Update(B), EACFCameraZoneRequestEvent::Switched);
	const FACFCameraRequestHandle HandleB = R.State.GetHandle();
	TestTrue(TEXT("B has a new request"), HandleB.IsValid() && HandleB != HandleA);
	TestFalse(TEXT("A's request released"), R.Director->HasRequest(HandleA));
	TestEqual(TEXT("Exploration + B"), R.Director->GetNumRequests(), 2);
	R.Step();
	TestTrue(TEXT("B active"), R.Last.ActiveRequest == HandleB);
	TestEqual(TEXT("B's Blend In (0.2 s): alpha after one frame"), R.Last.BlendAlpha, 0.5f, 1e-3f);
	R.Frames(B, 3);
	PoseAt(*this, TEXT("B on screen"), R.Last, LocB);

	// B -> no zone: back to the exploration camera with B's Blend Out (return blend), its orbit set to B's view.
	ExpectEvent(*this, TEXT("Leave B"), R.Update(nullptr), EACFCameraZoneRequestEvent::Released);
	TestFalse(TEXT("B's request released"), R.Director->HasRequest(HandleB));
	TestFalse(TEXT("No zone request left"), R.State.GetHandle().IsValid());
	TestEqual(TEXT("Only the exploration request"), R.Director->GetNumRequests(), 1);
	R.Step();
	TestTrue(TEXT("Exploration active"), R.Last.ActiveRequest == Explore);
	TestTrue(TEXT("Return blend running"), R.Last.bIsBlending);
	TestEqual(TEXT("B's Blend Out (0.5 s) instead of the exploration camera's cut"), R.Last.BlendAlpha, 0.2f, 1e-3f);
	R.Frames(nullptr, 6);
	TestFalse(TEXT("Return blend over"), R.Last.bIsBlending);
	TestTrue(FString::Printf(TEXT("Exploration continues from B's view: yaw %.1f == 45"), R.Last.Pose.Rotation.Yaw), YawDelta(R.Last.Pose.Rotation.Yaw, 45.f) < 0.5f);
	TestTrue(FString::Printf(TEXT("... and pitch %.1f == -10"), R.Last.Pose.Rotation.Pitch), FMath::IsNearlyEqual(R.Last.Pose.Rotation.Pitch, -10.f, 0.5f));
	ExpectEvent(*this, TEXT("No zone, no request"), R.Update(nullptr), EACFCameraZoneRequestEvent::None);

	// Without any other request: zone -> no zone returns to the UE camera with the zone's Blend Out.
	R.Director->RemoveRequest(Explore);
	R.Frames(nullptr, 2);
	TestEqual(TEXT("No request"), R.Director->GetNumRequests(), 0);
	ExpectEvent(*this, TEXT("Enter A from the UE camera"), R.Update(A), EACFCameraZoneRequestEvent::Requested);
	R.Frames(A, 6);
	TestEqual(TEXT("A fully in"), R.Last.RequestWeight, 1.f, 1e-3f);
	ExpectEvent(*this, TEXT("Leave A to the UE camera"), R.Update(nullptr), EACFCameraZoneRequestEvent::Released);
	TestEqual(TEXT("No request"), R.Director->GetNumRequests(), 0);
	R.Step();
	TestFalse(TEXT("No active request"), R.Last.ActiveRequest.IsValid());
	TestEqual(TEXT("A's Blend Out (0.5 s) to the UE camera: weight after one frame"), R.Last.RequestWeight, 0.8f, 1e-3f);
	R.Frames(nullptr, 5);
	TestEqual(TEXT("Back on the UE camera"), R.Last.RequestWeight, 0.f, 1e-3f);
	return true;
}

// Rotation handoff: only the zone request (enter) or the request that takes over from the zone on screen (leave); the
// Control Rotation exploration camera gets the control rotation; Use Camera Setting changes nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneRequestRotationTest, "ActionCameraFramework.Zone.Requests.RotationHandoff", ACFTests::Flags)
bool FACFZoneRequestRotationTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneRequestTests;
	{
		// Enter an orbit zone (its own initial yaw 90) from the exploration camera at yaw 0.
		FRig R;
		R.Game(R.Orbit(EACFCameraOrbitInputSource::CameraInput, 0.f), 0);
		R.Frames(nullptr, 2);
		UACFCameraZoneComponent* Keep = R.Zone(R.Orbit(EACFCameraOrbitInputSource::CameraInput, 90.f), 0.f, 0.f);
		UACFCameraZoneComponent* Own = R.Zone(R.Orbit(EACFCameraOrbitInputSource::CameraInput, 90.f), 0.f, 0.f);
		Own->EnterRotation = EACFCameraZoneRotationHandoff::UseCameraSetting;
		R.Frames(Keep, 2);
		TestTrue(FString::Printf(TEXT("Enter, Keep View: the zone camera continues at the view's yaw (%.1f == 0)"), R.Last.Pose.Rotation.Yaw), YawDelta(R.Last.Pose.Rotation.Yaw, 0.f) < 0.5f);
		R.Frames(nullptr, 2);
		R.Frames(Own, 2);
		TestTrue(FString::Printf(TEXT("Enter, Use Camera Setting: the preset's initial yaw (%.1f == 90)"), R.Last.Pose.Rotation.Yaw), YawDelta(R.Last.Pose.Rotation.Yaw, 90.f) < 0.5f);
		TestEqual(TEXT("No control rotation change for camera-input cameras"), R.ControlRotationSets, 0);
	}
	{
		// Control Rotation exploration camera: leaving gives the control rotation the zone's view; the orbit alone would not hold.
		FRig R;
		const FACFCameraRequestHandle Explore = R.Game(R.Orbit(EACFCameraOrbitInputSource::ControlRotation, 0.f), 0);
		R.Frames(nullptr, 2);
		TestTrue(TEXT("Control Rotation camera at the control yaw 0"), YawDelta(R.Last.Pose.Rotation.Yaw, 0.f) < 0.5f);
		UACFCameraZoneComponent* A = R.Zone(LocA, 0.f, 0.f);
		R.Frames(A, 3);
		TestEqual(TEXT("Entering a fixed camera changes no control rotation"), R.ControlRotationSets, 0);
		R.Frames(nullptr, 3);
		TestTrue(TEXT("Exploration active"), R.Last.ActiveRequest == Explore);
		TestEqual(TEXT("Leaving: the control rotation was set once"), R.ControlRotationSets, 1);
		TestTrue(FString::Printf(TEXT("Control rotation = the zone's view (%s)"), *R.Driver.ControlRotation.ToString()), YawDelta(R.Driver.ControlRotation.Yaw, 45.f) < 0.01f && FMath::IsNearlyEqual(R.Driver.ControlRotation.Pitch, -10.f, 0.01f));
		TestTrue(FString::Printf(TEXT("The Control Rotation camera keeps the view (yaw %.1f == 45)"), R.Last.Pose.Rotation.Yaw), YawDelta(R.Last.Pose.Rotation.Yaw, 45.f) < 0.5f);

		// Exit Rotation = Use Camera Setting: nothing is set, the exploration camera returns to its own rotation.
		R.Driver.ControlRotation = FRotator(-15.0, 0.0, 0.0);
		UACFCameraZoneComponent* Own = R.Zone(LocA, 0.f, 0.f);
		Own->ExitRotation = EACFCameraZoneRotationHandoff::UseCameraSetting;
		R.Frames(Own, 3);
		R.Frames(nullptr, 3);
		TestEqual(TEXT("Use Camera Setting: no control rotation change"), R.ControlRotationSets, 1);
		TestTrue(FString::Printf(TEXT("Use Camera Setting: back to the control yaw (%.1f == 0)"), R.Last.Pose.Rotation.Yaw), YawDelta(R.Last.Pose.Rotation.Yaw, 0.f) < 0.5f);
	}
	return true;
}

// Lock-On above the zone: the zone switches below it (A -> B) without changing the Lock-On camera; after the Lock-On the
// camera returns to B. Leaving the zone under the Lock-On changes nothing on screen and no rotation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneRequestLockOnTest, "ActionCameraFramework.Zone.Requests.LockOnAbove", ACFTests::Flags)
bool FACFZoneRequestLockOnTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneRequestTests;
	FRig R;
	const FACFCameraRequestHandle Explore = R.Game(R.Orbit(EACFCameraOrbitInputSource::CameraInput, 0.f), 0, TEXT("Explore"));
	UACFCameraPreset* LockOnPreset = R.Orbit(EACFCameraOrbitInputSource::CameraInput, 120.f);
	UACFCameraZoneComponent* A = R.Zone(LocA, 0.f, 0.5f);
	UACFCameraZoneComponent* B = R.Zone(LocB, 0.f, 0.5f);
	R.Frames(A, 3);
	const FACFCameraRequestHandle HandleA = R.State.GetHandle();
	PoseAt(*this, TEXT("A on screen"), R.Last, LocA);

	FACFCameraRequestHandle LockOn = R.Game(LockOnPreset, 10, TEXT("LockOn"));
	R.Frames(A, 2);
	TestTrue(TEXT("Lock-On active above A"), R.Last.ActiveRequest == LockOn);
	const FRotator LockOnView = R.Last.Pose.Rotation;
	TestTrue(TEXT("Lock-On at its own yaw 120"), YawDelta(LockOnView.Yaw, 120.f) < 0.5f);

	ExpectEvent(*this, TEXT("A to B under the Lock-On"), R.Update(B), EACFCameraZoneRequestEvent::Switched);
	const FACFCameraRequestHandle HandleB = R.State.GetHandle();
	TestFalse(TEXT("A released"), R.Director->HasRequest(HandleA));
	R.Step();
	TestTrue(TEXT("Lock-On still active"), R.Last.ActiveRequest == LockOn);
	TestFalse(TEXT("No transition on screen"), R.Last.bIsBlending);
	R.Frames(B, 3);
	TestTrue(FString::Printf(TEXT("Lock-On rotation unchanged (%s)"), *R.Last.Pose.Rotation.ToString()), R.Last.Pose.Rotation.Equals(LockOnView, 0.1));

	// The game releases the Lock-On: B (not A) takes over.
	R.Director->RemoveRequest(LockOn, nullptr);
	R.Frames(B, 6);
	TestTrue(TEXT("After the Lock-On: B"), R.Last.ActiveRequest == HandleB);
	PoseAt(*this, TEXT("B on screen"), R.Last, LocB);

	// Lock-On again, then leave B below it: plain release, no rotation handoff to anything.
	LockOn = R.Game(LockOnPreset, 10, TEXT("LockOn"));
	R.Frames(B, 2);
	const FRotator LockOnView2 = R.Last.Pose.Rotation;
	ExpectEvent(*this, TEXT("Leave B under the Lock-On"), R.Update(nullptr), EACFCameraZoneRequestEvent::Released);
	R.Step();
	TestTrue(TEXT("Lock-On still active"), R.Last.ActiveRequest == LockOn);
	TestFalse(TEXT("No transition on screen"), R.Last.bIsBlending);
	TestTrue(TEXT("Lock-On rotation unchanged"), R.Last.Pose.Rotation.Equals(LockOnView2, 0.1));
	R.Director->RemoveRequest(LockOn, nullptr);
	R.Frames(nullptr, 3);
	TestTrue(TEXT("Exploration after the Lock-On"), R.Last.ActiveRequest == Explore);
	TestTrue(FString::Printf(TEXT("Exploration orbit untouched by the zone (yaw %.1f == 0)"), R.Last.Pose.Rotation.Yaw), YawDelta(R.Last.Pose.Rotation.Yaw, 0.f) < 0.5f);
	TestEqual(TEXT("No control rotation change"), R.ControlRotationSets, 0);
	return true;
}

// A finisher (priority 1000) while the zone changes: the finisher stays on screen; it returns to the new zone, or to the
// exploration camera with the finisher's own return blend when the zone was left.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneRequestFinisherTest, "ActionCameraFramework.Zone.Requests.FinisherAbove", ACFTests::Flags)
bool FACFZoneRequestFinisherTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneRequestTests;
	FRig R;
	const FACFCameraRequestHandle Explore = R.Game(R.Orbit(EACFCameraOrbitInputSource::CameraInput, 0.f), 0, TEXT("Explore"));
	UACFCameraZoneComponent* A = R.Zone(LocA, 0.f, 0.5f);
	UACFCameraZoneComponent* B = R.Zone(LocB, 0.f, 0.5f);
	UACFCameraPreset* FinisherPreset = ACFTests::MakeFixedPreset(LocFinisher, 60.f);
	R.Keep.Emplace(FinisherPreset);
	R.Frames(A, 3);

	FACFCameraSequenceHandle Finisher = R.Director->PlayFinisherPreset(FinisherPreset, nullptr, nullptr, 0.5f, FACFCameraBlendSettings::MakeCut(), LinearBlend(0.2f), 1000);
	R.Frames(A, 1);
	PoseAt(*this, TEXT("Finisher on screen"), R.Last, LocFinisher);
	ExpectEvent(*this, TEXT("A to B during the finisher"), R.Update(B), EACFCameraZoneRequestEvent::Switched);
	const FACFCameraRequestHandle HandleB = R.State.GetHandle();
	R.Step();
	PoseAt(*this, TEXT("Finisher still on screen"), R.Last, LocFinisher);
	int32 Frames = 0;
	while (R.Director->IsSequencePlaying(Finisher) && Frames++ < 20)
	{
		R.Frame(B);
	}
	TestFalse(TEXT("Finisher over"), R.Director->IsSequencePlaying(Finisher));
	TestTrue(TEXT("Back to B (not A)"), R.Last.ActiveRequest == HandleB);
	R.Frames(B, 3);
	PoseAt(*this, TEXT("B on screen"), R.Last, LocB);

	// Leave the zone during a finisher: the zone request goes below it; the finisher returns with ITS return blend (0.2 s).
	Finisher = R.Director->PlayFinisherPreset(FinisherPreset, nullptr, nullptr, 0.5f, FACFCameraBlendSettings::MakeCut(), LinearBlend(0.2f), 1000);
	R.Frames(B, 1);
	ExpectEvent(*this, TEXT("Leave B during the finisher"), R.Update(nullptr), EACFCameraZoneRequestEvent::Released);
	Frames = 0;
	while (R.Director->IsSequencePlaying(Finisher) && Frames++ < 20)
	{
		R.Frame(nullptr);
	}
	TestTrue(TEXT("Back to the exploration camera"), R.Last.ActiveRequest == Explore);
	TestEqual(TEXT("With the finisher's return blend (0.2 s), not the zone's Blend Out (0.5 s)"), R.Last.BlendAlpha, 0.5f, 1e-3f);
	TestEqual(TEXT("Only the exploration request"), R.Director->GetNumRequests(), 1);
	return true;
}

// The zone's request disappears while the player stays: requested again after 1 s, then 2 s, 4 s (never every frame);
// a request that lives for the max delay resets the delay; a zone without preset requests nothing and is not retried.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneRequestLostTest, "ActionCameraFramework.Zone.Requests.LostAndMissingPreset", ACFTests::Flags)
bool FACFZoneRequestLostTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneRequestTests;
	FRig R;
	UACFCameraPreset* ExplorePreset = R.Orbit(EACFCameraOrbitInputSource::CameraInput, 0.f);
	R.Game(ExplorePreset, 0, TEXT("Explore"));
	UACFCameraZoneComponent* A = R.Zone(LocA, 0.f, 0.f);
	UACFCameraZoneComponent* B = R.Zone(LocB, 0.f, 0.f);
	R.Frames(A, 2);
	const FACFCameraRequestHandle First = R.State.GetHandle();

	// Released by the game: lost, one retry after 1 s.
	R.Director->RemoveRequest(First);
	ExpectEvent(*this, TEXT("Lost"), R.Update(A), EACFCameraZoneRequestEvent::Lost);
	TestTrue(TEXT("Waiting for a retry"), R.State.IsWaitingForRetry());
	TestEqual(TEXT("First retry 1 s later"), R.State.GetNextRetryTime() - R.Time, 1.0, 1e-6);
	int32 Requests = 0;
	int32 Frames = 0;
	auto RunUntilRetry = [&R, &Requests, &Frames, A](int32 MaxFrames)
	{
		for (int32 I = 0; I < MaxFrames; ++I)
		{
			R.Step();
			++Frames;
			const EACFCameraZoneRequestEvent Event = R.Update(A);
			if (Event == EACFCameraZoneRequestEvent::Retried)
			{
				++Requests;
				return;
			}
		}
	};
	RunUntilRetry(30);
	TestEqual(TEXT("One new request"), Requests, 1);
	TestTrue(FString::Printf(TEXT("After 1 s (%d frames of 0.1 s)"), Frames), Frames >= 10 && Frames <= 11);
	TestTrue(TEXT("New request alive"), R.State.GetHandle().IsValid() && R.State.GetHandle() != First && R.Director->HasRequest(R.State.GetHandle()));

	// Lost again at once (Release All Cameras): 2 s; then 4 s.
	R.Director->RemoveAllRequests(false);
	R.Game(ExplorePreset, 0, TEXT("Explore")); // the game requests its exploration camera again
	ExpectEvent(*this, TEXT("Lost again"), R.Update(A), EACFCameraZoneRequestEvent::Lost);
	TestEqual(TEXT("Second delay 2 s"), R.State.GetNextRetryTime() - R.Time, 2.0, 1e-6);
	Frames = 0;
	Requests = 0;
	RunUntilRetry(40);
	TestTrue(FString::Printf(TEXT("One request after 2 s (%d frames)"), Frames), Requests == 1 && Frames >= 20 && Frames <= 21);
	R.Director->RemoveRequest(R.State.GetHandle());
	ExpectEvent(*this, TEXT("Lost a third time"), R.Update(A), EACFCameraZoneRequestEvent::Lost);
	TestEqual(TEXT("Third delay 4 s"), R.State.GetNextRetryTime() - R.Time, 4.0, 1e-6);
	TestEqual(TEXT("Losses counted"), R.State.GetNumLost(), 3);
	Frames = 0;
	Requests = 0;
	RunUntilRetry(60);
	TestEqual(TEXT("One request in 4 s"), Requests, 1);

	// Alive for the max delay (8 s): the next loss starts from 1 s again.
	R.Frames(A, 81);
	TestEqual(TEXT("Loss count reset after 8 s alive"), R.State.GetNumLost(), 0);
	R.Director->RemoveRequest(R.State.GetHandle());
	ExpectEvent(*this, TEXT("Lost after a long life"), R.Update(A), EACFCameraZoneRequestEvent::Lost);
	TestEqual(TEXT("Back to 1 s"), R.State.GetNextRetryTime() - R.Time, 1.0, 1e-6);

	// Changing the selection while waiting: B is requested at once.
	ExpectEvent(*this, TEXT("A (waiting) to B"), R.Update(B), EACFCameraZoneRequestEvent::Switched);
	TestFalse(TEXT("Not waiting any more"), R.State.IsWaitingForRetry());
	TestTrue(TEXT("B requested"), R.State.GetHandle().IsValid());

	// A zone without preset: no request, one warning, no retry while it stays selected.
	AddExpectedMessagePlain(TEXT("has no Camera Preset"), EAutomationExpectedMessageFlags::Contains, 1);
	UACFCameraZoneComponent* Missing = R.Zone(nullptr, 0.f, 0.f);
	const int32 Before = R.Director->GetNumRequests();
	ExpectEvent(*this, TEXT("Missing preset"), R.Update(Missing), EACFCameraZoneRequestEvent::Failed);
	TestTrue(TEXT("Failed"), R.State.HasFailed());
	TestEqual(TEXT("B released, nothing requested"), R.Director->GetNumRequests(), Before - 1);
	int32 Events = 0;
	for (int32 I = 0; I < 100; ++I)
	{
		R.Step();
		Events += R.Update(Missing) != EACFCameraZoneRequestEvent::None ? 1 : 0;
	}
	TestEqual(TEXT("10 s in the zone without preset: nothing happens"), Events, 0);
	TestEqual(TEXT("Still only the exploration request"), R.Director->GetNumRequests(), Before - 1);
	R.Frames(nullptr, 1);
	ExpectEvent(*this, TEXT("Enter it again"), R.Update(Missing), EACFCameraZoneRequestEvent::Failed);

	// A retry delay of 0 in the settings is raised to the minimum: never a new request every frame.
	{
		FRig Zero;
		Zero.RetryDelay = 0.f;
		UACFCameraZoneComponent* Z = Zero.Zone(LocA, 0.f, 0.f);
		Zero.Frames(Z, 2);
		Zero.Director->RemoveRequest(Zero.State.GetHandle());
		ExpectEvent(*this, TEXT("Lost with a zero delay"), Zero.Update(Z), EACFCameraZoneRequestEvent::Lost);
		TestEqual(TEXT("Zero delay: the minimum retry delay"), Zero.State.GetNextRetryTime() - Zero.Time, FACFCameraZoneRequestState::MinRetryDelay, 1e-6);
		ExpectEvent(*this, TEXT("Not in the same frame"), Zero.Update(Z), EACFCameraZoneRequestEvent::None);
	}

	// Release (World end / player gone): nothing of the zone left.
	R.Frames(A, 2);
	R.State.Release(R.Director.Get());
	TestEqual(TEXT("Release: only the exploration request"), R.Director->GetNumRequests(), 1);
	TestTrue(TEXT("Release: no zone"), R.State.GetActiveZone() == nullptr && !R.State.GetHandle().IsValid());
	return true;
}

// Interruptions: A -> B in the middle of B's blend goes to C from the pose on screen (no pop); leaving during C's blend and
// coming back during the return blend also continue from the pose on screen.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneRequestInterruptTest, "ActionCameraFramework.Zone.Requests.InterruptedBlends", ACFTests::Flags)
bool FACFZoneRequestInterruptTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneRequestTests;
	FRig R;
	const FACFCameraRequestHandle Explore = R.Game(R.Orbit(EACFCameraOrbitInputSource::CameraInput, 0.f), 0, TEXT("Explore"));
	UACFCameraZoneComponent* A = R.Zone(LocA, 0.4f, 0.4f);
	UACFCameraZoneComponent* B = R.Zone(LocB, 0.4f, 0.4f);
	UACFCameraZoneComponent* C = R.Zone(LocC, 0.4f, 0.4f);
	R.Frames(A, 6);
	PoseAt(*this, TEXT("A"), R.Last, LocA);

	R.Frames(B, 2);
	TestTrue(TEXT("A -> B blending"), R.Last.bIsBlending);
	FVector Previous = R.Last.Pose.Location;
	ExpectEvent(*this, TEXT("B to C mid-blend"), R.Update(C), EACFCameraZoneRequestEvent::Switched);
	TestEqual(TEXT("Exploration + C"), R.Director->GetNumRequests(), 2);
	R.Step();
	const FVector Expected = FMath::Lerp(Previous, LocC, 0.25);
	TestTrue(FString::Printf(TEXT("C blends from the pose on screen (%s == %s)"), *R.Last.Pose.Location.ToString(), *Expected.ToString()), R.Last.Pose.Location.Equals(Expected, 1.0));
	R.Frame(C);

	// Leave during C's blend: return blend from the pose on screen.
	Previous = R.Last.Pose.Location;
	ExpectEvent(*this, TEXT("Leave mid-blend"), R.Update(nullptr), EACFCameraZoneRequestEvent::Released);
	R.Step();
	TestTrue(TEXT("Exploration active"), R.Last.ActiveRequest == Explore);
	// One frame of the 0.4 s return blend: a quarter of the way to the exploration camera (within 400 cm of the target).
	const double MaxStep = 0.25 * (FVector::Dist(Previous, R.Target->GetComponentLocation()) + 400.0) + 1.0;
	TestTrue(FString::Printf(TEXT("No pop on leaving mid-blend (moved %.0f cm, at most %.0f)"), FVector::Dist(Previous, R.Last.Pose.Location), MaxStep), FVector::Dist(Previous, R.Last.Pose.Location) <= MaxStep);

	// Back into A during the return blend.
	Previous = R.Last.Pose.Location;
	ExpectEvent(*this, TEXT("Enter A mid return"), R.Update(A), EACFCameraZoneRequestEvent::Requested);
	R.Step();
	const FVector ExpectedA = FMath::Lerp(Previous, LocA, 0.25);
	TestTrue(FString::Printf(TEXT("A blends from the pose on screen (%s == %s)"), *R.Last.Pose.Location.ToString(), *ExpectedA.ToString()), R.Last.Pose.Location.Equals(ExpectedA, 1.0));
	R.Frames(A, 5);
	PoseAt(*this, TEXT("A at the end"), R.Last, LocA);
	TestEqual(TEXT("Exploration + A"), R.Director->GetNumRequests(), 2);
	return true;
}

// The priority contract: zone requests use the configured priority; a game request with the same priority is reported once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneRequestPriorityTest, "ActionCameraFramework.Zone.Requests.PriorityContract", ACFTests::Flags)
bool FACFZoneRequestPriorityTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneRequestTests;
	FRig R;
	R.Game(R.Orbit(EACFCameraOrbitInputSource::CameraInput, 0.f), 0, TEXT("Explore"));
	UACFCameraZoneComponent* A = R.Zone(LocA, 0.f, 0.f);
	UACFCameraZoneComponent* B = R.Zone(LocB, 0.f, 0.f);
	R.Frames(A, 2);
	TArray<FACFCameraRequestInfo> Infos;
	R.Director->GetRequestInfos(Infos);
	const FACFCameraRequestInfo* Zone = Infos.FindByPredicate([&R](const FACFCameraRequestInfo& Info) { return Info.Handle == R.State.GetHandle(); });
	TestTrue(TEXT("The zone request has the zone priority 5"), Zone && Zone->Priority == 5);

	AddExpectedMessagePlain(TEXT("has the zone request priority 5"), EAutomationExpectedMessageFlags::Contains, 1);
	R.Game(R.Orbit(EACFCameraOrbitInputSource::CameraInput, 0.f), 5, TEXT("SamePriority"));
	R.Frames(B, 2);
	R.Frames(A, 2);
	R.Frames(B, 2);
	TestEqual(TEXT("One zone request at a time"), R.Director->GetNumRequests(), 3);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
