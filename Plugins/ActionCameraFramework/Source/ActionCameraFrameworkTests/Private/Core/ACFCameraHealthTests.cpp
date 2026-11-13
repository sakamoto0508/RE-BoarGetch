#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestUtils.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 6B / B-4: 不正値（NaN / Inf / 不正な FOV / 不正な DeltaTime など）に対する出力と内部状態の健全性。
 * World なしで Director を直接評価する（PCM を通す確認は .Sequencer.Health.*）。
 */
namespace ACFHealthTests
{
	using namespace ACFTests;

	static const double NaN = std::numeric_limits<double>::quiet_NaN();
	static const double Inf = std::numeric_limits<double>::infinity();

	inline bool IsValidPose(const FACFCameraPose& Pose)
	{
		return !Pose.Location.ContainsNaN() && FMath::IsFinite(Pose.Location.X) && FMath::IsFinite(Pose.Location.Y) && FMath::IsFinite(Pose.Location.Z)
			&& !Pose.Rotation.ContainsNaN() && FMath::IsFinite(Pose.Rotation.Pitch) && FMath::IsFinite(Pose.Rotation.Yaw) && FMath::IsFinite(Pose.Rotation.Roll)
			&& FMath::IsFinite(Pose.FieldOfView) && Pose.FieldOfView > 0.f && Pose.FieldOfView < 180.f;
	}

	inline USceneComponent* MakeTarget(const FVector& Location)
	{
		USceneComponent* Component = NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetWorldLocation(Location);
		return Component;
	}

	/** Writes the transform without the engine's own NaN handling (what a broken animation / physics result would leave). */
	inline void ForceTransform(USceneComponent* Component, const FVector& Location, const FQuat& Rotation = FQuat::Identity)
	{
		FTransform Transform;
		Transform.SetLocation(Location);
		Transform.SetRotation(Rotation);
		Transform.SetScale3D(FVector::OneVector);
		Component->SetComponentToWorld(Transform);
	}

	inline FACFCameraTarget TargetOf(USceneComponent* Component)
	{
		FACFCameraTarget Target;
		Target.Component = Component;
		return Target;
	}

	inline UACFCameraPreset* MakeTrackingPreset(const FVector& Offset, float Damping, float LookDamping, EACFCameraOffsetSpace Space = EACFCameraOffsetSpace::World)
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_Follow* Follow = NewObject<UACFCameraNode_Follow>(Preset);
		Follow->Offset = Offset;
		Follow->OffsetSpace = Space;
		Follow->Damping = FACFCameraPositionDamping::Uniform(Damping);
		UACFCameraNode_LookAt* Look = NewObject<UACFCameraNode_LookAt>(Preset);
		Look->DampingTime = LookDamping;
		Preset->Nodes = { Follow, Look };
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		return Preset;
	}

	/** Counts frames whose pose is invalid; returns the first invalid frame description. */
	struct FValidity
	{
		int32 Invalid = 0;
		FString First;
		void Check(const FACFCameraEvaluationResult& Result, const TCHAR* Phase, int32 Frame)
		{
			if (!IsValidPose(Result.Pose) || !FMath::IsFinite(Result.Weight))
			{
				if (Invalid++ == 0)
				{
					First = FString::Printf(TEXT("%s frame %d: loc %s rot %s fov %f weight %f"), Phase, Frame, *Result.Pose.Location.ToString(), *Result.Pose.Rotation.ToString(), Result.Pose.FieldOfView, Result.Weight);
				}
			}
		}
	};
}

