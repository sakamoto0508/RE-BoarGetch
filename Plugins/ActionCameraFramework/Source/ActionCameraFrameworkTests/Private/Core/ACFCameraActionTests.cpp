#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestUtils.h"
#include "Components/SceneComponent.h"
#include "Nodes/ACFCameraFraming.h"
#include "Nodes/ACFCameraNode_GroupFraming.h"
#include "Nodes/ACFCameraNode_LockOn.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "UObject/StrongObjectPtr.h"

/** Phase 4A unit tests: Lock-On node, Target Group, Group Framing (no world needed). */
namespace ACFActionTests
{
	using namespace ACFTests;
	constexpr float Dt = 1.f / 60.f;
	constexpr float Aspect = 16.f / 9.f;

	inline USceneComponent* MakeTarget(const FVector& Location, float Yaw = 0.f)
	{
		USceneComponent* Component = NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetWorldLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f));
		return Component;
	}

	inline FACFCameraTarget TargetOf(USceneComponent* Component)
	{
		FACFCameraTarget Target;
		Target.Component = Component;
		return Target;
	}

	inline UACFCameraPreset* NewPreset(const FACFCameraBlendSettings& Blend = FACFCameraBlendSettings::MakeCut())
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		Preset->DefaultBlendIn = Blend;
		Preset->DefaultBlendOut = Blend;
		return Preset;
	}

	/** Lock-On (+ Group Framing of the player and the locked target). */
	inline UACFCameraPreset* MakeLockOnPreset(bool bFraming, UACFCameraNode_LockOn** OutLockOn = nullptr, UACFCameraNode_GroupFraming** OutFraming = nullptr,
		const FACFCameraBlendSettings& Blend = FACFCameraBlendSettings::MakeCut())
	{
		UACFCameraPreset* Preset = NewPreset(Blend);
		UACFCameraNode_LockOn* LockOn = NewObject<UACFCameraNode_LockOn>(Preset);
		Preset->Nodes.Add(LockOn);
		if (OutLockOn)
		{
			*OutLockOn = LockOn;
		}
		if (bFraming)
		{
			UACFCameraNode_GroupFraming* Framing = NewObject<UACFCameraNode_GroupFraming>(Preset);
			Framing->bFrameTargetGroup = false;
			Framing->FollowTargetSubject.bInclude = true;
			Framing->FollowTargetSubject.Radius = 50.f;
			Framing->LookAtTargetSubject.bInclude = true;
			Framing->LookAtTargetSubject.Radius = 80.f;
			Preset->Nodes.Add(Framing);
			if (OutFraming)
			{
				*OutFraming = Framing;
			}
		}
		return Preset;
	}

	/** Third-person camera the lock-on blends from / back to. */
	inline UACFCameraPreset* MakeOrbitPreset(float Distance = 400.f, const FACFCameraBlendSettings& Blend = FACFCameraBlendSettings::MakeCut())
	{
		UACFCameraPreset* Preset = NewPreset(Blend);
		UACFCameraNode_OrbitalFollow* Orbit = NewObject<UACFCameraNode_OrbitalFollow>(Preset);
		Orbit->InputSource = EACFCameraOrbitInputSource::None;
		Orbit->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
		Orbit->InitialYaw = 0.f;
		Orbit->InitialPitch = -15.f;
		Orbit->Distance = Distance;
		Preset->Nodes.Add(Orbit);
		return Preset;
	}

	/** Fixed camera + Look At (Target Group centre) + Group Framing. */
	inline UACFCameraPreset* MakeGroupPreset(UACFCameraNode_GroupFraming** OutFraming, const FVector& CameraLocation = FVector(-1000, 0, 200))
	{
		UACFCameraPreset* Preset = MakeFixedPreset(CameraLocation, 90.f);
		UACFCameraNode_LookAt* Look = NewObject<UACFCameraNode_LookAt>(Preset);
		Look->TargetSource = EACFCameraLookAtSource::TargetGroup;
		Preset->Nodes.Add(Look);
		UACFCameraNode_GroupFraming* Framing = NewObject<UACFCameraNode_GroupFraming>(Preset);
		Framing->Mode = EACFCameraGroupFramingMode::Distance;
		Framing->WidenDampingTime = 0.f;
		Framing->NarrowDampingTime = 0.f;
		Preset->Nodes.Add(Framing);
		*OutFraming = Framing;
		return Preset;
	}

	inline FACFCameraTargetGroupMember Member(USceneComponent* Component, float Weight = 1.f, float Radius = 60.f)
	{
		FACFCameraTargetGroupMember M;
		M.Target = TargetOf(Component);
		M.Weight = Weight;
		M.Radius = Radius;
		return M;
	}

	/**
	 * Signed clearance (cm) of a sphere inside the centred framing rectangle Size (1 = full screen):
	 * >= 0 means the whole sphere is inside. Uses the same plane test as Group Framing.
	 */
	inline double FramingClearance(const FACFCameraPose& Pose, const FVector& Center, float Radius, const FVector2D& Size = FVector2D(1.0, 1.0))
	{
		const ACFFraming::FLens Lens = ACFFraming::MakeLens(Pose.FieldOfView, Aspect);
		const FVector Local = Pose.Rotation.UnrotateVector(Center - Pose.Location);
		const double Fh = Lens.TanHalfH * Size.X;
		const double Fv = Lens.TanHalfV * Size.Y;
		const double H = (Fh * Local.X - FMath::Abs(Local.Y)) / FMath::Sqrt(1.0 + Fh * Fh) - Radius;
		const double V = (Fv * Local.X - FMath::Abs(Local.Z)) / FMath::Sqrt(1.0 + Fv * Fv) - Radius;
		return FMath::Min3(H, V, Local.X - Radius);
	}

	inline bool IsFinitePose(const FACFCameraPose& Pose)
	{
		return !Pose.Location.ContainsNaN() && !Pose.Rotation.ContainsNaN() && FMath::IsFinite(Pose.FieldOfView);
	}

	/** Horizontal angle (deg) of the camera's position around Pivot, as seen from the pivot towards the camera + 180 = looking direction. */
	inline float OrbitYawOf(const FACFCameraPose& Pose, const FVector& Pivot)
	{
		const FVector ToPivot = Pivot - Pose.Location;
		return FMath::RadiansToDegrees(FMath::Atan2(ToPivot.Y, ToPivot.X));
	}

	inline FFrameDriver MakeDriver()
	{
		FFrameDriver Driver;
		Driver.Upstream.Location = FVector(-500, 0, 300);
		Driver.Upstream.FieldOfView = 90.f;
		Driver.AspectRatio = Aspect;
		return Driver;
	}

	inline void Run(UACFCameraDirector* Director, FFrameDriver& Driver, float Seconds)
	{
		for (int32 Frame = 0; Frame < FMath::RoundToInt(Seconds / Dt); ++Frame)
		{
			Driver.Step(Director, Dt);
		}
	}
}

