#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestUtils.h"
#include "ACFCameraSettings.h"
#include "ACFCameraTestShake.h"
#include "Curves/CurveFloat.h"
#include "Effects/ACFCameraBuiltinEffects.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

/** Phase 3 camera effects: envelope, handles, composition rules, FOV limits, attenuation, shakes. */
namespace ACFEffectTests
{
	using namespace ACFTests;

	/** Fixed ACF camera at the origin looking along +X (yaw Yaw), FOV 90, so effects are visible directly. */
	struct FRig
	{
		TStrongObjectPtr<UACFCameraDirector> Director;
		TStrongObjectPtr<UACFCameraPreset> Camera;
		FFrameDriver Driver;

		explicit FRig(float Yaw = 0.f, float Fov = 90.f)
			: Director(NewObject<UACFCameraDirector>())
		{
			UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
			UACFCameraNode_FixedPose* Node = NewObject<UACFCameraNode_FixedPose>(Preset);
			Node->Location = FVector::ZeroVector;
			Node->Rotation = FRotator(0.f, Yaw, 0.f);
			Node->FieldOfView = Fov;
			Preset->Nodes.Add(Node);
			Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
			Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
			Camera.Reset(Preset);
			Director->AddRequest(Preset, FACFCameraRequestParams());
			Driver.Step(Director.Get(), 1 / 60.f);
		}

		const FACFCameraEvaluationResult& Step(float Dt) { return Driver.Step(Director.Get(), Dt); }
	};

	inline FACFCameraEffectEnvelope Envelope(float Attack, float Sustain, float Release)
	{
		FACFCameraEffectEnvelope E;
		E.Attack = Attack;
		E.Sustain = Sustain;
		E.Release = Release;
		E.Shape = EACFCameraEnvelopeShape::Linear;
		return E;
	}

	inline UACFCameraEffect_Punch* Punch(UACFCameraEffectPreset* Preset, const FVector& Location, const FACFCameraEffectEnvelope& Env,
		EACFCameraEffectSpace Space = EACFCameraEffectSpace::Camera)
	{
		UACFCameraEffect_Punch* Layer = NewObject<UACFCameraEffect_Punch>(Preset);
		Layer->Location = Location;
		Layer->Space = Space;
		Layer->Envelope = Env;
		Preset->Layers.Add(Layer);
		return Layer;
	}

	inline UACFCameraEffect_Punch* FovLayer(UACFCameraEffectPreset* Preset, EACFCameraEffectBlendMode Mode, float Value, int32 Priority = 0, float Weight = 1.f)
	{
		UACFCameraEffect_Punch* Layer = Punch(Preset, FVector::ZeroVector, Envelope(0.f, -1.f, 0.f));
		Layer->BlendMode = Mode;
		Layer->FOV = Value;
		Layer->Priority = Priority;
		Layer->Weight = Weight;
		return Layer;
	}

	inline UACFCameraEffectPreset* NewEffect()
	{
		return NewObject<UACFCameraEffectPreset>(GetTransientPackage(), NAME_None, RF_Transient);
	}