// -------------------------------------------------------------------------------------------------
// 1-3: 対象の位置・回転が非有限になっても出力は有限で、正常値に戻ると Damping が復帰する。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFHealthTargetNaNTest, "ActionCameraFramework.Health.InvalidTargetRecovers", ACFTests::Flags)
bool FACFHealthTargetNaNTest::RunTest(const FString& Parameters)
{
	using namespace ACFHealthTests;
	struct FCase { const TCHAR* Name; FVector Location; FQuat Rotation; EACFCameraOffsetSpace Space; };
	const FCase Cases[] = {
		{ TEXT("location NaN"), FVector(NaN, 0.0, 0.0), FQuat::Identity, EACFCameraOffsetSpace::World },
		{ TEXT("location +Inf"), FVector(0.0, Inf, 0.0), FQuat::Identity, EACFCameraOffsetSpace::World },
		{ TEXT("rotation NaN"), FVector(500.0, 0.0, 0.0), FQuat(NaN, 0.0, 0.0, 1.0), EACFCameraOffsetSpace::TargetLocal },
		{ TEXT("rotation Inf"), FVector(500.0, 0.0, 0.0), FQuat(0.0, 0.0, Inf, 1.0), EACFCameraOffsetSpace::TargetLocal },
		{ TEXT("zero quaternion"), FVector(500.0, 0.0, 0.0), FQuat(0.0, 0.0, 0.0, 0.0), EACFCameraOffsetSpace::TargetLocal },
	};
	for (const FCase& Case : Cases)
	{
		TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
		TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
		TStrongObjectPtr<UACFCameraPreset> Preset(MakeTrackingPreset(FVector(-300.0, 0.0, 100.0), 0.3f, 0.2f, Case.Space));
		FACFCameraRequestParams Params;
		Params.FollowTarget = TargetOf(Target.Get());
		Params.LookAtTarget = TargetOf(Target.Get());
		Director->AddRequest(Preset.Get(), Params);
		FFrameDriver Driver;
		Driver.Upstream.Location = FVector(0.0, 0.0, 500.0);
		FValidity Validity;
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("before"), Frame);
		}
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			ForceTransform(Target.Get(), Case.Location, Case.Rotation);
			Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("invalid target"), Frame);
		}
		const FVector Back(1000.0, 200.0, 0.0);
		ForceTransform(Target.Get(), Back);
		for (int32 Frame = 0; Frame < 240; ++Frame)
		{
			Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("after"), Frame);
		}
		TestEqual(FString::Printf(TEXT("%s: frames with a non-finite / invalid pose (%s)"), Case.Name, *Validity.First), Validity.Invalid, 0);
		const FACFCameraPose& Pose = Director->GetLastResult().Pose;
		TestTrue(FString::Printf(TEXT("%s: camera recovered to the target again (%s)"), Case.Name, *Pose.Location.ToString()),
			Pose.Location.Equals(Back + FVector(-300.0, 0.0, 100.0), 1.0));
		const FVector ToTarget = (Back - Pose.Location).GetSafeNormal();
		TestTrue(FString::Printf(TEXT("%s: look-at recovered (%s)"), Case.Name, *Pose.Rotation.ToString()), Pose.Rotation.Vector().Equals(ToTarget, 0.01));
		const FACFCameraHealthDiagnostics D = Director->GetHealthDiagnostics();
		TestTrue(FString::Printf(TEXT("%s: every invalid frame reported at the target boundary (%d)"), Case.Name, D.InvalidTargets), D.InvalidTargets >= 10);
		TestEqual(FString::Printf(TEXT("%s: stopped before any node (no node output had to be dropped)"), Case.Name), D.InvalidNodeOutputs, 0);
		TestTrue(FString::Printf(TEXT("%s: last issue names the parameter and the fallback (%s)"), Case.Name, *D.LastIssue),
			D.LastIssue.Contains(TEXT("target")) && D.LastIssue.Contains(TEXT("held")));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// 8: 最後の正常な姿勢（Fallback）は同じ World / PlayerController の中だけで使う。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFHealthFallbackContextTest, "ActionCameraFramework.Health.FallbackPoseIsScopedToTheWorld", ACFTests::Flags)