// -------------------------------------------------------------------------------------------------
// Lock-On

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLockOnStartReleaseTest, "ActionCameraFramework.Action.LockOnStartAndRelease", ACFTests::Flags)
bool FACFLockOnStartReleaseTest::RunTest(const FString& Parameters)
{
	using namespace ACFActionTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Player(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<USceneComponent> Enemy(MakeTarget(FVector(1000, 500, 0)));
	// Releasing hands control back to the exploration camera with ITS Blend In (camera-to-camera rule).
	TStrongObjectPtr<UACFCameraPreset> Orbit(MakeOrbitPreset(400.f, Linear(0.5f)));
	TStrongObjectPtr<UACFCameraPreset> LockPreset(MakeLockOnPreset(true, nullptr, nullptr, Linear(0.5f)));

	FACFCameraRequestParams OrbitParams;
	OrbitParams.FollowTarget = TargetOf(Player.Get());
	Director->AddRequest(Orbit.Get(), OrbitParams);
	FFrameDriver Driver = MakeDriver();
	Run(Director.Get(), Driver, 0.7f);
	const FACFCameraPose OrbitPose = Director->GetLastResult().Pose;

	FACFCameraRequestParams LockParams = OrbitParams;
	LockParams.LookAtTarget = TargetOf(Enemy.Get());
	LockParams.Priority = 10;
	FACFCameraRequestHandle Lock = Director->AddRequest(LockPreset.Get(), LockParams);

	// Start: a smooth blend (no per-frame pop), then the lock-on framing.
	double MaxStep = 0.0;
	FACFCameraPose Previous = OrbitPose;
	bool bSawMidBlend = false;
	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		const FACFCameraEvaluationResult& Result = Driver.Step(Director.Get(), Dt);
		MaxStep = FMath::Max(MaxStep, FVector::Dist(Result.Pose.Location, Previous.Location));
		bSawMidBlend |= Result.bIsBlending && Result.BlendAlpha > 0.3f && Result.BlendAlpha < 0.7f;
		Previous = Result.Pose;
	}
	AddInfo(FString::Printf(TEXT("Lock-on start: max camera move per frame %.1f cm"), MaxStep));
	TestTrue(TEXT("Lock-on start blends (alpha passes 0.3..0.7)"), bSawMidBlend);
	TestTrue(TEXT("Lock-on start has no pop (< 40 cm per 1/60 s)"), MaxStep < 40.0);
	const FACFCameraPose LockPose = Director->GetLastResult().Pose;
	TestFalse(TEXT("Blend finished"), Director->GetLastResult().bIsBlending);
	TestTrue(TEXT("Lock-on camera is in control"), Director->GetActiveRequest() == Lock);

	const float ExpectedYaw = FMath::RadiansToDegrees(FMath::Atan2(500.f, 1000.f));
	const float Yaw = Director->GetLastDebug().OrbitYaw;
	const float CameraSide = OrbitYawOf(LockPose, FVector(0, 0, 60));
	AddInfo(FString::Printf(TEXT("Lock-on orbit yaw %.2f (player -> enemy %.2f), camera seen from the pivot at %.2f (shoulder offset)"), Yaw, ExpectedYaw, CameraSide));
	TestTrue(TEXT("Orbit yaw is the player -> enemy direction"), FMath::Abs(FRotator::NormalizeAxis(Yaw - ExpectedYaw)) < 0.1f);
	TestTrue(TEXT("Camera is behind the player (opposite side from the enemy)"), FMath::Abs(FRotator::NormalizeAxis(CameraSide - ExpectedYaw)) < 15.f);
	TestTrue(TEXT("Player fully on screen"), FramingClearance(LockPose, FVector::ZeroVector, 50.f) >= -0.5);
	TestTrue(TEXT("Enemy fully on screen"), FramingClearance(LockPose, Enemy->GetComponentLocation(), 80.f) >= -0.5);

	// Release: blends back to the exploration camera.
	TestTrue(TEXT("Release lock-on"), Director->RemoveRequest(Lock));
	MaxStep = 0.0;
	Previous = LockPose;
	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		const FACFCameraEvaluationResult& Result = Driver.Step(Director.Get(), Dt);
		MaxStep = FMath::Max(MaxStep, FVector::Dist(Result.Pose.Location, Previous.Location));
		Previous = Result.Pose;
	}
	TestTrue(TEXT("Release has no pop"), MaxStep < 40.0);
	TestTrue(TEXT("Back on the exploration camera"), Director->GetLastResult().Pose.Location.Equals(OrbitPose.Location, 0.5));
	TestEqual(TEXT("ACF keeps full weight (camera-to-camera)"), Director->GetLastResult().Weight, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLockOnRelativeMotionTest, "ActionCameraFramework.Action.LockOnRelativeMotion", ACFTests::Flags)
