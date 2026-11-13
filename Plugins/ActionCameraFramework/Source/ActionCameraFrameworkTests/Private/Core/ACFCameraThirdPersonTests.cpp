#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestUtils.h"
#include "Components/SceneComponent.h"
#include "Nodes/ACFCameraFraming.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_Composers.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

/** Phase 2 (Third Person) unit tests: orbit / FreeLook, input, axis damping, lookahead, teleport, composers. */
namespace ACFTPTests
{
	using namespace ACFTests;

	inline USceneComponent* MakeTarget(const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		USceneComponent* Component = NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetWorldLocationAndRotation(Location, Rotation);
		return Component;
	}

	inline UACFCameraPreset* MakePreset()
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		return Preset;
	}

	template <typename T>
	T* AddNode(UACFCameraPreset* Preset)
	{
		T* Node = NewObject<T>(Preset);
		Preset->Nodes.Add(Node);
		return Node;
	}

	inline FACFCameraRequestParams FollowParams(USceneComponent* Follow, USceneComponent* Look = nullptr)
	{
		FACFCameraRequestParams Params;
		Params.FollowTarget.Component = Follow;
		if (Look)
		{
			Params.LookAtTarget.Component = Look;
		}
		return Params;
	}

	inline FVector OrbitCamera(const FVector& Pivot, const FRotator& Rotation, float Distance, const FVector& Shoulder = FVector::ZeroVector)
	{
		return Pivot - Rotation.Vector() * Distance + Rotation.RotateVector(Shoulder);
	}

	inline FVector2D ScreenOf(const FACFCameraPose& Pose, float Aspect, const FVector& Point)
	{
		FVector2D Screen;
		double Depth;
		ACFFraming::Project(Pose.Location, Pose.Rotation, ACFFraming::MakeLens(Pose.FieldOfView, Aspect), Point, Screen, Depth);
		return Screen;
	}
}