bool FACFHealthFallbackContextTest::RunTest(const FString& Parameters)
{
	using namespace ACFHealthTests;
	UWorld* WorldA = nullptr;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		WorldA = WorldA ? WorldA : WorldContext.World();
	}
	if (!TestNotNull(TEXT("a world to stand for world A"), WorldA))
	{
		return false;
	}
	// Look At only: the location comes from the UE camera (upstream), so an invalid UE camera reaches the final pose.
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector(500.0, 0.0, 0.0)));
	TStrongObjectPtr<UACFCameraPreset> Preset(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
	Preset->Nodes.Add(NewObject<UACFCameraNode_LookAt>(Preset.Get()));
	Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
	FACFCameraRequestParams Params;
	Params.LookAtTarget = TargetOf(Target.Get());
	Director->AddRequest(Preset.Get(), Params);
	uint64 Frame = 5000;
	auto Step = [&](UWorld* World, const FVector& Upstream) -> const FACFCameraEvaluationResult&
	{
		FACFCameraEvaluationContext Context;
		Context.DeltaTime = 1.f / 60.f;
		Context.FrameNumber = ++Frame;
		Context.World = World;
		Context.UpstreamPose.Location = Upstream;
		Context.UpstreamPose.FieldOfView = 90.f;
		return Director->Evaluate(Context);
	};
	const FVector UpstreamA(0.0, 0.0, 300.0);
	for (int32 I = 0; I < 10; ++I)
	{
		Step(WorldA, UpstreamA);
	}
	// Same world: an invalid UE camera falls back to the last valid one of this world.
	const FACFCameraEvaluationResult& Same = Step(WorldA, FVector(NaN, NaN, NaN));
	TestTrue(TEXT("Same world: invalid UE camera -> last valid UE camera of this world"), IsValidPose(Same.Pose) && Same.Pose.Location.Equals(UpstreamA, 0.01) && Same.Weight == 1.f);
	// Another world (map travel): nothing from world A may be reused.
	const FACFCameraEvaluationResult& Other = Step(nullptr, FVector(NaN, NaN, NaN));
	TestEqual(TEXT("Other world, no valid pose yet: ACF does not output (weight 0) instead of reusing world A's pose"), Other.Weight, 0.f);
	TestFalse(TEXT("Other world: world A's pose is not output"), Other.Pose.Location.Equals(UpstreamA, 0.01));
	TestFalse(TEXT("Other world: no fallback pose"), Director->HasLastGoodPose());
	const FVector UpstreamB(1000.0, 0.0, 200.0);
	const FACFCameraEvaluationResult& Valid = Step(nullptr, UpstreamB);
	TestTrue(TEXT("Other world, valid UE camera: ACF outputs again"), Valid.Weight == 1.f && Valid.Pose.Location.Equals(UpstreamB, 0.01));
	TestTrue(TEXT("Other world: a new fallback pose of this world"), Director->HasLastGoodPose());
	Director->InvalidateLastGoodPose(); // what the subsystem does when the view target changes
	TestFalse(TEXT("View target changed: fallback forgotten"), Director->HasLastGoodPose());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Node のパラメータ（Offset / Damping / FOV）に非有限値・範囲外の値が入る。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFHealthNodeParametersTest, "ActionCameraFramework.Health.InvalidNodeParameters", ACFTests::Flags)