bool FACFLockOnRelativeMotionTest::RunTest(const FString& Parameters)
{
	using namespace ACFActionTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Player(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<USceneComponent> Enemy(MakeTarget(FVector(800, 0, 0)));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeLockOnPreset(true));
	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Player.Get());
	Params.LookAtTarget = TargetOf(Enemy.Get());
	Director->AddRequest(Preset.Get(), Params);
	FFrameDriver Driver = MakeDriver();

	// The enemy circles the player (90 deg/s) while the player walks forward (300 cm/s) for 4 s.
	double WorstPlayer = UE_BIG_NUMBER;
	double WorstEnemy = UE_BIG_NUMBER;
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		const double T = Frame * Dt;
		const FVector PlayerLocation(300.0 * T, 0, 0);
		const double Angle = FMath::DegreesToRadians(90.0 * T);
		Player->SetWorldLocation(PlayerLocation);
		Enemy->SetWorldLocation(PlayerLocation + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * 800.0);
		const FACFCameraPose& Pose = Driver.Step(Director.Get(), Dt).Pose;
		WorstPlayer = FMath::Min(WorstPlayer, FramingClearance(Pose, PlayerLocation, 50.f));
		WorstEnemy = FMath::Min(WorstEnemy, FramingClearance(Pose, Enemy->GetComponentLocation(), 80.f));
	}
	AddInfo(FString::Printf(TEXT("Circling enemy: worst on-screen clearance player %.1f cm, enemy %.1f cm"), WorstPlayer, WorstEnemy));
	TestTrue(TEXT("Player never leaves the screen while the enemy circles"), WorstPlayer >= 0.0);
	TestTrue(TEXT("Enemy never leaves the screen while it circles"), WorstEnemy >= 0.0);

	// Stop: the camera settles on the player -> enemy direction.
	Run(Director.Get(), Driver, 2.f);
	const FVector ToEnemy = Enemy->GetComponentLocation() - Player->GetComponentLocation();
	const float Expected = FMath::RadiansToDegrees(FMath::Atan2(ToEnemy.Y, ToEnemy.X));
	const float Yaw = Director->GetLastDebug().OrbitYaw;
	AddInfo(FString::Printf(TEXT("Settled orbit yaw %.2f, player -> enemy %.2f"), Yaw, Expected));
	TestTrue(TEXT("Orbit yaw settles on the player -> enemy direction"), FMath::Abs(FRotator::NormalizeAxis(Yaw - Expected)) < 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLockOnRangeTest, "ActionCameraFramework.Action.LockOnNearFarAndOverlap", ACFTests::Flags)
bool FACFLockOnRangeTest::RunTest(const FString& Parameters)
{
	using namespace ACFActionTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Player(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<USceneComponent> Enemy(MakeTarget(FVector(150, 0, 0)));
	UACFCameraNode_LockOn* LockOn = nullptr;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeLockOnPreset(false, &LockOn));
	LockOn->bAdjustFieldOfView = true;
	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Player.Get());
	Params.LookAtTarget = TargetOf(Enemy.Get());
	Director->AddRequest(Preset.Get(), Params);
	FFrameDriver Driver = MakeDriver();

	Run(Director.Get(), Driver, 3.f);
	const float NearArm = Director->GetLastDebug().OrbitDistance;
	const float NearFov = Director->GetLastResult().Pose.FieldOfView;
	Enemy->SetWorldLocation(FVector(3000, 0, 0));
	Run(Director.Get(), Driver, 4.f);
	const float FarArm = Director->GetLastDebug().OrbitDistance;
	const float FarFov = Director->GetLastResult().Pose.FieldOfView;
	AddInfo(FString::Printf(TEXT("Near (150 cm): arm %.1f FOV %.2f / Far (3000 cm): arm %.1f FOV %.2f"), NearArm, NearFov, FarArm, FarFov));
	TestTrue(TEXT("Near: Near Arm Length"), FMath::IsNearlyEqual(NearArm, LockOn->NearArmLength, 1.f));
	TestTrue(TEXT("Near: Near FOV"), FMath::IsNearlyEqual(NearFov, LockOn->NearFieldOfView, 0.1f));
	TestTrue(TEXT("Far: Far Arm Length"), FMath::IsNearlyEqual(FarArm, LockOn->FarArmLength, 1.f));
	TestTrue(TEXT("Far: Far FOV"), FMath::IsNearlyEqual(FarFov, LockOn->FarFieldOfView, 0.1f));

	// Range change is damped (no jump in one frame).
	Enemy->SetWorldLocation(FVector(150, 0, 0));
	Driver.Step(Director.Get(), Dt);
	TestTrue(TEXT("Arm length change is damped"), FMath::Abs(Director->GetLastDebug().OrbitDistance - FarArm) < 10.f);

	// Overlap: target right on top of the player (direction undefined) -> yaw held, no NaN.
	Run(Director.Get(), Driver, 2.f);
	const float YawBefore = Director->GetLastDebug().OrbitYaw;
	bool bFinite = true;
	for (const FVector& Offset : { FVector(0, 0, 0), FVector(1, 0, 300), FVector(-5, 3, -40), FVector(0, 0, 2000) })
	{
		Enemy->SetWorldLocation(Offset);
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			bFinite &= IsFinitePose(Driver.Step(Director.Get(), Dt).Pose);
		}
	}
	TestTrue(TEXT("Overlapping / vertical target never produces NaN"), bFinite);
	TestTrue(TEXT("Yaw is held while the target overlaps the player"), FMath::Abs(FRotator::NormalizeAxis(Director->GetLastDebug().OrbitYaw - YawBefore)) < 0.01f);
	const float Pitch = Director->GetLastDebug().OrbitPitch;
	TestTrue(TEXT("Pitch stays inside the limits for a target straight above"), Pitch >= LockOn->PitchMin - 0.01f && Pitch <= LockOn->PitchMax + 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLockOnJumpTeleportTest, "ActionCameraFramework.Action.LockOnTargetJumpAndPlayerTeleport", ACFTests::Flags)