// -------------------------------------------------------------------------------------------------
// Orbital Follow / FreeLook

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFOrbitInputTest, "ActionCameraFramework.ThirdPerson.OrbitFreeLookInputAndPitchLimits", ACFTests::Flags)
bool FACFOrbitInputTest::RunTest(const FString& Parameters)
{
	using namespace ACFTPTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector(100, 200, 0)));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_OrbitalFollow* Orbit = AddNode<UACFCameraNode_OrbitalFollow>(Preset.Get());
	Orbit->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
	Orbit->InitialYaw = 0.f;
	Orbit->InitialPitch = -10.f;
	Orbit->Sensitivity = FVector2D(2.f, 1.f);
	Orbit->PitchMin = -60.f;
	Orbit->PitchMax = 30.f;
	Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FFrameDriver Driver;
	const FVector Pivot(100, 200, 60);

	const FACFCameraPose Start = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(TEXT("Initial orbit position"), Start.Location.Equals(OrbitCamera(Pivot, FRotator(-10, 0, 0), 400.f), 0.01));
	TestTrue(TEXT("Camera looks along the orbit"), Start.Rotation.Equals(FRotator(-10, 0, 0), 0.01f));

	Director->AddCameraInput(FVector2D(10, 0));
	TestTrue(TEXT("Yaw input x sensitivity"), FMath::IsNearlyEqual(Driver.Step(Director.Get(), 1 / 60.f).Pose.Rotation.Yaw, 20.0, 0.01));
	Driver.Repeat(Director.Get(), 1 / 60.f);
	TestTrue(TEXT("Same-frame repeat does not re-apply input"), FMath::IsNearlyEqual(Driver.Repeat(Director.Get(), 1 / 60.f).Pose.Rotation.Yaw, 20.0, 0.01));
	TestTrue(TEXT("Input consumed once"), FMath::IsNearlyEqual(Driver.Step(Director.Get(), 1 / 60.f).Pose.Rotation.Yaw, 20.0, 0.01));

	Director->AddCameraInput(FVector2D(0, -100));
	TestTrue(TEXT("Pitch clamped at PitchMin"), FMath::IsNearlyEqual(Driver.Step(Director.Get(), 1 / 60.f).Pose.Rotation.Pitch, -60.0, 0.01));
	Director->AddCameraInput(FVector2D(0, 500));
	const FACFCameraPose Up = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(TEXT("Pitch clamped at PitchMax"), FMath::IsNearlyEqual(Up.Rotation.Pitch, 30.0, 0.01));
	TestTrue(TEXT("Camera stays on the orbit sphere"), FMath::IsNearlyEqual(FVector::Dist(Up.Location, Pivot), 400.0, 0.01));

	Orbit->bInvertYaw = true;
	Director->AddCameraInput(FVector2D(5, 0));
	TestTrue(TEXT("Inverted yaw"), FMath::IsNearlyEqual(Driver.Step(Director.Get(), 1 / 60.f).Pose.Rotation.Yaw, 10.0, 0.01));
	Orbit->bInvertYaw = false;

	Director->SetCameraInputRate(FVector2D(30, 0));
	TestTrue(TEXT("Rate input is multiplied by DeltaTime"), FMath::IsNearlyEqual(Driver.Step(Director.Get(), 0.5f).Pose.Rotation.Yaw, 40.0, 0.01));
	TestTrue(TEXT("Rate input must be set every frame"), FMath::IsNearlyEqual(Driver.Step(Director.Get(), 0.5f).Pose.Rotation.Yaw, 40.0, 0.01));

	// Zero-delta evaluation (PCM InitializeFor) keeps the input for the real evaluation of the frame.
	Director->AddCameraInput(FVector2D(10, 0));
	TestTrue(TEXT("Zero-dt keeps input pending"), FMath::IsNearlyEqual(Driver.Step(Director.Get(), 0.f).Pose.Rotation.Yaw, 40.0, 0.01));
	TestTrue(TEXT("Real-dt in same frame applies it once"), FMath::IsNearlyEqual(Driver.Repeat(Director.Get(), 1 / 60.f).Pose.Rotation.Yaw, 60.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFOrbitFollowTargetTest, "ActionCameraFramework.ThirdPerson.OrbitFollowsTargetMoveRotateAndRings", ACFTests::Flags)
bool FACFOrbitFollowTargetTest::RunTest(const FString& Parameters)
{
	using namespace ACFTPTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_OrbitalFollow* Orbit = AddNode<UACFCameraNode_OrbitalFollow>(Preset.Get());
	Orbit->bFollowTargetYaw = true;
	Orbit->InitialRotation = EACFCameraOrbitInitialRotation::BehindTarget;
	Orbit->InitialPitch = -15.f;
	Orbit->ShoulderOffset = FVector(0, 50, 0);
	Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FFrameDriver Driver;

	TestTrue(TEXT("Behind the target with shoulder offset"),
		Driver.Step(Director.Get(), 1 / 60.f).Pose.Location.Equals(OrbitCamera(FVector(0, 0, 60), FRotator(-15, 0, 0), 400.f, FVector(0, 50, 0)), 0.01));

	Target->SetWorldLocationAndRotation(FVector(500, -300, 100), FRotator(0, 90, 0));
	const FACFCameraPose Turned = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(TEXT("Moves with the target"), Turned.Location.Equals(OrbitCamera(FVector(500, -300, 160), FRotator(-15, 90, 0), 400.f, FVector(0, 50, 0)), 0.01));
	TestTrue(TEXT("Rotates with the target (bFollowTargetYaw)"), FMath::IsNearlyEqual(Turned.Rotation.Yaw, 90.0, 0.01));

	Director->AddCameraInput(FVector2D(30, 0));
	TestTrue(TEXT("Input yaw is relative to the target heading"), FMath::IsNearlyEqual(Driver.Step(Director.Get(), 1 / 60.f).Pose.Rotation.Yaw, 120.0, 0.01));

	// FreeLook rings: distance depends on pitch.
	Orbit->bUseRings = true;
	Orbit->TopDistance = 600.f;
	Orbit->BottomDistance = 200.f;
	TestTrue(TEXT("Ring distance at PitchMin"), FMath::IsNearlyEqual(Orbit->GetDistanceForPitch(Orbit->PitchMin), 600.f, 0.01f));
	TestTrue(TEXT("Ring distance at 0"), FMath::IsNearlyEqual(Orbit->GetDistanceForPitch(0.f), 400.f, 0.01f));
	TestTrue(TEXT("Ring distance halfway down"), FMath::IsNearlyEqual(Orbit->GetDistanceForPitch(Orbit->PitchMax * 0.5f), 300.f, 0.01f));
	Director->AddCameraInput(FVector2D(0, -200));
	const FACFCameraPose Top = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(TEXT("Camera uses the top ring"), FMath::IsNearlyEqual(FVector::Dist(Top.Location - Top.Rotation.RotateVector(FVector(0, 50, 0)), FVector(500, -300, 160)), 600.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFOrbitRecenterTest, "ActionCameraFramework.ThirdPerson.OrbitIdleRecenterAndSetRotation", ACFTests::Flags)
bool FACFOrbitRecenterTest::RunTest(const FString& Parameters)
{
	using namespace ACFTPTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector, FRotator(0, 45, 0)));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_OrbitalFollow* Orbit = AddNode<UACFCameraNode_OrbitalFollow>(Preset.Get());
	Orbit->bRecenterWhenIdle = true;
	Orbit->RecenterDelay = 1.f;
	Orbit->RecenterTime = 0.2f;
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FFrameDriver Driver;
	TestTrue(TEXT("Starts behind the target"), FMath::IsNearlyEqual(Driver.Step(Director.Get(), 1 / 60.f).Pose.Rotation.Yaw, 45.0, 0.01));

	Director->AddCameraInput(FVector2D(90, 0));
	Driver.Step(Director.Get(), 1 / 60.f);
	for (int32 I = 0; I < 30; ++I) { Driver.Step(Director.Get(), 1 / 60.f); }
	TestTrue(TEXT("No recentering before the delay"), FMath::IsNearlyEqual(Director->GetLastResult().Pose.Rotation.Yaw, 135.0, 0.01));
	for (int32 I = 0; I < 180; ++I) { Driver.Step(Director.Get(), 1 / 60.f); }
	TestTrue(TEXT("Recentered behind the target when idle"), FMath::Abs(FRotator::NormalizeAxis(Director->GetLastResult().Pose.Rotation.Yaw - 45.0)) < 1.0);
	TestTrue(TEXT("Pitch recentered"), FMath::IsNearlyEqual(Director->GetLastResult().Pose.Rotation.Pitch, Orbit->InitialPitch, 0.5f));

	TestTrue(TEXT("SetOrbitRotation finds the node"), Director->SetOrbitRotation(Handle, FRotator(-30, -90, 0), true));
	const FRotator Set = Driver.Step(Director.Get(), 1 / 60.f).Pose.Rotation;
	TestTrue(TEXT("SetOrbitRotation applied"), FMath::IsNearlyEqual(Set.Yaw, -90.0, 0.5) && FMath::IsNearlyEqual(Set.Pitch, -30.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFOrbitRotationDampingTest, "ActionCameraFramework.ThirdPerson.RotationDampingPerAxisFrameRate", ACFTests::Flags)
bool FACFOrbitRotationDampingTest::RunTest(const FString& Parameters)
{
	using namespace ACFTPTests;
	auto Run = [](float FrameRate) -> FRotator
	{
		TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
		TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
		TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
		UACFCameraNode_OrbitalFollow* Orbit = AddNode<UACFCameraNode_OrbitalFollow>(Preset.Get());
		Orbit->InitialPitch = 0.f;
		Orbit->RotationDamping = FVector2D(0.2f, 0.05f);
		Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
		FFrameDriver Driver;
		Driver.Step(Director.Get(), 1.f / FrameRate);
		Director->AddCameraInput(FVector2D(90, -40));
		const int32 Frames = FMath::RoundToInt(0.3f * FrameRate);
		for (int32 I = 0; I < Frames; ++I) { Driver.Step(Director.Get(), 1.f / FrameRate); }
		return Director->GetLastResult().Pose.Rotation;
	};
	const FRotator R30 = Run(30.f);
	const FRotator R60 = Run(60.f);
	const FRotator R120 = Run(120.f);
	const double ExpectedYaw = 90.0 * (1.0 - FMath::Exp(-0.3 / 0.2));
	AddInfo(FString::Printf(TEXT("Yaw after 0.3s: expected %.3f  30fps %.3f  60fps %.3f  120fps %.3f  (pitch %.3f / %.3f / %.3f)"), ExpectedYaw, R30.Yaw, R60.Yaw, R120.Yaw, R30.Pitch, R60.Pitch, R120.Pitch));
	TestTrue(TEXT("Yaw damping matches analytical at 30/60/120fps"),
		FMath::IsNearlyEqual(R30.Yaw, ExpectedYaw, 0.05) && FMath::IsNearlyEqual(R60.Yaw, ExpectedYaw, 0.05) && FMath::IsNearlyEqual(R120.Yaw, ExpectedYaw, 0.05));
	TestTrue(TEXT("Pitch damps independently (faster)"), R60.Pitch < -38.0 && FMath::IsNearlyEqual(R30.Pitch, R120.Pitch, 0.05));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Axis damping, teleport, lookahead

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAxisDampingTest, "ActionCameraFramework.ThirdPerson.AxisDampingFrameRateAndHitches", ACFTests::Flags)
bool FACFAxisDampingTest::RunTest(const FString& Parameters)
{
	using namespace ACFTPTests;
	const FVector Velocity(600, 300, 100);
	const FVector Times(0.3, 0.0, 0.1);
	auto Run = [&](const TArray<float>& Deltas) -> FVector
	{
		TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
		TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
		TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
		UACFCameraNode_Follow* Follow = AddNode<UACFCameraNode_Follow>(Preset.Get());
		Follow->Offset = FVector::ZeroVector;
		Follow->OffsetSpace = EACFCameraOffsetSpace::World;
		Follow->Damping.Time = Times;
		Follow->Damping.Space = EACFCameraDampingSpace::World;
		Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
		FFrameDriver Driver;
		Driver.Step(Director.Get(), 1 / 60.f);
		double T = 0.0;
		for (const float Dt : Deltas)
		{
			T += Dt;
			Target->SetWorldLocation(Velocity * T);
			Driver.Step(Director.Get(), Dt);
		}
		return Director->GetLastResult().Pose.Location;
	};
	TArray<float> Uniform;
	TArray<float> Hitchy;
	for (int32 I = 0; I < 60; ++I) { Uniform.Add(1 / 60.f); }
	// Same total time (1s) with irregular frames and a 0.25s hitch.
	const float Pattern[] = { 1 / 120.f, 1 / 30.f, 0.25f, 1 / 144.f, 1 / 20.f };
	double Total = 0.0;
	for (int32 I = 0; Total < 1.0 - 1e-6; I = (I + 1) % 5)
	{
		const float Dt = static_cast<float>(FMath::Min<double>(Pattern[I], 1.0 - Total));
		Hitchy.Add(Dt);
		Total += Dt;
	}
	const FVector A = Run(Uniform);
	const FVector B = Run(Hitchy);
	const FVector Expected(
		Velocity.X - Velocity.X * Times.X * (1.0 - FMath::Exp(-1.0 / Times.X)),
		Velocity.Y,
		Velocity.Z - Velocity.Z * Times.Z * (1.0 - FMath::Exp(-1.0 / Times.Z)));
	AddInfo(FString::Printf(TEXT("Expected %s  uniform %s  hitchy %s"), *Expected.ToString(), *A.ToString(), *B.ToString()));
	TestTrue(TEXT("Per-axis damping matches analytical (uniform 60fps)"), A.Equals(Expected, 0.5));
	TestTrue(TEXT("Per-axis damping identical with hitches"), B.Equals(Expected, 0.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTeleportTest, "ActionCameraFramework.ThirdPerson.TeleportResetsInterpolation", ACFTests::Flags)
bool FACFTeleportTest::RunTest(const FString& Parameters)
{
	using namespace ACFTPTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_Follow* Follow = AddNode<UACFCameraNode_Follow>(Preset.Get());
	Follow->Offset = FVector(-300, 0, 100);
	Follow->OffsetSpace = EACFCameraOffsetSpace::World;
	Follow->Damping = FACFCameraPositionDamping::Uniform(0.5f);
	Follow->TargetMotion.Lookahead.Time = 0.3f;
	Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FFrameDriver Driver;
	Driver.Step(Director.Get(), 1 / 60.f);

	Target->SetWorldLocation(FVector(5000, 2000, 0)); // > TeleportDistance (1000) in one frame
	TestTrue(TEXT("Detected teleport snaps (no fly-through, no lookahead spike)"),
		Driver.Step(Director.Get(), 1 / 60.f).Pose.Location.Equals(FVector(4700, 2000, 100), 0.01));

	Follow->TargetMotion.TeleportDistance = 0.f; // detection off: only NotifyTeleport helps
	Target->SetWorldLocation(FVector(0, 0, 0));
	TestFalse(TEXT("Without detection the damped camera lags behind"), Driver.Step(Director.Get(), 1 / 60.f).Pose.Location.Equals(FVector(-300, 0, 100), 1.0));
	Director->NotifyTeleport(true);
	TestTrue(TEXT("NotifyTeleport snaps"), Driver.Step(Director.Get(), 1 / 60.f).Pose.Location.Equals(FVector(-300, 0, 100), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLookaheadTest, "ActionCameraFramework.ThirdPerson.LookaheadSteadyAndReversal", ACFTests::Flags)
bool FACFLookaheadTest::RunTest(const FString& Parameters)
{
	using namespace ACFTPTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_Follow* Follow = AddNode<UACFCameraNode_Follow>(Preset.Get());
	Follow->Offset = FVector::ZeroVector;
	Follow->OffsetSpace = EACFCameraOffsetSpace::World;
	Follow->TargetMotion.Lookahead.Time = 0.5f;
	Follow->TargetMotion.Lookahead.Smoothing = 0.15f;
	Follow->TargetMotion.Lookahead.MaxDistance = 1000.f;
	Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FFrameDriver Driver;
	Driver.Step(Director.Get(), 1 / 60.f);

	constexpr double Speed = 600.0;
	constexpr double Steady = Speed * 0.5;
	double X = 0.0;
	double PrevOffset = 0.0;
	double MaxOffset = 0.0;
	double MaxStep = 0.0;
	for (int32 I = 0; I < 90; ++I) // 1.5s forward
	{
		X += Speed / 60.0;
		Target->SetWorldLocation(FVector(X, 0, 0));
		const double Offset = Driver.Step(Director.Get(), 1 / 60.f).Pose.Location.X - X;
		MaxOffset = FMath::Max(MaxOffset, Offset);
		MaxStep = FMath::Max(MaxStep, FMath::Abs(Offset - PrevOffset));
		PrevOffset = Offset;
	}
	TestTrue(FString::Printf(TEXT("Steady lead = speed * time (%.2f)"), PrevOffset), FMath::IsNearlyEqual(PrevOffset, Steady, Steady * 0.01));

	int32 FramesToFlip = -1;
	for (int32 I = 0; I < 90; ++I) // sudden reversal
	{
		X -= Speed / 60.0;
		Target->SetWorldLocation(FVector(X, 0, 0));
		const double Offset = Driver.Step(Director.Get(), 1 / 60.f).Pose.Location.X - X;
		MaxOffset = FMath::Max(MaxOffset, Offset);
		MaxStep = FMath::Max(MaxStep, FMath::Abs(Offset - PrevOffset));
		if (FramesToFlip < 0 && Offset < 0.0)
		{
			FramesToFlip = I + 1;
		}
		PrevOffset = Offset;
	}
	AddInfo(FString::Printf(TEXT("Lookahead: max lead %.2f, max per-frame change %.2f, frames to flip after reversal %d"), MaxOffset, MaxStep, FramesToFlip));
	TestTrue(TEXT("Never predicts beyond speed * time (no runaway)"), MaxOffset <= Steady + 1.0);
	TestTrue(TEXT("Prediction flips quickly after a reversal (< 0.25s)"), FramesToFlip > 0 && FramesToFlip <= 15);
	TestTrue(TEXT("Prediction moves continuously (no jump > half the lead per frame at 60fps)"), MaxStep < Steady * 0.5);
	TestTrue(TEXT("Steady lead in the new direction"), FMath::IsNearlyEqual(PrevOffset, -Steady, Steady * 0.01));

	Follow->TargetMotion.Lookahead.MaxDistance = 100.f;
	for (int32 I = 0; I < 30; ++I)
	{
		X -= Speed / 60.0;
		Target->SetWorldLocation(FVector(X, 0, 0));
		Driver.Step(Director.Get(), 1 / 60.f);
	}
	TestTrue(TEXT("MaxDistance clamps the lead"), FMath::IsNearlyEqual(Director->GetLastResult().Pose.Location.X - X, -100.0, 0.01));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Composers

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFRotationComposerTest, "ActionCameraFramework.ThirdPerson.RotationComposerZonesFovAspect", ACFTests::Flags)
bool FACFRotationComposerTest::RunTest(const FString& Parameters)
{
	using namespace ACFTPTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Look(MakeTarget(FVector(1000, 200, 0)));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_FixedPose* Fixed = AddNode<UACFCameraNode_FixedPose>(Preset.Get());
	Fixed->Location = FVector::ZeroVector;
	Fixed->bOverrideRotation = false;
	Fixed->FieldOfView = 90.f;
	UACFCameraNode_RotationComposer* Composer = AddNode<UACFCameraNode_RotationComposer>(Preset.Get());
	Composer->Composition.ScreenPosition = FVector2D(0.1, 0.05);
	Composer->Composition.DeadZoneSize = FVector2D(0.1, 0.1);
	Composer->Composition.SoftZoneSize = FVector2D(0.5, 0.5);
	Composer->Damping = FVector2D(0.3, 0.3);
	FACFCameraRequestParams Params;
	Params.LookAtTarget.Component = Look.Get();
	Director->AddRequest(Preset.Get(), Params);
	FFrameDriver Driver;
	const float Aspect = Driver.AspectRatio;

	FACFCameraPose Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(TEXT("Activation frames the target at the screen position"), ScreenOf(Pose, Aspect, Look->GetComponentLocation()).Equals(FVector2D(0.1, 0.05), 1e-3));
	TestEqual(TEXT("No roll"), Pose.Rotation.Roll, 0.0);

	// Small move inside the dead zone: the camera does not rotate.
	const FRotator Before = Pose.Rotation;
	Look->SetWorldLocation(FVector(1000, 260, 0));
	Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(TEXT("Inside the dead zone: no correction"), Pose.Rotation.Equals(Before, 1e-3f));

	// Jump far to the side: hard-limited to the soft zone this very frame.
	Look->SetWorldLocation(FVector(1000, 1100, 250)); // 880cm: below TeleportDistance (a teleport would re-frame exactly)
	bool bAlwaysInSoftZone = true;
	for (int32 I = 0; I < 120; ++I)
	{
		Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
		const FVector2D S = ScreenOf(Pose, Aspect, Look->GetComponentLocation());
		bAlwaysInSoftZone &= FMath::Abs(S.X - 0.1) <= 0.25 + 1e-3 && FMath::Abs(S.Y - 0.05) <= 0.25 + 1e-3;
	}
	const FVector2D Settled = ScreenOf(Pose, Aspect, Look->GetComponentLocation());
	AddInfo(FString::Printf(TEXT("Settled screen position %s"), *Settled.ToString()));
	TestTrue(TEXT("Target never leaves the soft zone"), bAlwaysInSoftZone);
	TestTrue(TEXT("Settles at the dead zone edge"), FMath::IsNearlyEqual(Settled.X, 0.15, 2e-3) && FMath::IsNearlyEqual(Settled.Y, 0.1, 2e-3));

	// Exact framing (no dead zone, no damping) survives FOV and aspect changes.
	Composer->Composition.DeadZoneSize = FVector2D::ZeroVector;
	Composer->Damping = FVector2D::ZeroVector;
	Fixed->FieldOfView = 60.f;
	Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(TEXT("FOV change keeps the composition"), ScreenOf(Pose, Aspect, Look->GetComponentLocation()).Equals(FVector2D(0.1, 0.05), 1e-3));
	Driver.AspectRatio = 4.f / 3.f;
	Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(TEXT("Aspect change keeps the composition"), ScreenOf(Pose, 4.f / 3.f, Look->GetComponentLocation()).Equals(FVector2D(0.1, 0.05), 1e-3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFOrbitWithComposerTest, "ActionCameraFramework.ThirdPerson.OrbitPlusRotationComposerNoFight", ACFTests::Flags)
bool FACFOrbitWithComposerTest::RunTest(const FString& Parameters)
{
	using namespace ACFTPTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Player(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_OrbitalFollow* Orbit = AddNode<UACFCameraNode_OrbitalFollow>(Preset.Get());
	Orbit->InitialPitch = -10.f;
	UACFCameraNode_RotationComposer* Composer = AddNode<UACFCameraNode_RotationComposer>(Preset.Get());
	Composer->TargetSource = EACFCameraLookAtSource::FollowTarget;
	Composer->TargetOffset = FVector(0, 0, 60);
	Composer->Composition.ScreenPosition = FVector2D(-0.15, 0.0); // character a bit left of centre
	Composer->Damping = FVector2D(1.0, 1.0);                     // deliberately slow framing
	Director->AddRequest(Preset.Get(), FollowParams(Player.Get()));
	FFrameDriver Driver;
	const FRotator Start = Driver.Step(Director.Get(), 1 / 60.f).Pose.Rotation;
	TestTrue(TEXT("Composer frames the character left of centre"),
		ScreenOf(Director->GetLastResult().Pose, Driver.AspectRatio, FVector(0, 0, 60)).Equals(FVector2D(-0.15, 0.0), 2e-3));

	Director->AddCameraInput(FVector2D(40, 0));
	const FACFCameraPose After = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(TEXT("Orbit input turns the camera immediately despite slow composer damping"),
		FMath::IsNearlyEqual(FRotator::NormalizeAxis(After.Rotation.Yaw - Start.Yaw), 40.0, 0.5));
	TestTrue(TEXT("Framing preserved while orbiting"),
		ScreenOf(After, Driver.AspectRatio, FVector(0, 0, 60)).Equals(FVector2D(-0.15, 0.0), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPositionComposerTest, "ActionCameraFramework.ThirdPerson.PositionComposerZonesAndDistance", ACFTests::Flags)
bool FACFPositionComposerTest::RunTest(const FString& Parameters)
{
	using namespace ACFTPTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	UACFCameraNode_FixedPose* Fixed = AddNode<UACFCameraNode_FixedPose>(Preset.Get());
	Fixed->bOverrideLocation = false;
	Fixed->Rotation = FRotator(-30, 0, 0);
	Fixed->FieldOfView = 70.f;
	UACFCameraNode_PositionComposer* Composer = AddNode<UACFCameraNode_PositionComposer>(Preset.Get());
	Composer->CameraDistance = 800.f;
	Composer->Composition.ScreenPosition = FVector2D(0.0, -0.1);
	Composer->Composition.DeadZoneSize = FVector2D(0.1, 0.1);
	Composer->Composition.SoftZoneSize = FVector2D(0.5, 0.5);
	Composer->Damping = FVector(0.3, 0.3, 0.3);
	Director->AddRequest(Preset.Get(), FollowParams(Target.Get()));
	FFrameDriver Driver;
	const float Aspect = Driver.AspectRatio;
	auto Depth = [](const FACFCameraPose& Pose, const FVector& P) { return Pose.Rotation.UnrotateVector(P - Pose.Location).X; };

	FACFCameraPose Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	TestTrue(TEXT("Activation: target at screen position"), ScreenOf(Pose, Aspect, FVector::ZeroVector).Equals(FVector2D(0.0, -0.1), 1e-3));
	TestTrue(TEXT("Activation: camera distance"), FMath::IsNearlyEqual(Depth(Pose, FVector::ZeroVector), 800.0, 0.01));
	TestTrue(TEXT("Position Composer never rotates"), Pose.Rotation.Equals(FRotator(-30, 0, 0), 1e-3f));

	Target->SetWorldLocation(FVector(300, 900, 0));
	bool bAlwaysInSoftZone = true;
	for (int32 I = 0; I < 180; ++I)
	{
		Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
		const FVector2D S = ScreenOf(Pose, Aspect, Target->GetComponentLocation());
		bAlwaysInSoftZone &= FMath::Abs(S.X) <= 0.25 + 1e-3 && FMath::Abs(S.Y + 0.1) <= 0.25 + 1e-3;
	}
	const FVector2D Settled = ScreenOf(Pose, Aspect, Target->GetComponentLocation());
	AddInfo(FString::Printf(TEXT("Settled %s depth %.2f"), *Settled.ToString(), Depth(Pose, Target->GetComponentLocation())));
	TestTrue(TEXT("Target never leaves the soft zone"), bAlwaysInSoftZone);
	TestTrue(TEXT("Settles inside the dead zone"), FMath::Abs(Settled.X) <= 0.05 + 2e-3 && FMath::Abs(Settled.Y + 0.1) <= 0.05 + 2e-3);
	TestTrue(TEXT("Distance restored"), FMath::IsNearlyEqual(Depth(Pose, Target->GetComponentLocation()), 800.0, 1.0));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetValidationTest, "ActionCameraFramework.ThirdPerson.PresetValidationConflicts", ACFTests::Flags)
bool FACFPresetValidationTest::RunTest(const FString& Parameters)
{
	using namespace ACFTPTests;
	TStrongObjectPtr<UACFCameraPreset> Good(MakePreset());
	AddNode<UACFCameraNode_OrbitalFollow>(Good.Get());
	AddNode<UACFCameraNode_RotationComposer>(Good.Get());
	AddNode<UACFCameraNode_Collision>(Good.Get());
	FDataValidationContext GoodContext;
	Good->IsDataValid(GoodContext);
	TestEqual(TEXT("Orbit + Rotation Composer + Collision is a valid stack"), GoodContext.GetNumWarnings(), 0);

	TStrongObjectPtr<UACFCameraPreset> Bad(MakePreset());
	AddNode<UACFCameraNode_Follow>(Bad.Get());
	AddNode<UACFCameraNode_PositionComposer>(Bad.Get());
	AddNode<UACFCameraNode_LookAt>(Bad.Get());
	FDataValidationContext BadContext;
	Bad->IsDataValid(BadContext);
	TestEqual(TEXT("Follow + Position Composer + LookAt warns twice"), BadContext.GetNumWarnings(), 2);
	return true;
}
#endif

#endif // WITH_DEV_AUTOMATION_TESTS