bool FACFHealthNodeParametersTest::RunTest(const FString& Parameters)
{
	using namespace ACFHealthTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector(100.0, 0.0, 0.0)));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeTrackingPreset(FVector(-300.0, 0.0, 100.0), 0.2f, 0.f));
	UACFCameraNode_Follow* Follow = CastChecked<UACFCameraNode_Follow>(Preset->Nodes[0]);
	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Target.Get());
	Params.LookAtTarget = TargetOf(Target.Get());
	Director->AddRequest(Preset.Get(), Params);
	FFrameDriver Driver;
	Driver.Upstream.Location = FVector(0.0, 0.0, 500.0);
	FValidity Validity;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("before"), Frame);
	}
	Follow->Offset = FVector(NaN, 0.0, 0.0);
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("offset NaN"), Frame);
	}
	Follow->Offset = FVector(-300.0, 0.0, 100.0);
	Follow->Damping = FACFCameraPositionDamping::Uniform(static_cast<float>(NaN));
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("damping NaN"), Frame);
	}
	Follow->Damping = FACFCameraPositionDamping::Uniform(0.2f);
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("after"), Frame);
	}
	TestEqual(FString::Printf(TEXT("Invalid node parameters: frames with an invalid pose (%s)"), *Validity.First), Validity.Invalid, 0);
	TestTrue(FString::Printf(TEXT("Parameters fixed: camera back on the follow offset (%s)"), *Director->GetLastResult().Pose.Location.ToString()),
		Director->GetLastResult().Pose.Location.Equals(FVector(-200.0, 0.0, 100.0), 1.0));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 4: 不正な FOV（Fixed Pose の FOV に NaN / Inf / 負 / 0 / 180 以上）。有効な FOV はそのまま。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFHealthFovTest, "ActionCameraFramework.Health.InvalidFov", ACFTests::Flags)
bool FACFHealthFovTest::RunTest(const FString& Parameters)
{
	using namespace ACFHealthTests;
	const float Invalid[] = { static_cast<float>(NaN), static_cast<float>(Inf), -static_cast<float>(Inf), -10.f, 0.f, 180.f, 500.f };
	for (const float Fov : Invalid)
	{
		TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
		TStrongObjectPtr<UACFCameraPreset> Valid(MakeFixedPreset(FVector(0.0, 0.0, 300.0), 75.f));
		Director->AddRequest(Valid.Get(), FACFCameraRequestParams());
		FFrameDriver Driver;
		Driver.Upstream.FieldOfView = 90.f;
		Driver.Step(Director.Get(), 1.f / 60.f);
		TStrongObjectPtr<UACFCameraPreset> Bad(MakeFixedPreset(FVector(0.0, 0.0, 300.0), Fov));
		FACFCameraRequestParams High;
		High.Priority = 10;
		Director->AddRequest(Bad.Get(), High);
		FValidity Validity;
		for (int32 Frame = 0; Frame < 5; ++Frame)
		{
			Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("bad fov"), Frame);
		}
		TestEqual(FString::Printf(TEXT("FOV %f: no invalid output (%s)"), Fov, *Validity.First), Validity.Invalid, 0);
		TestTrue(FString::Printf(TEXT("FOV %f: the location of the camera is still applied"), Fov), Director->GetLastResult().Pose.Location.Equals(FVector(0.0, 0.0, 300.0), 0.01));
	}
	const float ValidFovs[] = { 0.5f, 5.f, 90.f, 170.f, 179.5f };
	for (const float Fov : ValidFovs)
	{
		TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
		TStrongObjectPtr<UACFCameraPreset> Preset(MakeFixedPreset(FVector(0.0, 0.0, 300.0), Fov));
		Director->AddRequest(Preset.Get(), FACFCameraRequestParams());
		FFrameDriver Driver;
		TestEqual(FString::Printf(TEXT("Valid FOV %f is output unchanged"), Fov), Driver.Step(Director.Get(), 1.f / 60.f).Pose.FieldOfView, Fov);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// DeltaTime に NaN / Inf / 負 / 巨大値。時間は戻らず、出力は有限。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFHealthDeltaTimeTest, "ActionCameraFramework.Health.InvalidDeltaTime", ACFTests::Flags)
bool FACFHealthDeltaTimeTest::RunTest(const FString& Parameters)
{
	using namespace ACFHealthTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector(100.0, 0.0, 0.0)));
	TStrongObjectPtr<UACFCameraPreset> A(MakeFixedPreset(FVector(0.0, 0.0, 300.0), 80.f));
	TStrongObjectPtr<UACFCameraPreset> B(MakeTrackingPreset(FVector(-300.0, 0.0, 100.0), 0.3f, 0.2f));
	B->DefaultBlendIn = Linear(1.f);
	Director->AddRequest(A.Get(), FACFCameraRequestParams());
	FFrameDriver Driver;
	FValidity Validity;
	Driver.Step(Director.Get(), 1.f / 60.f);
	FACFCameraRequestParams Params;
	Params.Priority = 5;
	Params.FollowTarget = TargetOf(Target.Get());
	Params.LookAtTarget = TargetOf(Target.Get());
	Director->AddRequest(B.Get(), Params);
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("blend"), Frame);
	}
	const float AlphaBefore = Director->GetLastResult().BlendAlpha;
	const float Bad[] = { static_cast<float>(NaN), static_cast<float>(Inf), -static_cast<float>(Inf), -0.5f };
	int32 Index = 0;
	for (const float Dt : Bad)
	{
		Target->SetWorldLocation(FVector(100.0 + 10.0 * ++Index, 0.0, 0.0));
		Validity.Check(Driver.Step(Director.Get(), Dt), TEXT("bad delta"), Index);
	}
	TestTrue(FString::Printf(TEXT("Invalid delta times do not move the blend (alpha %f -> %f)"), AlphaBefore, Director->GetLastResult().BlendAlpha),
		Director->GetLastResult().bIsBlending && FMath::IsNearlyEqual(Director->GetLastResult().BlendAlpha, AlphaBefore, 1.e-4f));
	Validity.Check(Driver.Step(Director.Get(), 1.0e6f), TEXT("huge delta"), 0);
	TestFalse(TEXT("A huge (finite) delta time finishes the blend"), Director->GetLastResult().bIsBlending);
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("after"), Frame);
	}
	TestEqual(FString::Printf(TEXT("Invalid delta times: no invalid output (%s)"), *Validity.First), Validity.Invalid, 0);
	TestTrue(TEXT("After: following the target"), Director->GetLastResult().Pose.Location.Equals(Target->GetComponentLocation() + FVector(-300.0, 0.0, 100.0), 1.0));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 5: 注視対象がカメラと同じ位置（ゼロ距離の LookAt）。巨大な座標。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFHealthDegenerateGeometryTest, "ActionCameraFramework.Health.ZeroDistanceLookAtAndHugeCoordinates", ACFTests::Flags)