bool FACFLockOnJumpTeleportTest::RunTest(const FString& Parameters)
{
	using namespace ACFActionTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Player(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<USceneComponent> Enemy(MakeTarget(FVector(1000, 0, 0)));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeLockOnPreset(true));
	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Player.Get());
	Params.LookAtTarget = TargetOf(Enemy.Get());
	Director->AddRequest(Preset.Get(), Params);
	FFrameDriver Driver = MakeDriver();
	Run(Director.Get(), Driver, 2.f);

	// Enemy teleports 90 degrees around: the camera turns smoothly (no cut), then settles.
	Enemy->SetWorldLocation(FVector(0, 3000, 0));
	const float YawBefore = Director->GetLastDebug().OrbitYaw;
	Driver.Step(Director.Get(), Dt);
	const float FirstStep = FMath::Abs(FRotator::NormalizeAxis(Director->GetLastDebug().OrbitYaw - YawBefore));
	Run(Director.Get(), Driver, 3.f);
	const float Settled = Director->GetLastDebug().OrbitYaw;
	AddInfo(FString::Printf(TEXT("Enemy jump: first frame yaw change %.2f deg, settled at %.2f"), FirstStep, Settled));
	TestTrue(TEXT("Enemy jump glides (first frame < 5 deg)"), FirstStep < 5.f);
	TestTrue(TEXT("Enemy jump: settles towards the new target"), FMath::Abs(FRotator::NormalizeAxis(Settled - 90.f)) < 0.5f);

	// Player AND enemy teleport 50 m: the camera cuts with them (same relative pose next frame).
	const FACFCameraPose Before = Director->GetLastResult().Pose;
	const FVector Jump(5000, -2000, 300);
	Player->SetWorldLocation(Player->GetComponentLocation() + Jump);
	Enemy->SetWorldLocation(Enemy->GetComponentLocation() + Jump);
	const FACFCameraPose After = Driver.Step(Director.Get(), Dt).Pose;
	AddInfo(FString::Printf(TEXT("Teleport: relative camera offset error %.3f cm"), FVector::Dist(After.Location - Jump, Before.Location)));
	TestTrue(TEXT("Player teleport cuts (camera keeps its offset)"), (After.Location - Jump).Equals(Before.Location, 1.0));
	TestTrue(TEXT("Player teleport keeps the aim"), After.Rotation.Equals(Before.Rotation, 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLockOnSwitchDestroyTest, "ActionCameraFramework.Action.LockOnSwitchAndTargetDestroyed", ACFTests::Flags)
bool FACFLockOnSwitchDestroyTest::RunTest(const FString& Parameters)
{
	using namespace ACFActionTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Player(MakeTarget(FVector::ZeroVector));
	USceneComponent* EnemyA = MakeTarget(FVector(1000, 0, 0));
	USceneComponent* EnemyB = MakeTarget(FVector(0, 1000, 0));
	TStrongObjectPtr<UACFCameraPreset> Orbit(MakeOrbitPreset());
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeLockOnPreset(true));
	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Player.Get());
	const FACFCameraRequestHandle Explore = Director->AddRequest(Orbit.Get(), Params);
	Params.LookAtTarget = TargetOf(EnemyA);
	Params.Priority = 10;
	const FACFCameraRequestHandle Lock = Director->AddRequest(Preset.Get(), Params);
	FFrameDriver Driver = MakeDriver();
	Run(Director.Get(), Driver, 2.f);

	// Switch targets (gameplay decides; the camera glides).
	TestTrue(TEXT("Switch lock-on target"), Director->SetRequestLookAtTarget(Lock, TargetOf(EnemyB)));
	const FVector LocationBefore = Director->GetLastResult().Pose.Location;
	Driver.Step(Director.Get(), Dt);
	const double SwitchStep = FVector::Dist(Director->GetLastResult().Pose.Location, LocationBefore);
	Run(Director.Get(), Driver, 3.f);
	AddInfo(FString::Printf(TEXT("Target switch: first frame camera move %.2f cm, settled yaw %.2f"), SwitchStep, Director->GetLastDebug().OrbitYaw));
	TestTrue(TEXT("Target switch glides (first frame < 10 cm)"), SwitchStep < 10.0);
	TestTrue(TEXT("Target switch settles on the new target"), FMath::Abs(FRotator::NormalizeAxis(Director->GetLastDebug().OrbitYaw - 90.f)) < 0.5f);

	// Target destroyed without auto release: hold the last lock point (no jump, no crash).
	const FACFCameraPose Held = Director->GetLastResult().Pose;
	EnemyB->MarkAsGarbage();
	Driver.Step(Director.Get(), Dt);
	TestTrue(TEXT("Destroyed target: camera holds"), Director->GetLastResult().Pose.Location.Equals(Held.Location, 0.5));
	TestTrue(TEXT("Destroyed target: flagged as lost"), Director->GetLastDebug().bLockOnTargetLost);
	TestTrue(TEXT("Request still alive (no auto release requested)"), Director->HasRequest(Lock));

	// With bReleaseWhenLookAtTargetLost, destroying the (switched) target releases the request with its blend-out.
	Director->RemoveRequest(Lock);
	Run(Director.Get(), Driver, 0.5f);
	USceneComponent* EnemyC = MakeTarget(FVector(-800, 300, 0));
	USceneComponent* EnemyD = MakeTarget(FVector(800, 300, 0));
	Params.LookAtTarget = TargetOf(EnemyC);
	Params.bReleaseWhenLookAtTargetLost = true;
	const FACFCameraRequestHandle Lock2 = Director->AddRequest(Preset.Get(), Params);
	Run(Director.Get(), Driver, 1.f);
	Director->SetRequestLookAtTarget(Lock2, TargetOf(EnemyD));
	EnemyC->MarkAsGarbage(); // the previous target dying must not matter
	Run(Director.Get(), Driver, 0.2f);
	TestTrue(TEXT("Old target dying after a switch does not release"), Director->HasRequest(Lock2));
	EnemyD->MarkAsGarbage();
	Driver.Step(Director.Get(), Dt);
	TestFalse(TEXT("Current target destroyed: request released automatically"), Director->HasRequest(Lock2));
	TestTrue(TEXT("Control returns to the exploration camera"), Director->GetActiveRequest() == Explore);
	TestTrue(TEXT("Return is finite"), IsFinitePose(Director->GetLastResult().Pose));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFLockOnSwitchingSuppressionTest, "ActionCameraFramework.Action.LockOnCameraSwitchSuppressionFrameGuard", ACFTests::Flags)
