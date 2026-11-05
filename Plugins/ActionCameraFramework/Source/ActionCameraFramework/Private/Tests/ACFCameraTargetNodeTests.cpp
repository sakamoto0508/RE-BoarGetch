#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestUtils.h"
#include "Components/SceneComponent.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Follow / LookAt / damping / target lifetime / live blend tests. Targets are bare
 * USceneComponents (no world needed); actor targets are covered by the in-game integration test.
 */
namespace ACFNodeTests
{
	using namespace ACFTests;

	inline USceneComponent* MakeTarget(const FVector& Location)
	{
		USceneComponent* Component = NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetWorldLocation(Location);
		return Component;
	}

	inline FACFCameraTarget TargetOf(USceneComponent* Component)
	{
		FACFCameraTarget Target;
		Target.Component = Component;
		return Target;
	}

	inline UACFCameraNode_Follow* AddFollow(UACFCameraPreset* Preset, const FVector& Offset, EACFCameraOffsetSpace Space, float Damping)
	{
		UACFCameraNode_Follow* Node = NewObject<UACFCameraNode_Follow>(Preset);
		Node->Offset = Offset;
		Node->OffsetSpace = Space;
		Node->DampingTime = Damping;
		Preset->Nodes.Add(Node);
		return Node;
	}

	inline UACFCameraNode_LookAt* AddLookAt(UACFCameraPreset* Preset, float Damping = 0.f)
	{
		UACFCameraNode_LookAt* Node = NewObject<UACFCameraNode_LookAt>(Preset);
		Node->DampingTime = Damping;
		Preset->Nodes.Add(Node);
		return Node;
	}

	inline UACFCameraPreset* MakePreset(const FACFCameraBlendSettings& Blend = FACFCameraBlendSettings::MakeCut())
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		Preset->DefaultBlendIn = Blend;
		Preset->DefaultBlendOut = Blend;
		return Preset;
	}

	inline FACFCameraBlendSettings LinearFrom(float Seconds, EACFCameraBlendSource Source)
	{
		FACFCameraBlendSettings Blend = Linear(Seconds);
		Blend.Source = Source;
		return Blend;
	}

	/** Moves Target at Velocity for Duration seconds at the given frame rate; returns the camera X. */
	inline double RunConstantVelocity(float FrameRate, float DampingTime, double Velocity, double Duration, double StopAt)
	{
		TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
		TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
		TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
		AddFollow(Preset.Get(), FVector::ZeroVector, EACFCameraOffsetSpace::World, DampingTime);

		FACFCameraRequestParams Params;
		Params.FollowTarget = TargetOf(Target.Get());
		Director->AddRequest(Preset.Get(), Params);

		FFrameDriver Driver;
		Driver.Step(Director.Get(), 1.f / FrameRate); // t = 0: activation snaps to the target
		const int32 Frames = FMath::RoundToInt(Duration * FrameRate);
		const float Dt = 1.f / FrameRate;
		for (int32 Frame = 1; Frame <= Frames; ++Frame)
		{
			const double T = Frame * static_cast<double>(Dt);
			Target->SetWorldLocation(FVector(Velocity * FMath::Min(T, StopAt), 0.0, 0.0));
			Driver.Step(Director.Get(), Dt);
		}
		return Director->GetLastResult().Pose.Location.X;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFFollowMovingTargetTest, "ActionCameraFramework.Nodes.FollowMovingTargetAndOffsetSpace", ACFTests::Flags)