bool FACFHealthDegenerateGeometryTest::RunTest(const FString& Parameters)
{
	using namespace ACFHealthTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector(100.0, 0.0, 0.0)));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeTrackingPreset(FVector::ZeroVector, 0.f, 0.f)); // camera exactly at the look-at target
	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Target.Get());
	Params.LookAtTarget = TargetOf(Target.Get());
	Director->AddRequest(Preset.Get(), Params);
	FFrameDriver Driver;
	Driver.Upstream.Rotation = FRotator(-10.f, 30.f, 0.f);
	FValidity Validity;
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("zero distance"), Frame);
	}
	TestEqual(FString::Printf(TEXT("Zero-distance look-at: valid rotation (%s)"), *Validity.First), Validity.Invalid, 0);
	const double Huge[] = { 1.0e8, 1.0e15, 1.0e30, 1.0e300 };
	for (const double Coordinate : Huge)
	{
		ForceTransform(Target.Get(), FVector(Coordinate, -Coordinate, Coordinate));
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("huge coordinate"), static_cast<int32>(FMath::LogX(10.0, Coordinate)));
		}
	}
	ForceTransform(Target.Get(), FVector(100.0, 0.0, 0.0));
	for (int32 Frame = 0; Frame < 5; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("back"), Frame);
	}
	TestEqual(FString::Printf(TEXT("Huge coordinates: no invalid output (%s)"), *Validity.First), Validity.Invalid, 0);
	TestTrue(TEXT("Back to normal coordinates"), Director->GetLastResult().Pose.Location.Equals(FVector(100.0, 0.0, 0.0), 1.0));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 6: ブレンドの途中で行き先の対象が不正になる。ブレンドは止まらずに終わり、復帰する。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFHealthBlendTest, "ActionCameraFramework.Health.InvalidTargetDuringBlend", ACFTests::Flags)