bool FACFLockOnSwitchingSuppressionTest::RunTest(const FString& Parameters)
{
	using namespace ACFActionTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraDirector> Reference(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Player(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<USceneComponent> Enemy(MakeTarget(FVector(1200, 0, 0)));
	TStrongObjectPtr<USceneComponent> Enemy2(MakeTarget(FVector(400, -900, 0)));
	TStrongObjectPtr<UACFCameraPreset> Orbit(MakeOrbitPreset());
	TStrongObjectPtr<UACFCameraPreset> LockPreset(MakeLockOnPreset(true, nullptr, nullptr, Linear(0.6f)));
	TStrongObjectPtr<UACFCameraPreset> Cutscene(MakeFixedPreset(FVector(0, 2000, 800), 60.f, Linear(0.3f)));
	TStrongObjectPtr<UObject> SuppressionSource(NewObject<UACFCameraPreset>());

	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Player.Get());
	Director->AddRequest(Orbit.Get(), Params);
	Reference->AddRequest(Orbit.Get(), Params);
	Params.LookAtTarget = TargetOf(Enemy.Get());
	Params.Priority = 10;
	FFrameDriver Driver = MakeDriver();
	FFrameDriver RefDriver = MakeDriver();
	Run(Director.Get(), Driver, 0.3f);
	Run(Reference.Get(), RefDriver, 0.3f);
	const FACFCameraRequestHandle Lock = Director->AddRequest(LockPreset.Get(), Params);
	Reference->AddRequest(LockPreset.Get(), Params);

	// Frame guard: a second evaluation in the same frame (PCM view-target blend) never double-steps.
	bool bSame = true;
	bool bOneRigPerCall = true;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		const FACFCameraPose First = Driver.Step(Director.Get(), Dt).Pose;
		const FACFCameraPose Second = Driver.Repeat(Director.Get(), Dt).Pose;
		bOneRigPerCall &= Director->GetLastRigEvaluationCount() <= 2; // target + live source during the blend
		const FACFCameraPose Ref = RefDriver.Step(Reference.Get(), Dt).Pose;
		bSame &= First.Location.Equals(Second.Location, 1e-4) && First.Location.Equals(Ref.Location, 1e-3);
	}
	TestTrue(TEXT("Repeated evaluation in one frame changes nothing (matches a single-evaluation director)"), bSame);
	TestTrue(TEXT("At most the two blended rigs are evaluated"), bOneRigPerCall);

	// Switching the target in the middle of the lock-on blend-in: no pop.
	Director->RemoveRequest(Lock);
	Run(Director.Get(), Driver, 1.f);
	const FACFCameraRequestHandle Lock2 = Director->AddRequest(LockPreset.Get(), Params);
	Run(Director.Get(), Driver, 0.3f);
	TestTrue(TEXT("Mid blend"), Director->GetLastResult().bIsBlending);
	Director->SetRequestLookAtTarget(Lock2, TargetOf(Enemy2.Get()));
	double MaxStep = 0.0;
	FVector Previous = Director->GetLastResult().Pose.Location;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		const FVector Now = Driver.Step(Director.Get(), Dt).Pose.Location;
		MaxStep = FMath::Max(MaxStep, FVector::Dist(Now, Previous));
		Previous = Now;
	}
	AddInfo(FString::Printf(TEXT("Target switch during the blend-in: max move per frame %.1f cm"), MaxStep));
	TestTrue(TEXT("Target switch during the blend-in has no pop"), MaxStep < 40.0);

	// A higher-priority camera (cut-scene) takes over; the enemy moves meanwhile; lock-on resumes on it.
	FACFCameraRequestParams CutParams;
	CutParams.Priority = 20;
	FACFCameraRequestHandle Cut = Director->AddRequest(Cutscene.Get(), CutParams);
	Run(Director.Get(), Driver, 0.5f);
	Enemy2->SetWorldLocation(FVector(-300, 1200, 0));
	Run(Director.Get(), Driver, 1.f);
	Director->RemoveRequest(Cut);
	Run(Director.Get(), Driver, 1.5f);
	const FACFCameraPose Resumed = Director->GetLastResult().Pose;
	TestTrue(TEXT("Lock-on in control again"), Director->GetActiveRequest() == Lock2);
	TestTrue(TEXT("Resumed lock-on frames the moved enemy"), FramingClearance(Resumed, Enemy2->GetComponentLocation(), 80.f) >= -0.5);
	TestTrue(TEXT("Resumed lock-on frames the player"), FramingClearance(Resumed, FVector::ZeroVector, 50.f) >= -0.5);

	// Output suppression: weight 0, the lock-on keeps evaluating, and resumes up to date.
	Director->SuppressOutput(SuppressionSource.Get(), 0.f);
	Enemy2->SetWorldLocation(FVector(900, 900, 0));
	bool bEvaluated = true;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Driver.Step(Director.Get(), Dt);
		bEvaluated &= Director->GetLastRigEvaluationCount() >= 1;
	}
	TestEqual(TEXT("Suppressed: weight 0"), Director->GetLastResult().Weight, 0.f);
	TestTrue(TEXT("Suppressed: lock-on still evaluated every frame"), bEvaluated);
	Director->ReleaseOutputSuppression(SuppressionSource.Get(), 0.f);
	const FACFCameraEvaluationResult& After = Driver.Step(Director.Get(), Dt);
	TestEqual(TEXT("Released: weight 1"), After.Weight, 1.f);
	TestTrue(TEXT("Released: already framing the enemy's new position"), FramingClearance(After.Pose, Enemy2->GetComponentLocation(), 80.f) >= -0.5);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Target Group / Group Framing

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFGroupFramingMathTest, "ActionCameraFramework.Action.GroupFramingMath", ACFTests::Flags)
bool FACFGroupFramingMathTest::RunTest(const FString& Parameters)
{
	// Sphere r = 100 straight ahead at 500 cm, 90 deg horizontal FOV, aspect 1, full screen:
	// it touches the frustum planes when x = r * sqrt(2) (tan 45 = 1), so the required move is 141.42 - 500.
	FACFCameraFramingSubjects Subjects;
	Subjects.Add({ FVector(500, 0, 0), 100.f, 1.f });
	const double Dolly = UACFCameraNode_GroupFraming::ComputeRequiredDolly(Subjects, FVector::ZeroVector, FRotator::ZeroRotator, 90.f, 1.f, FVector2D(1, 1), 30.f);
	TestTrue(FString::Printf(TEXT("Required dolly %.3f == %.3f"), Dolly, 100.0 * UE_SQRT_2 - 500.0), FMath::IsNearlyEqual(Dolly, 100.0 * UE_SQRT_2 - 500.0, 1e-3));

	// Same sphere at 1000 cm: half angle asin(0.1) -> FOV 2 * asin(0.1) = 11.478 deg.
	Subjects[0].Location = FVector(1000, 0, 0);
	const double Fov = UACFCameraNode_GroupFraming::ComputeRequiredFov(Subjects, FVector::ZeroVector, FRotator::ZeroRotator, 1.f, FVector2D(1, 1), 30.f, 170.f);
	TestTrue(FString::Printf(TEXT("Required FOV %.4f == 11.4783"), Fov), FMath::IsNearlyEqual(Fov, 2.0 * FMath::RadiansToDegrees(FMath::Asin(0.1)), 1e-3));

	// Behind the camera: dolly requires moving back past it; FOV clamps to the maximum.
	Subjects[0].Location = FVector(-200, 0, 0);
	TestTrue(TEXT("Subject behind the camera: dolly moves back past it"),
		UACFCameraNode_GroupFraming::ComputeRequiredDolly(Subjects, FVector::ZeroVector, FRotator::ZeroRotator, 90.f, 1.f, FVector2D(1, 1), 30.f) > 200.0);
	TestTrue(TEXT("Subject behind the camera: FOV clamps to the maximum"),
		FMath::IsNearlyEqual(UACFCameraNode_GroupFraming::ComputeRequiredFov(Subjects, FVector::ZeroVector, FRotator::ZeroRotator, 1.f, FVector2D(1, 1), 30.f, 120.f), 120.0, 1e-3));

	// Weights: a weight-0.5 member is pulled half way to the weighted centre and its radius halved.
	FACFCameraFramingSubjects Weighted;
	Weighted.Add({ FVector(0, 0, 0), 100.f, 1.f });
	Weighted.Add({ FVector(0, 300, 0), 100.f, 0.5f });
	FVector Center;
	TestTrue(TEXT("ApplyWeights"), UACFCameraNode_GroupFraming::ApplyWeights(Weighted, Center));
	TestTrue(TEXT("Weighted centre"), Center.Equals(FVector(0, 100, 0), 1e-3));
	TestTrue(TEXT("Light member pulled half way to the centre"), Weighted[1].Location.Equals(FVector(0, 200, 0), 1e-3));
	TestTrue(TEXT("Light member radius halved"), FMath::IsNearlyEqual(Weighted[1].Radius, 50.f, 1e-3f));
	FACFCameraFramingSubjects Empty;
	TestFalse(TEXT("No subject: no centre"), UACFCameraNode_GroupFraming::ApplyWeights(Empty, Center));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFGroupAddRemoveTest, "ActionCameraFramework.Action.GroupAddRemoveDestroyed", ACFTests::Flags)