bool FACFFollowMovingTargetTest::RunTest(const FString& Parameters)
{
	using namespace ACFNodeTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector(100, 0, 0)));
	TStrongObjectPtr<UACFCameraPreset> LocalPreset(MakePreset());
	TStrongObjectPtr<UACFCameraPreset> WorldPreset(MakePreset());
	AddFollow(LocalPreset.Get(), FVector(-300, 0, 100), EACFCameraOffsetSpace::TargetLocal, 0.f);
	AddFollow(WorldPreset.Get(), FVector(-300, 0, 100), EACFCameraOffsetSpace::World, 0.f);

	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Target.Get());
	FACFCameraRequestHandle LocalHandle = Director->AddRequest(LocalPreset.Get(), Params);
	FFrameDriver Driver;

	Target->SetWorldRotation(FRotator(0, 90, 0)); // facing +Y: local "behind" is -Y
	TestTrue(TEXT("Local offset rotates with target"), Driver.Step(Director.Get(), 1 / 60.f).Pose.Location.Equals(FVector(100, -300, 100), 0.01));

	Target->SetWorldLocation(FVector(500, 200, 0));
	TestTrue(TEXT("Follows a moving target"), Driver.Step(Director.Get(), 1 / 60.f).Pose.Location.Equals(FVector(500, -100, 100), 0.01));

	Director->RemoveRequest(LocalHandle);
	Director->AddRequest(WorldPreset.Get(), Params);
	TestTrue(TEXT("World offset ignores target rotation"), Driver.Step(Director.Get(), 1 / 60.f).Pose.Location.Equals(FVector(200, 200, 100), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFFollowLookAtSeparateTargetsTest, "ActionCameraFramework.Nodes.FollowAndLookAtSeparateTargets", ACFTests::Flags)
bool FACFFollowLookAtSeparateTargetsTest::RunTest(const FString& Parameters)
{
	using namespace ACFNodeTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> FollowTarget(MakeTarget(FVector(0, 0, 0)));
	TStrongObjectPtr<USceneComponent> LookTarget(MakeTarget(FVector(1000, 1000, 0)));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	AddFollow(Preset.Get(), FVector(0, 0, 500), EACFCameraOffsetSpace::World, 0.f);
	AddLookAt(Preset.Get());

	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(FollowTarget.Get());
	Params.LookAtTarget = TargetOf(LookTarget.Get());
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), Params);
	FFrameDriver Driver;

	const FACFCameraPose& Pose = Driver.Step(Director.Get(), 1 / 60.f).Pose;
	const FRotator Expected = (FVector(1000, 1000, 0) - FVector(0, 0, 500)).Rotation();
	TestTrue(TEXT("Camera sits at follow target + offset"), Pose.Location.Equals(FVector(0, 0, 500), 0.01));
	TestTrue(TEXT("Camera aims at the separate look-at target"), Pose.Rotation.Equals(Expected, 0.01f));
	TestEqual(TEXT("No roll"), Pose.Rotation.Roll, 0.0);

	// Swap only the look-at target at runtime.
	TStrongObjectPtr<USceneComponent> Other(MakeTarget(FVector(-1000, 0, 500)));
	TestTrue(TEXT("SetRequestTargets"), Director->SetRequestTargets(Handle, TargetOf(FollowTarget.Get()), TargetOf(Other.Get())));
	TestTrue(TEXT("Looks at the new target"), Driver.Step(Director.Get(), 1 / 60.f).Pose.Rotation.Equals(FRotator(0, 180, 0), 0.01f));

	// No look-at target: LookAt does nothing, upstream rotation shows through.
	TestTrue(TEXT("Clear look-at"), Director->SetRequestTargets(Handle, TargetOf(FollowTarget.Get()), FACFCameraTarget()));
	Driver.Upstream.Rotation = FRotator(-5, 33, 0);
	TestTrue(TEXT("Without target the node passes through"), Driver.Step(Director.Get(), 1 / 60.f).Pose.Rotation.Equals(FRotator(-5, 33, 0), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTargetLifetimeTest, "ActionCameraFramework.Nodes.TargetLifetime", ACFTests::Flags)
bool FACFTargetLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace ACFNodeTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraPreset> HoldPreset(MakePreset());
	TStrongObjectPtr<UACFCameraPreset> PassPreset(MakePreset());
	AddFollow(HoldPreset.Get(), FVector(0, 0, 100), EACFCameraOffsetSpace::World, 0.5f);
	AddLookAt(HoldPreset.Get());
	UACFCameraNode_Follow* PassFollow = AddFollow(PassPreset.Get(), FVector(0, 0, 100), EACFCameraOffsetSpace::World, 0.f);
	PassFollow->TargetLostBehavior = EACFCameraTargetLostBehavior::PassThrough;

	USceneComponent* Doomed = MakeTarget(FVector(300, 0, 0));
	USceneComponent* LookDoomed = MakeTarget(FVector(300, 500, 0));
	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Doomed);
	Params.LookAtTarget = TargetOf(LookDoomed);
	FACFCameraRequestHandle Hold = Director->AddRequest(HoldPreset.Get(), Params);
	FFrameDriver Driver;
	Driver.Upstream.Location = FVector(-77, -77, -77);
	const FACFCameraPose Before = Driver.Step(Director.Get(), 1 / 60.f).Pose;

	Doomed->MarkAsGarbage();
	LookDoomed->MarkAsGarbage();
	const FACFCameraPose After = Driver.Step(Director.Get(), 1 / 60.f).Pose; // must not crash
	TestTrue(TEXT("HoldLastKnown keeps the follow point"), After.Location.Equals(Before.Location, 0.01));
	TestTrue(TEXT("HoldLastKnown keeps the aim"), After.Rotation.Equals(Before.Rotation, 0.01f));
	Driver.Step(Director.Get(), 1.f);

	Director->RemoveRequest(Hold);
	USceneComponent* Doomed2 = MakeTarget(FVector(300, 0, 0));
	Params.FollowTarget = TargetOf(Doomed2);
	Director->AddRequest(PassPreset.Get(), Params);
	TestTrue(TEXT("PassThrough follows while alive"), Driver.Step(Director.Get(), 1 / 60.f).Pose.Location.Equals(FVector(300, 0, 100), 0.01));
	Doomed2->MarkAsGarbage();
	TestTrue(TEXT("PassThrough falls back to upstream"), Driver.Step(Director.Get(), 1 / 60.f).Pose.Location.Equals(Driver.Upstream.Location, 0.01));

	// A target that was never alive: node does nothing, no crash.
	USceneComponent* NeverAlive = MakeTarget(FVector::ZeroVector);
	NeverAlive->MarkAsGarbage();
	Params.FollowTarget = TargetOf(NeverAlive);
	Director->AddRequest(HoldPreset.Get(), Params);
	TestTrue(TEXT("Dead-on-arrival target passes through"), Driver.Step(Director.Get(), 1 / 60.f).Pose.Location.Equals(Driver.Upstream.Location, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDampingFrameRateTest, "ActionCameraFramework.Nodes.DampingFrameRateIndependence", ACFTests::Flags)
bool FACFDampingFrameRateTest::RunTest(const FString& Parameters)
{
	using namespace ACFNodeTests;
	constexpr float Damping = 0.25f;
	constexpr double Velocity = 600.0;

	// Constant velocity for 1s: analytical trail = V*T*(1 - e^(-t/T)).
	const double Expected = Velocity * 1.0 - Velocity * Damping * (1.0 - FMath::Exp(-1.0 / Damping));
	const double X30 = RunConstantVelocity(30.f, Damping, Velocity, 1.0, 10.0);
	const double X60 = RunConstantVelocity(60.f, Damping, Velocity, 1.0, 10.0);
	const double X120 = RunConstantVelocity(120.f, Damping, Velocity, 1.0, 10.0);
	AddInfo(FString::Printf(TEXT("Moving target: expected %.3f  30fps %.3f  60fps %.3f  120fps %.3f"), Expected, X30, X60, X120));
	TestTrue(TEXT("30fps matches analytical"), FMath::IsNearlyEqual(X30, Expected, 0.5));
	TestTrue(TEXT("60fps matches analytical"), FMath::IsNearlyEqual(X60, Expected, 0.5));
	TestTrue(TEXT("120fps matches analytical"), FMath::IsNearlyEqual(X120, Expected, 0.5));
	TestTrue(TEXT("Damping actually lags"), X60 < Velocity - 100.0);

	// Start / stop: target stops at t = 0.5 and the camera settles.
	const double S30 = RunConstantVelocity(30.f, Damping, Velocity, 1.5, 0.5);
	const double S60 = RunConstantVelocity(60.f, Damping, Velocity, 1.5, 0.5);
	const double S120 = RunConstantVelocity(120.f, Damping, Velocity, 1.5, 0.5);
	AddInfo(FString::Printf(TEXT("Stopping target: 30fps %.3f  60fps %.3f  120fps %.3f"), S30, S60, S120));
	TestTrue(TEXT("Stop: 30 vs 120 fps"), FMath::IsNearlyEqual(S30, S120, 0.5));
	TestTrue(TEXT("Stop: 60 vs 120 fps"), FMath::IsNearlyEqual(S60, S120, 0.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDampingFrameGuardTest, "ActionCameraFramework.Nodes.DampingNotDoubleStepped", ACFTests::Flags)
bool FACFDampingFrameGuardTest::RunTest(const FString& Parameters)
{
	using namespace ACFNodeTests;
	TStrongObjectPtr<UACFCameraDirector> A(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraDirector> B(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset());
	AddFollow(Preset.Get(), FVector::ZeroVector, EACFCameraOffsetSpace::World, 0.3f);
	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Target.Get());
	A->AddRequest(Preset.Get(), Params);
	B->AddRequest(Preset.Get(), Params);
	FFrameDriver DriverA;
	FFrameDriver DriverB;
	DriverA.Step(A.Get(), 1 / 60.f);
	DriverB.Step(B.Get(), 1 / 60.f);

	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Target->SetWorldLocation(FVector(10.0 * (Frame + 1), 0, 0));
		DriverA.Step(A.Get(), 1 / 60.f);
		// B: same frames, but the PCM calls the modifier 3x per frame (view-target blend + InitializeFor-like zero delta).
		DriverB.Step(B.Get(), 0.f);
		DriverB.Repeat(B.Get(), 1 / 60.f);
		DriverB.Repeat(B.Get(), 1 / 60.f);
	}
	TestTrue(TEXT("Repeated same-frame calls do not double-step damping"),
		A->GetLastResult().Pose.Location.Equals(B->GetLastResult().Pose.Location, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLiveBlendTest, "ActionCameraFramework.Director.LiveAndSnapshotBlend", ACFTests::Flags)
bool FACFLiveBlendTest::RunTest(const FString& Parameters)
{
	using namespace ACFNodeTests;
	for (const EACFCameraBlendSource Source : { EACFCameraBlendSource::Live, EACFCameraBlendSource::Snapshot })
	{
		const bool bLive = Source == EACFCameraBlendSource::Live;
		const FString Mode = bLive ? TEXT("Live") : TEXT("Snapshot");
		TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
		TStrongObjectPtr<USceneComponent> TargetA(MakeTarget(FVector(0, 0, 0)));
		TStrongObjectPtr<USceneComponent> TargetB(MakeTarget(FVector(1000, 0, 0)));
		TStrongObjectPtr<UACFCameraPreset> PresetA(MakePreset(LinearFrom(1.f, Source)));
		TStrongObjectPtr<UACFCameraPreset> PresetB(MakePreset(LinearFrom(1.f, Source)));
		AddFollow(PresetA.Get(), FVector::ZeroVector, EACFCameraOffsetSpace::World, 0.f);
		AddFollow(PresetB.Get(), FVector::ZeroVector, EACFCameraOffsetSpace::World, 0.f);
		FACFCameraRequestParams ParamsA;
		ParamsA.FollowTarget = TargetOf(TargetA.Get());
		FACFCameraRequestParams ParamsB;
		ParamsB.FollowTarget = TargetOf(TargetB.Get());

		FFrameDriver Driver;
		Director->AddRequest(PresetA.Get(), ParamsA);
		Driver.Step(Director.Get(), 2.f); // fully on A

		Director->AddRequest(PresetB.Get(), ParamsB);
		Driver.Step(Director.Get(), 0.f); // transition starts with A at X=0
		TargetA->SetWorldLocation(FVector(200, 0, 0));
		TargetB->SetWorldLocation(FVector(1100, 0, 0));
		const FACFCameraEvaluationResult& Half = Driver.Step(Director.Get(), 0.5f);
		const double ExpectedX = bLive ? FMath::Lerp(200.0, 1100.0, 0.5) : FMath::Lerp(0.0, 1100.0, 0.5);
		TestTrue(FString::Printf(TEXT("%s: blend uses %s outgoing pose"), *Mode, bLive ? TEXT("live") : TEXT("frozen")), FMath::IsNearlyEqual(Half.Pose.Location.X, ExpectedX, 0.01));
		TestEqual(FString::Printf(TEXT("%s: rigs evaluated this frame"), *Mode), Director->GetLastRigEvaluationCount(), bLive ? 2 : 1);

		// Interrupt mid-blend with C: first frame must equal the on-screen pose (no jump), then move smoothly.
		const FVector OnScreen = Half.Pose.Location;
		TStrongObjectPtr<UACFCameraPreset> PresetC(ACFTests::MakeFixedPreset(FVector(0, 3000, 0), 90.f, LinearFrom(1.f, Source)));
		Director->AddRequest(PresetC.Get(), FACFCameraRequestParams());
		TestTrue(FString::Printf(TEXT("%s: no jump on interruption"), *Mode), Driver.Step(Director.Get(), 0.f).Pose.Location.Equals(OnScreen, 0.01));
		const FVector Next = Driver.Step(Director.Get(), 1 / 60.f).Pose.Location;
		const double Gap = FVector::Dist(OnScreen, FVector(0, 3000, 0));
		TestTrue(FString::Printf(TEXT("%s: first interrupted frame moves ~1/60 of the gap"), *Mode), FVector::Dist(OnScreen, Next) <= Gap / 60.0 + 1.0);
	}

	// Releasing a live-followed camera: it keeps tracking its target while fading back to UE.
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector(0, 0, 0)));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakePreset(LinearFrom(1.f, EACFCameraBlendSource::Live)));
	AddFollow(Preset.Get(), FVector::ZeroVector, EACFCameraOffsetSpace::World, 0.f);
	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Target.Get());
	FFrameDriver Driver;
	FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), Params);
	Driver.Step(Director.Get(), 2.f);
	Director->RemoveRequest(Handle);
	Target->SetWorldLocation(FVector(400, 0, 0));
	const FACFCameraEvaluationResult& Out = Driver.Step(Director.Get(), 0.5f);
	TestTrue(TEXT("Released live camera keeps tracking during blend-out"), FMath::IsNearlyEqual(Out.Pose.Location.X, 400.0, 0.01));
	TestTrue(TEXT("Blend-out weight"), FMath::IsNearlyEqual(Out.RequestWeight, 0.5f, 1e-4f));
	Driver.Step(Director.Get(), 0.6f);
	TestEqual(TEXT("Blend-out completes"), Director->GetLastResult().Weight, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSuppressionAndAimTest, "ActionCameraFramework.Director.OutputSuppressionAndAimPose", ACFTests::Flags)