bool FACFHealthBlendTest::RunTest(const FString& Parameters)
{
	using namespace ACFHealthTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector(1000.0, 0.0, 0.0)));
	TStrongObjectPtr<UACFCameraPreset> A(MakeFixedPreset(FVector(0.0, 0.0, 300.0), 80.f));
	TStrongObjectPtr<UACFCameraPreset> B(MakeTrackingPreset(FVector(-300.0, 0.0, 100.0), 0.3f, 0.2f));
	B->DefaultBlendIn = Linear(1.f);
	Director->AddRequest(A.Get(), FACFCameraRequestParams());
	FFrameDriver Driver;
	FValidity Validity;
	Driver.Step(Director.Get(), 1.f / 60.f);
	FACFCameraRequestParams Params;
	Params.Priority = 5;
	Params.FollowTarget = TargetOf(Target.Get());
	Params.LookAtTarget = TargetOf(Target.Get());
	Director->AddRequest(B.Get(), Params);
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("blend"), Frame);
	}
	for (int32 Frame = 0; Frame < 15; ++Frame)
	{
		ForceTransform(Target.Get(), FVector(NaN, NaN, NaN));
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("invalid mid-blend"), Frame);
	}
	ForceTransform(Target.Get(), FVector(1200.0, 0.0, 0.0));
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("after"), Frame);
	}
	TestEqual(FString::Printf(TEXT("Invalid target mid-blend: no invalid output (%s)"), *Validity.First), Validity.Invalid, 0);
	TestFalse(TEXT("The blend finished"), Director->GetLastResult().bIsBlending);
	TestTrue(FString::Printf(TEXT("Recovered on the target (%s)"), *Director->GetLastResult().Pose.Location.ToString()),
		Director->GetLastResult().Pose.Location.Equals(FVector(900.0, 0.0, 100.0), 1.0));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 実行時の入力（カメラ入力、Orbit の回転、Blend From Pose、ブレンド時間、UE カメラ）に不正値。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFHealthRuntimeInputsTest, "ActionCameraFramework.Health.InvalidRuntimeInputs", ACFTests::Flags)