bool FACFGroupAddRemoveTest::RunTest(const FString& Parameters)
{
	using namespace ACFActionTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	UACFCameraNode_GroupFraming* Framing = nullptr;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeGroupPreset(&Framing, FVector(-400, 0, 200)));
	TStrongObjectPtr<USceneComponent> A(MakeTarget(FVector(0, 0, 100)));
	TStrongObjectPtr<USceneComponent> B(MakeTarget(FVector(200, 1500, 100)));
	USceneComponent* C = MakeTarget(FVector(300, -700, 100));

	FACFCameraRequestParams Params;
	Params.TargetGroup.Members.Add(Member(A.Get()));
	const FACFCameraRequestHandle Handle = Director->AddRequest(Preset.Get(), Params);
	FFrameDriver Driver = MakeDriver();
	Driver.Step(Director.Get(), Dt);
	const FACFCameraPose Single = Director->GetLastResult().Pose;
	TestTrue(TEXT("One member: camera unchanged (already inside, Keep Inside)"), Single.Location.Equals(FVector(-400, 0, 200), 0.01));
	TestTrue(TEXT("One member: aimed at it"), FramingClearance(Single, A->GetComponentLocation(), 60.f) > 0.0);

	// Add a far member: the camera pulls back until both spheres fit inside 80% of the screen.
	TestTrue(TEXT("Add member"), Director->AddGroupMember(Handle, Member(B.Get())));
	Driver.Step(Director.Get(), Dt);
	const FACFCameraPose Two = Director->GetLastResult().Pose;
	const double ClearA = FramingClearance(Two, A->GetComponentLocation(), 60.f, Framing->FramingSize);
	const double ClearB = FramingClearance(Two, B->GetComponentLocation(), 60.f, Framing->FramingSize);
	AddInfo(FString::Printf(TEXT("Two members: dolly %.1f cm, clearances A %.3f B %.3f"), Director->GetLastDebug().GroupDolly, ClearA, ClearB));
	TestTrue(TEXT("Two members: camera pulled back"), Director->GetLastDebug().GroupDolly > 100.f);
	TestTrue(TEXT("Two members: both inside the framing area"), ClearA >= -0.01 && ClearB >= -0.01);
	TestTrue(TEXT("Two members: tight (one touches the framing edge)"), FMath::Min(ClearA, ClearB) < 0.5);
	TestEqual(TEXT("Adding the same target again only updates it"), (Director->AddGroupMember(Handle, Member(B.Get(), 1.f, 60.f)), Director->GetGroup(Handle)->Members.Num()), 2);

	// Three members, then the third is destroyed: removed automatically, framing follows.
	Director->AddGroupMember(Handle, Member(C));
	Driver.Step(Director.Get(), Dt);
	const float ThreeDolly = Director->GetLastDebug().GroupDolly;
	AddInfo(FString::Printf(TEXT("Three members: dolly %.1f cm, clearances A %.3f B %.3f C %.3f"), ThreeDolly,
		FramingClearance(Director->GetLastResult().Pose, A->GetComponentLocation(), 60.f, Framing->FramingSize),
		FramingClearance(Director->GetLastResult().Pose, B->GetComponentLocation(), 60.f, Framing->FramingSize),
		FramingClearance(Director->GetLastResult().Pose, C->GetComponentLocation(), 60.f, Framing->FramingSize)));
	TestTrue(TEXT("Three members all inside"), FramingClearance(Director->GetLastResult().Pose, C->GetComponentLocation(), 60.f, Framing->FramingSize) >= -0.01);
	C->MarkAsGarbage();
	Driver.Step(Director.Get(), Dt);
	TestEqual(TEXT("Destroyed member removed from the group"), Director->GetGroup(Handle)->Members.Num(), 2);
	TestTrue(TEXT("Framing without the destroyed member"), Director->GetLastDebug().GroupDolly < ThreeDolly);

	// Remove: back to the single-member camera; remove the last: 0 members (no adjustment).
	TestTrue(TEXT("Remove member"), Director->RemoveGroupMember(Handle, TargetOf(B.Get())));
	TestFalse(TEXT("Removing twice reports false"), Director->RemoveGroupMember(Handle, TargetOf(B.Get())));
	Driver.Step(Director.Get(), Dt);
	TestTrue(TEXT("After removal the camera is back"), Director->GetLastResult().Pose.Location.Equals(Single.Location, 0.01));
	Director->RemoveGroupMember(Handle, TargetOf(A.Get()));
	Driver.Step(Director.Get(), Dt);
	TestTrue(TEXT("Zero members: no adjustment, finite"), Director->GetLastResult().Pose.Location.Equals(FVector(-400, 0, 200), 0.01) && IsFinitePose(Director->GetLastResult().Pose));
	TestEqual(TEXT("Zero members: no subject"), Director->GetLastDebug().NumGroupSubjects, 0);

	// Damped: widening is fast but not instantaneous, narrowing slower.
	Framing->WidenDampingTime = 0.15f;
	Framing->NarrowDampingTime = 0.8f;
	Director->SetGroupMembers(Handle, { Member(A.Get()), Member(B.Get()) });
	Driver.Step(Director.Get(), Dt);
	const float Partial = Director->GetLastDebug().GroupDolly;
	Run(Director.Get(), Driver, 1.5f);
	const float Full = Director->GetLastDebug().GroupDolly;
	TestTrue(TEXT("Widening is damped"), Partial > 0.f && Partial < Full * 0.5f);
	Director->RemoveGroupMember(Handle, TargetOf(B.Get()));
	Run(Director.Get(), Driver, 0.3f);
	TestTrue(TEXT("Narrowing eases (no pop when a member leaves)"), Director->GetLastDebug().GroupDolly > Full * 0.4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFGroupWeightRadiusTest, "ActionCameraFramework.Action.GroupWeightAndRadius", ACFTests::Flags)