bool FACFSuppressionAndAimTest::RunTest(const FString& Parameters)
{
	using namespace ACFNodeTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraPreset> Preset(ACFTests::MakeFixedPreset(FVector(100, 0, 0), 60.f, Linear(1.f)));
	FFrameDriver Driver;
	Driver.Upstream.Location = FVector(0, 0, 0);
	Driver.Upstream.FieldOfView = 90.f;
	Director->AddRequest(Preset.Get(), FACFCameraRequestParams());

	const FACFCameraEvaluationResult& Half = Driver.Step(Director.Get(), 0.5f);
	TestTrue(TEXT("Aim pose = upstream blended by weight"), Half.AimPose.Location.Equals(FVector(50, 0, 0), 0.01));
	TestTrue(TEXT("Aim FOV blended"), FMath::IsNearlyEqual(Half.AimPose.FieldOfView, 75.f, 0.01f));
	Driver.Step(Director.Get(), 1.f);

	TStrongObjectPtr<UACFCameraPreset> Source(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
	Director->SuppressOutput(Source.Get(), 0.f);
	const FACFCameraEvaluationResult& Suppressed = Driver.Step(Director.Get(), 1 / 60.f);
	TestEqual(TEXT("Suppressed output weight"), Suppressed.Weight, 0.f);
	TestEqual(TEXT("Request weight kept while suppressed"), Suppressed.RequestWeight, 1.f);
	TestTrue(TEXT("Reported as suppressed"), Suppressed.bOutputSuppressed);
	TestTrue(TEXT("Rig still evaluated while suppressed"), Suppressed.Pose.Location.Equals(FVector(100, 0, 0), 0.01));
	TestTrue(TEXT("Aim follows the authority camera while suppressed"), Suppressed.AimPose.Location.Equals(FVector::ZeroVector, 0.01));

	Director->ReleaseOutputSuppression(Source.Get(), 1.f);
	const float Mid = Driver.Step(Director.Get(), 0.5f).Weight;
	TestTrue(TEXT("Release fades back in"), Mid > 0.f && Mid < 1.f);
	TestEqual(TEXT("Fully back"), Driver.Step(Director.Get(), 0.6f).Weight, 1.f);

	// A destroyed source releases its suppression.
	UACFCameraPreset* Doomed = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
	Director->SuppressOutput(Doomed, 0.f);
	TestEqual(TEXT("Suppressed by doomed source"), Driver.Step(Director.Get(), 1 / 60.f).Weight, 0.f);
	Doomed->MarkAsGarbage();
	TestEqual(TEXT("Lost source releases suppression"), Driver.Step(Director.Get(), 1 / 60.f).Weight, 1.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