bool FACFHealthRuntimeInputsTest::RunTest(const FString& Parameters)
{
	using namespace ACFHealthTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector(0.0, 0.0, 0.0)));
	TStrongObjectPtr<UACFCameraPreset> Orbit(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
	UACFCameraNode_OrbitalFollow* Node = NewObject<UACFCameraNode_OrbitalFollow>(Orbit.Get());
	Node->InputSource = EACFCameraOrbitInputSource::CameraInput;
	Orbit->Nodes.Add(Node);
	Orbit->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Target.Get());
	const FACFCameraRequestHandle Handle = Director->AddRequest(Orbit.Get(), Params);
	FFrameDriver Driver;
	FValidity Validity;
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("before"), Frame);
	}
	Director->AddCameraInput(FVector2D(NaN, 1.0));
	Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("camera input NaN"), 0);
	Director->SetCameraInputRate(FVector2D(0.0, Inf));
	Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("input rate Inf"), 0);
	Director->SetOrbitRotation(Handle, FRotator(NaN, 0.0, 0.0), true);
	Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("orbit rotation NaN"), 0);
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("after inputs"), Frame);
	}
	FACFCameraPose BadPose;
	BadPose.Location = FVector(NaN, 0.0, 0.0);
	FACFCameraBlendSettings Blend = Linear(0.5f);
	Director->BlendFromPose(BadPose, Blend);
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("blend from NaN pose"), Frame);
	}
	// Blend duration NaN: the transition must still end.
	TStrongObjectPtr<UACFCameraPreset> Fixed(MakeFixedPreset(FVector(0.0, 0.0, 300.0), 80.f, Linear(static_cast<float>(NaN))));
	FACFCameraRequestParams High;
	High.Priority = 10;
	Director->AddRequest(Fixed.Get(), High);
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("blend duration NaN"), Frame);
	}
	TestFalse(TEXT("A NaN blend duration does not leave the transition running forever"), Director->GetLastResult().bIsBlending);
	// UE camera (upstream) NaN while ACF is in control.
	Driver.Upstream.Location = FVector(NaN, NaN, NaN);
	Driver.Upstream.FieldOfView = static_cast<float>(NaN);
	for (int32 Frame = 0; Frame < 5; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("upstream NaN"), Frame);
	}
	TestEqual(FString::Printf(TEXT("Invalid runtime inputs: no invalid output (%s)"), *Validity.First), Validity.Invalid, 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// ランダム化: 不正値を混ぜた入力を 2000 フレーム。出力は常に有効で、不正値をやめると復帰する。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFHealthFuzzTest, "ActionCameraFramework.Health.RandomizedInvalidInputs", ACFTests::Flags)
bool FACFHealthFuzzTest::RunTest(const FString& Parameters)
{
	using namespace ACFHealthTests;
	FRandomStream Random(6024);
	auto Value = [&Random]() -> double
	{
		switch (Random.RandRange(0, 9))
		{
		case 0: return NaN;
		case 1: return Inf;
		case 2: return -Inf;
		case 3: return 1.0e300;
		default: return Random.FRandRange(-2000.f, 2000.f);
		}
	};
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeTrackingPreset(FVector(-300.0, 0.0, 100.0), 0.25f, 0.15f, EACFCameraOffsetSpace::TargetLocal));
	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Target.Get());
	Params.LookAtTarget = TargetOf(Target.Get());
	Director->AddRequest(Preset.Get(), Params);
	FFrameDriver Driver;
	FValidity Validity;
	for (int32 Frame = 0; Frame < 2000; ++Frame)
	{
		const bool bBad = Random.FRand() < 0.3f;
		const FVector Location = bBad ? FVector(Value(), Value(), Value()) : FVector(Random.FRandRange(-500.f, 500.f), Random.FRandRange(-500.f, 500.f), 0.0);
		const FQuat Rotation = bBad && Random.FRand() < 0.5f ? FQuat(Value(), Value(), Value(), Value()) : FQuat(FRotator(0.f, Random.FRandRange(-180.f, 180.f), 0.f));
		ForceTransform(Target.Get(), Location, Rotation);
		const float Dt = bBad && Random.FRand() < 0.3f ? static_cast<float>(Value()) : 1.f / 60.f;
		Driver.Upstream.Location = bBad && Random.FRand() < 0.2f ? FVector(Value(), 0.0, 0.0) : FVector(0.0, 0.0, 500.0);
		Driver.Upstream.FieldOfView = bBad && Random.FRand() < 0.2f ? static_cast<float>(Value()) : 90.f;
		Validity.Check(Driver.Step(Director.Get(), Dt), TEXT("fuzz"), Frame);
	}
	const FVector Final(250.0, -100.0, 0.0);
	ForceTransform(Target.Get(), Final);
	Driver.Upstream.Location = FVector(0.0, 0.0, 500.0);
	Driver.Upstream.FieldOfView = 90.f;
	for (int32 Frame = 0; Frame < 300; ++Frame)
	{
		Validity.Check(Driver.Step(Director.Get(), 1.f / 60.f), TEXT("after fuzz"), Frame);
	}
	TestEqual(FString::Printf(TEXT("Randomized invalid inputs: frames with an invalid pose (%s)"), *Validity.First), Validity.Invalid, 0);
	TestTrue(FString::Printf(TEXT("Recovered after the invalid inputs stopped (%s)"), *Director->GetLastResult().Pose.Location.ToString()),
		Director->GetLastResult().Pose.Location.Equals(Final + FVector(-300.0, 0.0, 100.0), 1.0));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 10: 正常な入力では修正前と姿勢が一致する（Follow + LookAt + Orbit の入力 + 割り込みブレンドを 600 フレーム。全姿勢のビット列の