bool FACFGroupWeightRadiusTest::RunTest(const FString& Parameters)
{
	using namespace ACFActionTests;
	UACFCameraNode_GroupFraming* Framing = nullptr;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeGroupPreset(&Framing, FVector(-400, 0, 200)));
	TStrongObjectPtr<USceneComponent> A(MakeTarget(FVector(0, -700, 100)));
	TStrongObjectPtr<USceneComponent> B(MakeTarget(FVector(0, 700, 100)));

	auto Frame = [&](float WeightB, float RadiusB, float& OutDolly, FACFCameraPose& OutPose)
	{
		TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
		FACFCameraRequestParams Params;
		Params.TargetGroup.Members = { Member(A.Get()), Member(B.Get(), WeightB, RadiusB) };
		Director->AddRequest(Preset.Get(), Params);
		FFrameDriver Driver = MakeDriver();
		Driver.Step(Director.Get(), Dt);
		OutDolly = Director->GetLastDebug().GroupDolly;
		OutPose = Director->GetLastResult().Pose;
	};
	float Base, BigRadius, Light, Ignored;
	FACFCameraPose BasePose, BigPose, LightPose, IgnoredPose;
	Frame(1.f, 60.f, Base, BasePose);
	Frame(1.f, 400.f, BigRadius, BigPose);
	Frame(0.25f, 60.f, Light, LightPose);
	Frame(0.f, 60.f, Ignored, IgnoredPose);
	AddInfo(FString::Printf(TEXT("Dolly: equal %.1f, B radius 400 %.1f, B weight 0.25 %.1f, B weight 0 %.1f"), Base, BigRadius, Light, Ignored));
	TestTrue(TEXT("Larger radius -> camera further back"), BigRadius > Base + 50.f);
	TestTrue(TEXT("Larger radius: the big sphere fits"), FramingClearance(BigPose, B->GetComponentLocation(), 400.f, Framing->FramingSize) >= -0.01);
	TestTrue(TEXT("Lower weight -> less pull-back"), Light < Base);
	TestTrue(TEXT("Lower weight -> centre moves towards the heavy member (camera turns to A)"), LightPose.Rotation.Yaw < BasePose.Rotation.Yaw - 1.f);
	TestTrue(TEXT("Lower weight: the heavy member still fits"), FramingClearance(LightPose, A->GetComponentLocation(), 60.f, Framing->FramingSize) >= -0.01);
	TestEqual(TEXT("Weight 0: member ignored (no pull-back for A alone)"), Ignored, 0.f);
	TestTrue(TEXT("Weight 0: aimed at A alone"), FramingClearance(IgnoredPose, A->GetComponentLocation(), 60.f) > 0.0 && FMath::Abs(FRotator::NormalizeAxis(IgnoredPose.Rotation.Yaw - (A->GetComponentLocation() - FVector(-400, 0, 200)).Rotation().Yaw)) < 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFGroupFovFillTest, "ActionCameraFramework.Action.GroupFovModeAndFill", ACFTests::Flags)