	inline int32 CountLiveTestShakes()
	{
		int32 Count = 0;
		for (TObjectIterator<UACFTestCameraShake> It; It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject) && IsValid(*It))
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectEnvelopeTest, "ActionCameraFramework.Effects.PlayEnvelopeAndEnd", ACFTests::Flags)
bool FACFEffectEnvelopeTest::RunTest(const FString& Parameters)
{
	using namespace ACFEffectTests;
	FRig Rig;
	TStrongObjectPtr<UACFCameraEffectPreset> Effect(NewEffect());
	Punch(Effect.Get(), FVector(-100, 0, 0), Envelope(0.1f, 0.2f, 0.3f));
	const FACFCameraEffectHandle Handle = Rig.Director->PlayEffect(Effect.Get(), FACFCameraEffectParams());
	TestTrue(TEXT("Handle valid"), Handle.IsValid());

	TestTrue(TEXT("Attack (t=0.05): half"), FMath::IsNearlyEqual(Rig.Step(0.05f).Pose.Location.X, -50.0, 0.01));
	TestTrue(TEXT("Sustain (t=0.2): full"), FMath::IsNearlyEqual(Rig.Step(0.15f).Pose.Location.X, -100.0, 0.01));
	TestTrue(TEXT("Release (t=0.45): half"), FMath::IsNearlyEqual(Rig.Step(0.25f).Pose.Location.X, -50.0, 0.01));
	TestEqual(TEXT("Still active during release"), Rig.Director->GetNumActiveEffects(), 1);
	TestTrue(TEXT("Ended (t=0.65): no offset"), FMath::IsNearlyEqual(Rig.Step(0.2f).Pose.Location.X, 0.0, 0.01));
	TestEqual(TEXT("Instance and runtime state removed after the end"), Rig.Director->GetNumActiveEffects(), 0);
	TestFalse(TEXT("Handle no longer active"), Rig.Director->IsEffectActive(Handle));
	TestFalse(TEXT("Stopping a finished effect is a no-op"), Rig.Director->StopEffect(Handle, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectHandleStopTest, "ActionCameraFramework.Effects.HandleStopsOnlyItsEffect", ACFTests::Flags)
bool FACFEffectHandleStopTest::RunTest(const FString& Parameters)
{
	using namespace ACFEffectTests;
	FRig Rig;
	TStrongObjectPtr<UACFCameraEffectPreset> A(NewEffect());
	TStrongObjectPtr<UACFCameraEffectPreset> B(NewEffect());
	Punch(A.Get(), FVector(-10, 0, 0), Envelope(0.f, -1.f, 0.2f));
	Punch(B.Get(), FVector(0, 0, 20), Envelope(0.f, -1.f, 0.2f));
	const FACFCameraEffectHandle HA = Rig.Director->PlayEffect(A.Get(), FACFCameraEffectParams());
	const FACFCameraEffectHandle HB = Rig.Director->PlayEffect(B.Get(), FACFCameraEffectParams());
	TestTrue(TEXT("Both effects stack"), Rig.Step(1 / 60.f).Pose.Location.Equals(FVector(-10, 0, 20), 0.01));

	TestTrue(TEXT("Stop A (release)"), Rig.Director->StopEffect(HA, false));
	TestTrue(TEXT("A releases, B untouched"), Rig.Step(0.1f).Pose.Location.Equals(FVector(-5, 0, 20), 0.01));
	Rig.Step(0.15f);
	TestFalse(TEXT("A finished"), Rig.Director->IsEffectActive(HA));
	TestTrue(TEXT("B still active"), Rig.Director->IsEffectActive(HB));
	TestTrue(TEXT("Only B remains"), Rig.Director->GetLastResult().Pose.Location.Equals(FVector(0, 0, 20), 0.01));

	TestTrue(TEXT("Stop B immediately"), Rig.Director->StopEffect(HB, true));
	TestEqual(TEXT("Immediate stop removes at once"), Rig.Director->GetNumActiveEffects(), 0);
	TestTrue(TEXT("No offset left"), Rig.Step(1 / 60.f).Pose.Location.Equals(FVector::ZeroVector, 0.01));
	TestFalse(TEXT("Unknown handle"), Rig.Director->StopEffect(FACFCameraEffectHandle(999999), false));

	// StopAll + same-frame play/stop.
	Rig.Director->PlayEffect(A.Get(), FACFCameraEffectParams());
	Rig.Director->PlayEffect(B.Get(), FACFCameraEffectParams());
	Rig.Director->StopAllEffects(false);
	Rig.Step(1 / 60.f);
	TestEqual(TEXT("Effects stopped before their first frame never run"), Rig.Director->GetNumActiveEffects(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectSpaceTest, "ActionCameraFramework.Effects.AdditiveLocalAndWorldSpace", ACFTests::Flags)
bool FACFEffectSpaceTest::RunTest(const FString& Parameters)
{
	using namespace ACFEffectTests;
	FRig Rig(90.f); // camera looks along +Y
	TStrongObjectPtr<UACFCameraEffectPreset> Local(NewEffect());
	TStrongObjectPtr<UACFCameraEffectPreset> World(NewEffect());
	Punch(Local.Get(), FVector(100, 0, 0), Envelope(0.f, -1.f, 0.f), EACFCameraEffectSpace::Camera);
	Punch(World.Get(), FVector(0, 0, 50), Envelope(0.f, -1.f, 0.f), EACFCameraEffectSpace::World);
	Rig.Director->PlayEffect(Local.Get(), FACFCameraEffectParams());
	FACFCameraEffectParams Half;
	Half.Scale = 0.5f;
	Rig.Director->PlayEffect(World.Get(), Half);
	TestTrue(TEXT("Camera-space forward follows the camera; world offset scaled by Params.Scale"),
		Rig.Step(1 / 60.f).Pose.Location.Equals(FVector(0, 100, 25), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectCompositionTest, "ActionCameraFramework.Effects.OverridePriorityMultiplyAdditiveFov", ACFTests::Flags)
bool FACFEffectCompositionTest::RunTest(const FString& Parameters)
{
	using namespace ACFEffectTests;
	auto Run = [](TFunctionRef<void(UACFCameraEffectPreset*)> Build, float BaseFov = 90.f) -> float
	{
		FRig Rig(0.f, BaseFov);
		TStrongObjectPtr<UACFCameraEffectPreset> Effect(NewEffect());
		Build(Effect.Get());
		Rig.Director->PlayEffect(Effect.Get(), FACFCameraEffectParams());
		return Rig.Step(1 / 60.f).Pose.FieldOfView;
	};
	using Mode = EACFCameraEffectBlendMode;
	TestTrue(TEXT("Higher priority override ends on top (100)"), FMath::IsNearlyEqual(Run([](UACFCameraEffectPreset* P) { FovLayer(P, Mode::Override, 60, 1); FovLayer(P, Mode::Override, 100, 5); }), 100.f, 0.01f));
	TestTrue(TEXT("Swapped priorities (60)"), FMath::IsNearlyEqual(Run([](UACFCameraEffectPreset* P) { FovLayer(P, Mode::Override, 60, 5); FovLayer(P, Mode::Override, 100, 1); }), 60.f, 0.01f));
	TestTrue(TEXT("Weighted overrides chain in priority order: 90 -> 75 -> 87.5"),
		FMath::IsNearlyEqual(Run([](UACFCameraEffectPreset* P) { FovLayer(P, Mode::Override, 100, 2, 0.5f); FovLayer(P, Mode::Override, 60, 1, 0.5f); }), 87.5f, 0.01f));
	TestTrue(TEXT("Multiply then additive: 90 * 1.1 + 5"),
		FMath::IsNearlyEqual(Run([](UACFCameraEffectPreset* P) { FovLayer(P, Mode::Additive, 5); FovLayer(P, Mode::Multiply, 1.1f); }), 104.f, 0.01f));
	TestTrue(TEXT("Override before multiply: (90 -> 80) * 1.25"),
		FMath::IsNearlyEqual(Run([](UACFCameraEffectPreset* P) { FovLayer(P, Mode::Multiply, 1.25f); FovLayer(P, Mode::Override, 80); }), 100.f, 0.01f));

	const UACFCameraSettings* Settings = GetDefault<UACFCameraSettings>();
	TestTrue(TEXT("FOV upper limit"), FMath::IsNearlyEqual(Run([](UACFCameraEffectPreset* P) { FovLayer(P, Mode::Multiply, 1.2f); FovLayer(P, Mode::Multiply, 1.5f); }), Settings->EffectFovMax, 0.01f));
	TestTrue(TEXT("FOV lower limit"), FMath::IsNearlyEqual(Run([](UACFCameraEffectPreset* P) { FovLayer(P, Mode::Additive, -100); }), Settings->EffectFovMin, 0.01f));
	TestTrue(TEXT("A base FOV already outside the limits is kept, effects cannot push it further out"), FMath::IsNearlyEqual(Run([](UACFCameraEffectPreset* P) { FovLayer(P, Mode::Additive, 5.f); }, 150.f), 150.f, 0.01f));
	TestTrue(TEXT("...but can bring it back in"), FMath::IsNearlyEqual(Run([](UACFCameraEffectPreset* P) { FovLayer(P, Mode::Additive, -30.f); }, 150.f), 120.f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectAttenuationTest, "ActionCameraFramework.Effects.ImpulseAttenuationAndCurve", ACFTests::Flags)
bool FACFEffectAttenuationTest::RunTest(const FString& Parameters)
{
	using namespace ACFEffectTests;
	FRig Rig;
	TStrongObjectPtr<UACFCameraEffectPreset> Effect(NewEffect());
	UACFCameraEffect_Impulse* Impulse = NewObject<UACFCameraEffect_Impulse>(Effect.Get());
	Impulse->Frequency = 0.f; // single push
	Impulse->Amplitude = 100.f;
	Impulse->RotationAmplitude = FRotator::ZeroRotator;
	Impulse->Envelope = Envelope(0.f, -1.f, 0.f);
	Impulse->DistanceAttenuation.InnerRadius = 100.f;
	Impulse->DistanceAttenuation.OuterRadius = 1100.f;
	Effect->Layers.Add(Impulse);

	FACFCameraEffectParams Params;
	Params.bUseSourceLocation = true;
	Params.SourceLocation = FVector(0, -600, 0); // 600 cm away -> 50 %
	const FACFCameraEffectHandle Handle = Rig.Director->PlayEffect(Effect.Get(), Params);
	TestTrue(TEXT("Pushed away from the source at half strength"), Rig.Step(1 / 60.f).Pose.Location.Equals(FVector(0, 50, 0), 0.01));
	Rig.Director->StopEffect(Handle, true);

	Params.SourceLocation = FVector(0, -5000, 0);
	Rig.Director->PlayEffect(Effect.Get(), Params);
	TestTrue(TEXT("Beyond the outer radius: nothing"), Rig.Step(1 / 60.f).Pose.Location.Equals(FVector::ZeroVector, 0.01));
	Rig.Director->StopAllEffects(true);

	// Intensity curve over time (seconds -> multiplier).
	TStrongObjectPtr<UCurveFloat> Curve(NewObject<UCurveFloat>(GetTransientPackage(), NAME_None, RF_Transient));
	Curve->FloatCurve.AddKey(0.f, 0.f);
	Curve->FloatCurve.AddKey(1.f, 1.f);
	TStrongObjectPtr<UACFCameraEffectPreset> Curved(NewEffect());
	Punch(Curved.Get(), FVector(0, 0, 40), Envelope(0.f, -1.f, 0.f))->IntensityCurve = Curve.Get();
	Rig.Director->PlayEffect(Curved.Get(), FACFCameraEffectParams());
	Rig.Step(0.25f);
	TestTrue(TEXT("Curve at t=0.5 -> half"), FMath::IsNearlyEqual(Rig.Step(0.25f).Pose.Location.Z, 20.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectRecoilTest, "ActionCameraFramework.Effects.RecoilStacksDeterministicAndAim", ACFTests::Flags)
bool FACFEffectRecoilTest::RunTest(const FString& Parameters)
{
	using namespace ACFEffectTests;
	FRig Rig;
	TStrongObjectPtr<UACFCameraEffectPreset> Effect(NewEffect());
	UACFCameraEffect_Recoil* Recoil = NewObject<UACFCameraEffect_Recoil>(Effect.Get());
	Recoil->Envelope = Envelope(0.f, 1.f, 0.f);
	Recoil->YawSpread = 0.f;
	Recoil->RollSpread = 0.f;
	Effect->Layers.Add(Recoil);
	Rig.Director->PlayEffect(Effect.Get(), FACFCameraEffectParams());
	Rig.Director->PlayEffect(Effect.Get(), FACFCameraEffectParams());
	const FACFCameraEvaluationResult& Result = Rig.Step(1 / 60.f);
	TestTrue(TEXT("Two shots stack: pitch"), FMath::IsNearlyEqual(Result.Pose.Rotation.Pitch, 2.0 * Recoil->PitchKick, 0.01));
	TestTrue(TEXT("Two shots stack: kick back"), FMath::IsNearlyEqual(Result.Pose.Location.X, -2.0 * Recoil->KickBack, 0.01));
	TestTrue(TEXT("Aim pose excludes rotation effects"), FMath::IsNearlyEqual(Result.AimPose.Rotation.Pitch, 0.0, 0.01));

	// Spread is deterministic per seed.
	Recoil->YawSpread = 3.f;
	auto YawFor = [&](int32 Seed)
	{
		FRig Other;
		FACFCameraEffectParams Params;
		Params.RandomSeed = Seed;
		Other.Director->PlayEffect(Effect.Get(), Params);
		return Other.Step(1 / 60.f).Pose.Rotation.Yaw;
	};
	const double Yaw1 = YawFor(42);
	TestTrue(TEXT("Same seed -> same spread"), FMath::IsNearlyEqual(Yaw1, YawFor(42), 1e-4));
	TestTrue(TEXT("Spread within range"), FMath::Abs(Yaw1) <= 3.0 + 1e-4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectLifetimeTest, "ActionCameraFramework.Effects.InstigatorAndFrameGuard", ACFTests::Flags)
bool FACFEffectLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace ACFEffectTests;
	FRig Rig;
	TStrongObjectPtr<UACFCameraEffectPreset> Effect(NewEffect());
	Punch(Effect.Get(), FVector(-100, 0, 0), Envelope(1.f, -1.f, 0.f));

	UACFCameraEffectPreset* Instigator = NewEffect();
	FACFCameraEffectParams Params;
	Params.Instigator = Instigator;
	Rig.Director->PlayEffect(Effect.Get(), Params);
	TestTrue(TEXT("Attack at t=0.25"), FMath::IsNearlyEqual(Rig.Step(0.25f).Pose.Location.X, -25.0, 0.01));
	TestTrue(TEXT("Same-frame repeat does not advance the envelope"), FMath::IsNearlyEqual(Rig.Driver.Repeat(Rig.Director.Get(), 0.25f).Pose.Location.X, -25.0, 0.01));
	Instigator->MarkAsGarbage();
	TestTrue(TEXT("Instigator gone -> effect stopped"), FMath::IsNearlyEqual(Rig.Step(0.25f).Pose.Location.X, 0.0, 0.01));
	TestEqual(TEXT("No instance left"), Rig.Director->GetNumActiveEffects(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectShakeUnitTest, "ActionCameraFramework.Effects.ShakeLayerAppliedOnceAndFreed", ACFTests::Flags)
bool FACFEffectShakeUnitTest::RunTest(const FString& Parameters)
{
	using namespace ACFEffectTests;
	FRig Rig(90.f);
	TStrongObjectPtr<UACFCameraEffectPreset> Effect(NewEffect());
	UACFCameraEffect_Shake* Shake = NewObject<UACFCameraEffect_Shake>(Effect.Get());
	Shake->ShakeClass = UACFTestCameraShake::StaticClass(); // constant 60 cm backwards (camera local)
	Effect->Layers.Add(Shake);
	const int32 ShakesBefore = CountLiveTestShakes();
	const FACFCameraEffectHandle Handle = Rig.Director->PlayEffect(Effect.Get(), FACFCameraEffectParams());
	const FACFCameraEvaluationResult& Result = Rig.Step(1 / 60.f);
	TestTrue(TEXT("UE shake pattern applied through ACF (camera local, once)"), Result.Pose.Location.Equals(FVector(0, -60, 0), 0.01));
	TestTrue(TEXT("Shake location is part of the aim-independent pre-collision pose; rotation untouched"), Result.Pose.Rotation.Equals(FRotator(0, 90, 0), 0.01f));
	TestEqual(TEXT("One ACF-owned shake instance"), CountLiveTestShakes(), ShakesBefore + 1);

	Rig.Director->StopEffect(Handle, false); // the shake's own (zero) blend out
	Rig.Step(1 / 60.f);
	TestEqual(TEXT("Layer removed when the shake finished"), Rig.Director->GetNumActiveEffects(), 0);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestEqual(TEXT("Shake instance released (no leaked runtime state)"), CountLiveTestShakes(), ShakesBefore);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