// チェックサムを修正前のビルドで記録した値と比べる）。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFHealthNormalUnchangedTest, "ActionCameraFramework.Health.NormalInputUnchanged", ACFTests::Flags)
bool FACFHealthNormalUnchangedTest::RunTest(const FString& Parameters)
{
	using namespace ACFHealthTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Target(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<UACFCameraPreset> Track(MakeTrackingPreset(FVector(-300.0, 50.0, 120.0), 0.25f, 0.15f, EACFCameraOffsetSpace::TargetLocal));
	TStrongObjectPtr<UACFCameraPreset> Orbit(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
	UACFCameraNode_OrbitalFollow* Node = NewObject<UACFCameraNode_OrbitalFollow>(Orbit.Get());
	Node->InputSource = EACFCameraOrbitInputSource::CameraInput;
	Orbit->Nodes.Add(Node);
	Orbit->DefaultBlendIn = Linear(0.7f);
	TStrongObjectPtr<UACFCameraPreset> Fixed(MakeFixedPreset(FVector(200.0, 300.0, 400.0), 65.f, Linear(0.4f)));
	FACFCameraRequestParams Params;
	Params.FollowTarget = TargetOf(Target.Get());
	Params.LookAtTarget = TargetOf(Target.Get());
	Director->AddRequest(Track.Get(), Params);
	FFrameDriver Driver;
	Driver.Upstream.Location = FVector(10.0, 20.0, 300.0);
	Driver.Upstream.FieldOfView = 90.f;
	uint32 Hash = 0;
	FACFCameraRequestHandle OrbitHandle;
	FACFCameraRequestHandle FixedHandle;
	for (int32 Frame = 0; Frame < 600; ++Frame)
	{
		const double T = Frame / 60.0;
		ForceTransform(Target.Get(), FVector(400.0 * FMath::Cos(T), 300.0 * FMath::Sin(T * 1.3), 20.0 * FMath::Sin(T * 3.0)), FQuat(FRotator(0.0, 40.0 * T, 0.0)));
		if (Frame == 100)
		{
			FACFCameraRequestParams OrbitParams = Params;
			OrbitParams.Priority = 5;
			OrbitHandle = Director->AddRequest(Orbit.Get(), OrbitParams);
		}
		if (Frame > 100 && Frame < 400)
		{
			Director->AddCameraInput(FVector2D(0.8, -0.2 * FMath::Sin(T)));
		}
		if (Frame == 300)
		{
			FACFCameraRequestParams FixedParams;
			FixedParams.Priority = 10;
			FixedHandle = Director->AddRequest(Fixed.Get(), FixedParams);
		}
		if (Frame == 310)
		{
			Director->RemoveRequest(FixedHandle, nullptr, false); // interrupt the blend
		}
		if (Frame == 450)
		{
			Director->RemoveRequest(OrbitHandle, nullptr, false);
		}
		const float Dt = (Frame % 7 == 0) ? 1.f / 30.f : 1.f / 60.f;
		const FACFCameraEvaluationResult& Result = Driver.Step(Director.Get(), Dt);
		Hash = FCrc::MemCrc32(&Result.Pose.Location, sizeof(FVector), Hash);
		Hash = FCrc::MemCrc32(&Result.Pose.Rotation, sizeof(FRotator), Hash);
		Hash = FCrc::MemCrc32(&Result.Pose.FieldOfView, sizeof(float), Hash);
		Hash = FCrc::MemCrc32(&Result.Weight, sizeof(float), Hash);
	}
	// Recorded with the Phase 6B / B-2 build (before B-4), same scenario, Development Win64.
	constexpr uint32 Expected = 0xe714f4cdu;
	AddInfo(FString::Printf(TEXT("Normal input pose checksum: 0x%08x"), Hash));
	TestEqual(TEXT("Normal input: every pose identical to the build before B-4 (bit-exact checksum)"), Hash, Expected);
	TestEqual(TEXT("Normal input: no health issue reported"), Director->GetHealthDiagnostics().TotalIssues, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