bool FACFGroupFovFillTest::RunTest(const FString& Parameters)
{
	using namespace ACFActionTests;
	UACFCameraNode_GroupFraming* Framing = nullptr;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeGroupPreset(&Framing));
	TStrongObjectPtr<USceneComponent> A(MakeTarget(FVector(0, -900, 100)));
	TStrongObjectPtr<USceneComponent> B(MakeTarget(FVector(0, 900, 100)));
	TStrongObjectPtr<USceneComponent> Far(MakeTarget(FVector(3000, 0, 200)));

	auto Evaluate = [&](const TArray<FACFCameraTargetGroupMember>& Members) -> FACFCameraPose
	{
		TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
		FACFCameraRequestParams Params;
		Params.TargetGroup.Members = Members;
		Director->AddRequest(Preset.Get(), Params);
		FFrameDriver Driver = MakeDriver();
		return Driver.Step(Director.Get(), Dt).Pose;
	};

	// FOV mode, Keep Inside: widen, never move.
	Framing->Mode = EACFCameraGroupFramingMode::FieldOfView;
	Framing->MaxFieldOfView = 140.f;
	const FACFCameraPose Wide = Evaluate({ Member(A.Get()), Member(B.Get()) });
	AddInfo(FString::Printf(TEXT("FOV mode: %.2f deg"), Wide.FieldOfView));
	TestTrue(TEXT("FOV mode: camera does not move"), Wide.Location.Equals(FVector(-1000, 0, 200), 0.01));
	TestTrue(TEXT("FOV mode: widened"), Wide.FieldOfView > 90.f);
	TestTrue(TEXT("FOV mode: both fit"), FramingClearance(Wide, A->GetComponentLocation(), 60.f, Framing->FramingSize) >= -0.05
		&& FramingClearance(Wide, B->GetComponentLocation(), 60.f, Framing->FramingSize) >= -0.05);
	const FACFCameraPose KeepFar = Evaluate({ Member(Far.Get()) });
	TestEqual(TEXT("Keep Inside never narrows"), KeepFar.FieldOfView, 90.f);

	// Fill: a single far member is zoomed in until its sphere fills the framing area (within Min FOV).
	Framing->Policy = EACFCameraGroupFramingPolicy::Fill;
	Framing->MinFieldOfView = 5.f;
	const FACFCameraPose Filled = Evaluate({ Member(Far.Get(), 1.f, 150.f) });
	const double FillClearance = FramingClearance(Filled, Far->GetComponentLocation(), 150.f, Framing->FramingSize);
	AddInfo(FString::Printf(TEXT("Fill FOV %.2f deg, edge clearance %.3f cm"), Filled.FieldOfView, FillClearance));
	TestTrue(TEXT("Fill: narrowed"), Filled.FieldOfView < 20.f);
	TestTrue(TEXT("Fill: sphere touches the framing edge"), FMath::Abs(FillClearance) < 0.5);
	Framing->MinFieldOfView = 30.f;
	TestEqual(TEXT("Fill: clamped by Min FOV"), Evaluate({ Member(Far.Get(), 1.f, 150.f) }).FieldOfView, 30.f);

	// Distance + Fill: one member framed by moving in (within Max Dolly In).
	Framing->Mode = EACFCameraGroupFramingMode::Distance;
	Framing->MaxDollyIn = 5000.f;
	const FACFCameraPose Dolly = Evaluate({ Member(Far.Get(), 1.f, 150.f) });
	TestTrue(TEXT("Distance Fill: moved towards the member"), Dolly.Location.X > -1000.0 + 100.0);
	TestTrue(TEXT("Distance Fill: sphere touches the framing edge"), FMath::Abs(FramingClearance(Dolly, Far->GetComponentLocation(), 150.f, Framing->FramingSize)) < 0.5);

	// Distance then FOV: Max Dolly Out too small -> FOV covers the rest.
	Framing->Mode = EACFCameraGroupFramingMode::DistanceThenFieldOfView;
	Framing->Policy = EACFCameraGroupFramingPolicy::KeepInside;
	Framing->MaxDollyOut = 100.f;
	const FACFCameraPose Mixed = Evaluate({ Member(A.Get()), Member(B.Get()) });
	TestTrue(TEXT("Distance then FOV: moved back by the maximum"), FMath::IsNearlyEqual(FVector::Dist(Mixed.Location, FVector(-1000, 0, 200)), 100.0, 0.5));
	TestTrue(TEXT("Distance then FOV: widened for the rest"), Mixed.FieldOfView > 90.f && Mixed.FieldOfView < Wide.FieldOfView);
	TestTrue(TEXT("Distance then FOV: both fit"), FramingClearance(Mixed, A->GetComponentLocation(), 60.f, Framing->FramingSize) >= -0.05
		&& FramingClearance(Mixed, B->GetComponentLocation(), 60.f, Framing->FramingSize) >= -0.05);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
