#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ACFCameraTestUtils.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "ACFCameraPresetValidation.h"
#include "Algo/Count.h"
#endif

/**
 * Phase 7B-1 / 7B-2: Orbital Follow movement assistance (7B-2: input activity, real-time input rate, tuning) (Recenter Heading Movement Direction) and Suppress Camera Assistance,
 * director level and deterministic (fixed steps, the target is a scene component moved by the test). README 29 章.
 *
 * Default rig: camera yaw 0 (looking along +X, the camera behind at -X), Recenter Delay 0.5 s, Recenter Time 0.3 s,
 * Start / Stop 150 / 75 cm/s, Required Movement Duration 0.3 s, Backward Exclusion 45 deg, dead zone 0, max 90 deg/s, no
 * rotation damping, no pitch recentering. The target's velocity is smoothed by its tracking (Lookahead Smoothing 0.25 s).
 */
namespace ACFAssistTests
{
	using namespace ACFTests;
	constexpr float Dt60 = 1.f / 60.f;

	inline float Delta(float A, float B) { return FRotator::NormalizeAxis(A - B); }

	inline FVector Heading(float YawDegrees, float Speed)
	{
		const float R = FMath::DegreesToRadians(YawDegrees);
		return FVector(FMath::Cos(R) * Speed, FMath::Sin(R) * Speed, 0.f);
	}

	inline void Defaults(UACFCameraNode_OrbitalFollow& O)
	{
		O.Distance = 400.f;
		O.PivotOffset = FVector::ZeroVector;
		O.InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
		O.InitialYaw = 0.f;
		O.InitialPitch = -15.f;
		O.bEnableMovementAssistance = true;
		O.RecenterHeading = EACFCameraRecenterHeading::MovementDirection;
		O.RecenterDelay = 0.5f;
		O.RecenterTime = 0.3f;
		O.bRecenterPitch = false;
		O.StartSpeedThreshold = 150.f;
		O.StopSpeedThreshold = 75.f;
		O.RequiredMovementDuration = 0.3f;
		O.BackwardExclusionAngle = 45.f;
		O.AngularDeadZone = 0.f;
		O.MaxAssistanceYawSpeed = 90.f;
	}

	/** One director, one Orbital Follow request following a moving scene component. */
	struct FRig
	{
		TStrongObjectPtr<UACFCameraDirector> Director;
		TStrongObjectPtr<USceneComponent> Target;
		TStrongObjectPtr<UACFCameraPreset> Preset;
		UACFCameraNode_OrbitalFollow* Orbit = nullptr;
		FACFCameraRequestHandle Handle;
		FFrameDriver Driver;
		FVector Velocity = FVector::ZeroVector;
		int32 Frames = 0;

		explicit FRig(TFunction<void(UACFCameraNode_OrbitalFollow&)> Configure = nullptr, uint64 FirstFrame = 1000)
		{
			Director.Reset(NewObject<UACFCameraDirector>());
			Target.Reset(NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient));
			Target->SetMobility(EComponentMobility::Movable);
			Target->SetWorldLocation(FVector(0.0, 0.0, 100.0));
			Preset.Reset(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
			Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
			Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
			Orbit = NewObject<UACFCameraNode_OrbitalFollow>(Preset.Get());
			Defaults(*Orbit);
			if (Configure)
			{
				Configure(*Orbit);
			}
			Preset->Nodes.Add(Orbit);
			Driver.Frame = FirstFrame;
			Driver.Upstream.FieldOfView = 90.f;
			Handle = Director->AddRequest(Preset.Get(), Params());
		}

		FACFCameraRequestParams Params() const
		{
			FACFCameraRequestParams P;
			P.FollowTarget.Component = Target.Get();
			return P;
		}

		const FACFCameraEvaluationResult& Step(float Dt)
		{
			const float Move = FMath::IsFinite(Dt) && Dt > 0.f ? Dt : 0.f;
			Target->SetWorldLocation(Target->GetComponentLocation() + Velocity * Move);
			++Frames;
			return Driver.Step(Director.Get(), Dt);
		}

		void Run(float Seconds, float Dt, TFunction<void(int32)> PerFrame = nullptr)
		{
			const int32 N = FMath::RoundToInt(Seconds / Dt);
			for (int32 I = 0; I < N; ++I)
			{
				Step(Dt);
				if (PerFrame)
				{
					PerFrame(I);
				}
			}
		}

		/** Steps until Predicate holds (max MaxFrames); returns the number of steps taken, INDEX_NONE if never. */
		int32 StepUntil(float Dt, int32 MaxFrames, TFunctionRef<bool()> Predicate)
		{
			for (int32 I = 1; I <= MaxFrames; ++I)
			{
				Step(Dt);
				if (Predicate())
				{
					return I;
				}
			}
			return INDEX_NONE;
		}

		float Yaw() const { return Director->GetLastResult().Pose.Rotation.Yaw; }
		const FACFCameraRigDebug& Debug() const { return Director->GetLastDebug(); }
		EACFCameraAssistanceState State() const { return Debug().AssistanceState; }
		EACFCameraAssistanceReason Reason() const { return Debug().AssistanceReason; }
		bool Assisting() const { return State() == EACFCameraAssistanceState::Assisting; }
	};

	inline FString Describe(const FRig& R)
	{
		return FString::Printf(TEXT("state %s, reason %s, yaw %.3f, speed %.1f, wait %.3f, moving %.3f"), *UEnum::GetValueAsString(R.State()),
			*UEnum::GetValueAsString(R.Reason()), R.Yaw(), R.Debug().AssistanceMovementSpeed, R.Debug().AssistanceWaitTime, R.Debug().AssistanceMovementTime);
	}
}

// -------------------------------------------------------------------------------------------------
// 手動入力が優先: 入力のフレームは補助しない（入力量だけ動く）。入力停止後は Recenter Delay から待ち直す。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistManualInputTest, "ActionCameraFramework.Assistance.ManualInputHasPriority", ACFTests::Flags)
bool FACFAssistManualInputTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	FRig R;
	R.Velocity = Heading(90.f, 400.f);
	const int32 ToAssist = R.StepUntil(Dt60, 600, [&R] { return R.Assisting(); });
	if (!TestTrue(TEXT("Assistance starts while moving sideways"), ToAssist != INDEX_NONE))
	{
		return false;
	}
	R.Run(0.2f, Dt60);
	TestTrue(FString::Printf(TEXT("Turning towards the movement (%s)"), *Describe(R)), R.Yaw() > 5.f && R.Yaw() < 85.f && R.Reason() == EACFCameraAssistanceReason::None);

	// One frame of mouse input: exactly the input, no assistance step on top.
	const float Before = R.Yaw();
	R.Director->AddCameraInput(FVector2D(-5.0, 0.0));
	R.Step(Dt60);
	TestTrue(FString::Printf(TEXT("Input frame: yaw moved by the input only (%.4f -> %.4f)"), Before, R.Yaw()), FMath::IsNearlyEqual(Delta(R.Yaw(), Before), -5.f, 1e-3f));
	TestTrue(TEXT("Input frame: Suppressed / Manual Input"), R.State() == EACFCameraAssistanceState::Suppressed && R.Reason() == EACFCameraAssistanceReason::ManualInput);

	// After the input: Recenter Delay (0.5 s = 30 frames) again; the movement duration already ran (overlapping waits).
	const float Held = R.Yaw();
	bool bHeld = true;
	const int32 Resume = R.StepUntil(Dt60, 120, [&]
	{
		bHeld &= R.Assisting() || R.Yaw() == Held;
		return R.Assisting();
	});
	TestTrue(TEXT("Yaw held while waiting after input"), bHeld);
	TestTrue(FString::Printf(TEXT("Resumes after Recenter Delay only (%d frames, expected 30)"), Resume), Resume >= 29 && Resume <= 31);

	// Held stick (rate input every frame): the assistance never turns, the camera follows the rate exactly.
	bool bOnlyInput = true;
	for (int32 I = 0; I < 60; ++I)
	{
		const float Y0 = R.Yaw();
		R.Director->SetCameraInputRate(FVector2D(30.0, 0.0));
		R.Step(Dt60);
		bOnlyInput &= FMath::IsNearlyEqual(Delta(R.Yaw(), Y0), 30.f * Dt60, 1e-3f) && R.Reason() == EACFCameraAssistanceReason::ManualInput;
	}
	TestTrue(TEXT("Held stick: every frame moves by the rate only, assistance stopped"), bOnlyInput);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 開始条件: 速度・継続時間・Recenter Delay（2 つの待ちは並行して数える）。静止中は Recenter When Idle でも回らない。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistStartConditionsTest, "ActionCameraFramework.Assistance.StartConditionsAndDuration", ACFTests::Flags)
bool FACFAssistStartConditionsTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	FRig R([](UACFCameraNode_OrbitalFollow& O) { O.bRecenterWhenIdle = true; O.InitialYaw = 0.f; });
	R.Director->SetOrbitRotation(R.Handle, FRotator(-15.f, 40.f, 0.f), true); // away from every heading used below
	R.Step(Dt60);
	R.Run(0.2f, Dt60);
	TestTrue(FString::Printf(TEXT("Standing, 0.2 s: waiting for Recenter Delay (%s)"), *Describe(R)), R.Reason() == EACFCameraAssistanceReason::RecenterDelay);
	R.Run(1.0f, Dt60);
	TestTrue(FString::Printf(TEXT("Standing, 1.2 s: waiting for movement (%s)"), *Describe(R)), R.State() == EACFCameraAssistanceState::Waiting && R.Reason() == EACFCameraAssistanceReason::BelowSpeed);
	TestTrue(TEXT("Standing: no recenter even with Recenter When Idle (assistance decides)"), FMath::IsNearlyEqual(R.Yaw(), 40.f, 1e-4f));

	// Start moving at 400 cm/s along 0 deg: moving once the smoothed speed passes 150, assisting 0.3 s later.
	R.Velocity = Heading(0.f, 400.f);
	int32 MovingFrame = INDEX_NONE;
	int32 AssistFrame = INDEX_NONE;
	bool bStill = true;
	for (int32 I = 1; I <= 120 && AssistFrame == INDEX_NONE; ++I)
	{
		R.Step(Dt60);
		if (MovingFrame == INDEX_NONE && R.Debug().AssistanceMovementTime > 0.f)
		{
			MovingFrame = I;
			TestTrue(FString::Printf(TEXT("Moving at the start threshold (%.1f cm/s)"), R.Debug().AssistanceMovementSpeed), R.Debug().AssistanceMovementSpeed >= 150.f);
		}
		if (R.Assisting())
		{
			AssistFrame = I;
		}
		else
		{
			bStill &= FMath::IsNearlyEqual(R.Yaw(), 40.f, 1e-4f);
		}
	}
	// 400 (1 - exp(-t / 0.25)) >= 150 at t = 0.1175 s: frame 8. The duration (18 frames) then decides.
	TestTrue(FString::Printf(TEXT("Moving from frame %d (expected 8)"), MovingFrame), MovingFrame >= 7 && MovingFrame <= 9);
	TestTrue(FString::Printf(TEXT("Assisting %d frames after moving (expected 18)"), AssistFrame - MovingFrame), AssistFrame - MovingFrame >= 17 && AssistFrame - MovingFrame <= 19);
	TestTrue(TEXT("No rotation before the start"), bStill);
	R.Step(Dt60);
	TestTrue(TEXT("Turns towards the heading once assisting"), R.Yaw() < 40.f);

	// Longer Recenter Delay than the movement duration: the delay decides (the waits overlap, they do not add up).
	FRig L([](UACFCameraNode_OrbitalFollow& O) { O.RecenterDelay = 1.0f; O.RequiredMovementDuration = 0.3f; });
	L.Director->SetOrbitRotation(L.Handle, FRotator(-15.f, 40.f, 0.f), true);
	L.Velocity = Heading(0.f, 400.f);
	const int32 First = L.StepUntil(Dt60, 240, [&L] { return L.Assisting(); });
	TestTrue(FString::Printf(TEXT("Delay 1.0 s and duration 0.3 s overlap: assisting after %d frames (expected 60)"), First), First >= 59 && First <= 62);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 速度のヒステリシス: Start と Stop の間では状態を保つ。閾値付近の速度でも振動しない。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistHysteresisTest, "ActionCameraFramework.Assistance.SpeedHysteresis", ACFTests::Flags)
bool FACFAssistHysteresisTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	{
		FRig R;
		R.Velocity = Heading(90.f, 300.f);
		R.Run(1.5f, Dt60);
		TestTrue(FString::Printf(TEXT("300 cm/s: moving (%s)"), *Describe(R)), R.Debug().AssistanceMovementTime > 0.f);
		R.Velocity = Heading(90.f, 100.f); // between Stop (75) and Start (150)
		R.Run(2.0f, Dt60);
		TestTrue(FString::Printf(TEXT("100 cm/s after moving: still moving (%s)"), *Describe(R)), R.Debug().AssistanceMovementTime > 3.f && R.Assisting());
		R.Velocity = Heading(90.f, 50.f);
		R.Run(2.0f, Dt60);
		TestTrue(FString::Printf(TEXT("50 cm/s: stopped (%s)"), *Describe(R)), R.Reason() == EACFCameraAssistanceReason::BelowSpeed);
		R.Velocity = Heading(90.f, 100.f);
		R.Run(2.0f, Dt60);
		TestTrue(FString::Printf(TEXT("100 cm/s from standing: not moving (below Start) (%s)"), *Describe(R)), R.Reason() == EACFCameraAssistanceReason::BelowSpeed);
	}

	// Speed hovering around the start threshold (raw 140 / 160 alternating, smoothed ~150 +/- 0.3).
	auto CountStarts = [](float Stop)
	{
		FRig R([Stop](UACFCameraNode_OrbitalFollow& O) { O.StopSpeedThreshold = Stop; });
		int32 Starts = 0;
		bool bWasMoving = false;
		for (int32 I = 0; I < 360; ++I)
		{
			R.Velocity = Heading(90.f, (I % 2) ? 160.f : 140.f);
			R.Step(Dt60);
			const bool bMoving = R.Debug().AssistanceMovementTime > 0.f;
			Starts += (bMoving && !bWasMoving) ? 1 : 0;
			bWasMoving = bMoving;
		}
		return Starts;
	};
	const int32 WithHysteresis = CountStarts(75.f);
	const int32 Without = CountStarts(150.f);
	TestEqual(TEXT("Hysteresis: moving starts once around the threshold"), WithHysteresis, 1);
	TestTrue(FString::Printf(TEXT("Reference without hysteresis (Stop = Start) toggles (%d starts)"), Without), Without > 3);
	return true;
}

// -------------------------------------------------------------------------------------------------
// カメラへ向かう移動は除外（180° 回転しない）。通常の方向転換（90°、120°）は補助する。急な反転でも回らない。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistBackwardTest, "ActionCameraFramework.Assistance.BackwardExclusionAndTurns", ACFTests::Flags)
bool FACFAssistBackwardTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	for (const float H : { 180.f, -170.f, 150.f, -140.f })
	{
		FRig R;
		R.Velocity = Heading(H, 400.f);
		float MaxAbs = 0.f;
		R.Run(3.f, Dt60, [&](int32) { MaxAbs = FMath::Max(MaxAbs, FMath::Abs(R.Yaw())); });
		TestTrue(FString::Printf(TEXT("Heading %.0f (towards the camera): never turns (max |yaw| %.5f, %s)"), H, MaxAbs, *Describe(R)),
			MaxAbs < 1e-4f && R.Reason() == EACFCameraAssistanceReason::MovingTowardCamera);
	}
	for (const float H : { 90.f, 120.f, -130.f })
	{
		FRig R;
		R.Velocity = Heading(H, 400.f);
		bool bMonotonic = true;
		float Last = 0.f;
		R.Run(4.f, Dt60, [&](int32) { bMonotonic &= FMath::Sign(H) * Delta(R.Yaw(), Last) >= -1e-4f; Last = R.Yaw(); });
		TestTrue(FString::Printf(TEXT("Heading %.0f (a normal turn): turned to it (%s)"), H, *Describe(R)), FMath::Abs(Delta(R.Yaw(), H)) < 0.5f);
		TestTrue(FString::Printf(TEXT("Heading %.0f: one direction, no overshoot"), H), bMonotonic);
	}

	// Aligned with 90, then the player reverses (-90 = towards the camera): the camera holds.
	FRig R;
	R.Velocity = Heading(90.f, 400.f);
	R.Run(4.f, Dt60);
	const float Aligned = R.Yaw();
	R.Velocity = Heading(-90.f, 400.f);
	float MaxChange = 0.f;
	R.Run(3.f, Dt60, [&](int32) { MaxChange = FMath::Max(MaxChange, FMath::Abs(Delta(R.Yaw(), Aligned))); });
	TestTrue(FString::Printf(TEXT("Reversal: no 180 degree swing (max change %.4f, %s)"), MaxChange, *Describe(R)), MaxChange < 0.01f);
	// A 90 degree turn from there is a normal turn again.
	R.Velocity = Heading(0.f, 400.f);
	R.Run(4.f, Dt60);
	TestTrue(FString::Printf(TEXT("Then a 90 degree turn is followed (%s)"), *Describe(R)), FMath::Abs(Delta(R.Yaw(), 0.f)) < 0.5f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 359° / 1° の境界と ±180° の境界: 最短の向きに回り、大回りしない。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistWrapTest, "ActionCameraFramework.Assistance.ShortestAngleAcrossWrap", ACFTests::Flags)
bool FACFAssistWrapTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	struct FCase { float From; float To; };
	for (const FCase C : { FCase{ 359.f, 1.f }, FCase{ 1.f, 359.f }, FCase{ 170.f, -170.f }, FCase{ -170.f, 170.f } })
	{
		FRig R;
		R.Director->SetOrbitRotation(R.Handle, FRotator(-15.f, C.From, 0.f), true);
		R.Velocity = Heading(C.To, 400.f);
		const float Direction = FMath::Sign(Delta(C.To, C.From));
		float Last = FRotator::NormalizeAxis(C.From);
		bool bShortWay = true;
		float MaxDistance = 0.f;
		R.Step(Dt60);
		Last = R.Yaw();
		R.Run(3.f, Dt60, [&](int32)
		{
			bShortWay &= Direction * Delta(R.Yaw(), Last) >= -1e-4f;
			MaxDistance = FMath::Max(MaxDistance, FMath::Abs(Delta(R.Yaw(), C.From)));
			Last = R.Yaw();
		});
		TestTrue(FString::Printf(TEXT("%.0f -> %.0f: arrived (%s)"), C.From, C.To, *Describe(R)), FMath::Abs(Delta(R.Yaw(), C.To)) < 0.05f);
		TestTrue(FString::Printf(TEXT("%.0f -> %.0f: always the short way, never further than %.1f deg from the start (max %.3f)"), C.From, C.To,
			FMath::Abs(Delta(C.To, C.From)), MaxDistance), bShortWay && MaxDistance <= FMath::Abs(Delta(C.To, C.From)) + 1e-3f);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// 最大 Yaw 速度: 補助が加える Yaw は毎フレーム Max * dt 以下（30 / 60 / 120 fps、ヒッチ）。Rotation Damping の後も同じ。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistMaxSpeedTest, "ActionCameraFramework.Assistance.MaxYawSpeed", ACFTests::Flags)
bool FACFAssistMaxSpeedTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	for (const float Damping : { 0.f, 0.2f })
	{
		for (const float Dt : { 1.f / 30.f, 1.f / 60.f, 1.f / 120.f, 0.25f })
		{
			FRig R([Damping](UACFCameraNode_OrbitalFollow& O) { O.RecenterTime = 0.01f; O.RotationDamping = FVector2D(Damping, 0.f); });
			R.Velocity = Heading(90.f, 400.f);
			float MaxStep = 0.f;
			float Last = 0.f;
			int32 ArrivedFrame = INDEX_NONE;
			int32 StartFrame = INDEX_NONE;
			const int32 N = FMath::RoundToInt(6.f / Dt);
			for (int32 I = 0; I < N; ++I)
			{
				R.Step(Dt);
				MaxStep = FMath::Max(MaxStep, FMath::Abs(Delta(R.Yaw(), Last)));
				Last = R.Yaw();
				StartFrame = (StartFrame == INDEX_NONE && R.Assisting()) ? I : StartFrame;
				ArrivedFrame = (ArrivedFrame == INDEX_NONE && FMath::Abs(Delta(R.Yaw(), 90.f)) < 0.5f) ? I : ArrivedFrame;
			}
			TestTrue(FString::Printf(TEXT("Damping %.1f, dt %.4f: per-frame yaw %.4f <= %.4f (90 deg/s)"), Damping, Dt, MaxStep, 90.f * Dt), MaxStep <= 90.f * Dt + 1e-3f);
			const float Seconds = (ArrivedFrame - StartFrame + 1) * Dt; // the first assisting frame already turns
			TestTrue(FString::Printf(TEXT("Damping %.1f, dt %.4f: arrives (%.3f s; without damping about 1 s at the limit)"), Damping, Dt, Seconds),
				ArrivedFrame != INDEX_NONE && Seconds >= 0.95f && (Damping > 0.f || Seconds <= 1.0f + Dt * 2.f));
		}
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// フレームレート: 30 / 60 / 120 fps で同じ時刻の Yaw がほぼ同じ。ヒッチ（0.5 s の 1 フレーム）でも反転・行き過ぎなし。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistFrameRateTest, "ActionCameraFramework.Assistance.FrameRateAndHitches", ACFTests::Flags)
bool FACFAssistFrameRateTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	const float Times[] = { 0.8f, 1.0f, 1.5f, 2.0f, 3.0f }; // multiples of 1/30 s
	constexpr int32 NumTimes = UE_ARRAY_COUNT(Times);
	TArray<TArray<float>> Yaws;
	for (const float Dt : { 1.f / 30.f, 1.f / 60.f, 1.f / 120.f })
	{
		FRig R;
		R.Velocity = Heading(100.f, 400.f);
		TArray<float>& Samples = Yaws.AddDefaulted_GetRef();
		double Time = 0.0;
		int32 Next = 0;
		while (Next < NumTimes)
		{
			R.Step(Dt);
			Time += Dt;
			if (Time >= Times[Next] - 1e-4)
			{
				Samples.Add(R.Yaw());
				++Next;
			}
		}
	}
	for (int32 I = 0; I < NumTimes; ++I)
	{
		const float Spread = FMath::Max3(FMath::Abs(Delta(Yaws[0][I], Yaws[1][I])), FMath::Abs(Delta(Yaws[2][I], Yaws[1][I])), FMath::Abs(Delta(Yaws[0][I], Yaws[2][I])));
		TestTrue(FString::Printf(TEXT("t = %.2f s: yaw 30 / 60 / 120 fps = %.2f / %.2f / %.2f (spread %.2f <= 4)"), Times[I], Yaws[0][I], Yaws[1][I], Yaws[2][I], Spread), Spread <= 4.f);
	}

	// Hitch in the middle of a turn: bounded by Max * dt, same direction, never past the heading.
	FRig R;
	R.Velocity = Heading(100.f, 400.f);
	R.StepUntil(Dt60, 600, [&R] { return R.Assisting(); });
	R.Run(0.1f, Dt60);
	const float Before = R.Yaw();
	R.Step(0.5f);
	const float Step = Delta(R.Yaw(), Before);
	TestTrue(FString::Printf(TEXT("Hitch 0.5 s: step %.3f in the turn direction, at most 45 deg"), Step), Step > 0.f && Step <= 45.f + 1e-3f);
	float MaxYaw = R.Yaw();
	R.Run(3.f, Dt60, [&](int32) { MaxYaw = FMath::Max(MaxYaw, R.Yaw()); });
	TestTrue(FString::Printf(TEXT("After the hitch: never past the heading (max %.3f)"), MaxYaw), MaxYaw <= 100.f + 1e-3f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Pause（DeltaTime 0 の評価）ではタイマーも Yaw も進まない。Time Dilation はゲーム時間で進む（実時間あたりは遅くなる）。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistPauseTest, "ActionCameraFramework.Assistance.PauseAndTimeDilation", ACFTests::Flags)
bool FACFAssistPauseTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	FRig R;
	R.Velocity = Heading(90.f, 400.f);
	R.Run(0.2f, Dt60);
	const float Wait = R.Debug().AssistanceWaitTime;
	const float Moving = R.Debug().AssistanceMovementTime;
	for (int32 I = 0; I < 300; ++I)
	{
		R.Step(0.f); // paused: new frames, zero delta
	}
	TestTrue(FString::Printf(TEXT("Paused 300 frames: waits unchanged (%.4f / %.4f -> %.4f / %.4f)"), Wait, Moving, R.Debug().AssistanceWaitTime, R.Debug().AssistanceMovementTime),
		R.Debug().AssistanceWaitTime == Wait && R.Debug().AssistanceMovementTime == Moving);
	const int32 Remaining = R.StepUntil(Dt60, 120, [&R] { return R.Assisting(); });
	TestTrue(FString::Printf(TEXT("After the pause: the remaining 0.3 s of Recenter Delay (%d frames, expected 18)"), Remaining), Remaining >= 17 && Remaining <= 19);
	R.Run(0.2f, Dt60);
	const float Turning = R.Yaw();
	for (int32 I = 0; I < 120; ++I)
	{
		R.Step(0.f);
	}
	TestEqual(TEXT("Paused while turning: yaw frozen"), R.Yaw(), Turning);
	TestTrue(TEXT("Paused while turning: still Assisting (state kept)"), R.Assisting());

	// Time dilation 0.5 at 60 fps = 1/120 s of game time per frame: the assistance follows game time.
	FRig Normal;
	FRig Slow;
	Normal.Velocity = Heading(90.f, 400.f);
	Slow.Velocity = Heading(90.f, 400.f); // cm per game second: the dilated delta moves it half as far per frame
	Normal.Run(1.5f, Dt60);
	Slow.Run(0.75f, Dt60 * 0.5f); // 90 frames = 1.5 s of real time at 60 fps, 0.75 s of game time
	TestTrue(FString::Printf(TEXT("Dilated: after 1.5 s of real time the turn is behind (%.2f < %.2f)"), Slow.Yaw(), Normal.Yaw()), Slow.Yaw() < Normal.Yaw() - 5.f);
	Slow.Run(0.75f, Dt60 * 0.5f); // 180 frames = 3 s real = 1.5 s game
	FRig Normal120;
	Normal120.Velocity = Heading(90.f, 400.f);
	Normal120.Run(1.5f, 1.f / 120.f);
	TestTrue(FString::Printf(TEXT("Dilated: 1.5 s of game time = undilated 1.5 s at the same step (%.4f vs %.4f)"), Slow.Yaw(), Normal120.Yaw()), FMath::IsNearlyEqual(Slow.Yaw(), Normal120.Yaw(), 1e-3f));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Target Facing（補助なし）は従来と同じ: 補助の設定値を変えても姿勢はビット単位で同じ。Phase 2 の式とも一致する。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistTargetFacingCompatTest, "ActionCameraFramework.Assistance.TargetFacingBitwiseCompatible", ACFTests::Flags)
bool FACFAssistTargetFacingCompatTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	auto Phase2 = [](UACFCameraNode_OrbitalFollow& O)
	{
		O.bEnableMovementAssistance = false;
		O.RecenterHeading = EACFCameraRecenterHeading::TargetFacing;
		O.bRecenterWhenIdle = true;
		O.bRecenterPitch = true;
		O.InitialRotation = EACFCameraOrbitInitialRotation::BehindTarget;
		O.InitialYaw = 10.f;
		O.RotationDamping = FVector2D(0.1f, 0.05f);
	};
	FRig A(Phase2);
	FRig B([&Phase2](UACFCameraNode_OrbitalFollow& O)
	{
		Phase2(O);
		O.StartSpeedThreshold = 999.f; O.StopSpeedThreshold = 1.f; O.RequiredMovementDuration = 5.f;
		O.BackwardExclusionAngle = 170.f; O.AngularDeadZone = 30.f; O.MaxAssistanceYawSpeed = 1.f;
	});
	FRig C([&Phase2](UACFCameraNode_OrbitalFollow& O) { Phase2(O); O.RecenterHeading = EACFCameraRecenterHeading::MovementDirection; });
	int32 Mismatches = 0;
	for (int32 I = 0; I < 600; ++I)
	{
		const float T = I * Dt60;
		for (FRig* R : { &A, &B, &C })
		{
			R->Velocity = Heading(T * 40.f, 300.f);
			R->Target->SetWorldRotation(FRotator(0.f, FMath::Min(T, 2.f) * 60.f, 0.f));
			if (I >= 30 && I < 40)
			{
				R->Director->AddCameraInput(FVector2D(3.0, -1.0));
			}
			if (I == 300)
			{
				R->Director->SetOrbitRotation(R->Handle, FRotator(-30.f, -50.f, 0.f), false);
			}
			R->Step(I == 200 ? 0.2f : Dt60);
		}
		const FACFCameraPose& PA = A.Director->GetLastResult().Pose;
		for (const FRig* R : { &B, &C })
		{
			const FACFCameraPose& P = R->Director->GetLastResult().Pose;
			Mismatches += (P.Location == PA.Location && P.Rotation == PA.Rotation && P.FieldOfView == PA.FieldOfView) ? 0 : 1;
		}
	}
	TestEqual(TEXT("Assistance settings without Enable Movement Assistance change nothing (bitwise, 600 frames x 2 presets)"), Mismatches, 0);
	TestTrue(TEXT("The recenter did turn the camera (the comparison is not trivial)"), FMath::Abs(Delta(A.Yaw(), 120.f + 10.f)) < 2.f);

	// Phase 2 reference: the formula of the recenter before 7B-1 (goal += shortest(home - goal) * (1 - exp(-dt / T))).
	FRig D([](UACFCameraNode_OrbitalFollow& O)
	{
		O.bEnableMovementAssistance = false;
		O.bRecenterWhenIdle = true;
		O.bRecenterPitch = false;
		O.InitialRotation = EACFCameraOrbitInitialRotation::BehindTarget;
		O.InitialYaw = 0.f;
	});
	D.Step(Dt60); // initialized: goal yaw 0, TimeSinceInput 0
	D.Target->SetWorldRotation(FRotator(0.f, 90.f, 0.f));
	float Goal = 0.f;
	float TimeSinceInput = Dt60; // the initializing evaluation already counted one frame without input
	float MaxError = 0.f;
	for (int32 I = 0; I < 180; ++I)
	{
		D.Step(Dt60);
		TimeSinceInput += Dt60;
		if (TimeSinceInput >= 0.5f)
		{
			Goal += FRotator::NormalizeAxis(90.f - Goal) * ACFCameraMath::ExpAlpha(0.3f, Dt60);
		}
		Goal = FRotator::NormalizeAxis(Goal);
		MaxError = FMath::Max(MaxError, FMath::Abs(Delta(D.Yaw(), Goal)));
	}
	TestTrue(FString::Printf(TEXT("Target Facing matches the Phase 2 formula (max error %.6f deg)"), MaxError), MaxError < 1e-3f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// bFollowTargetYaw との関係: Movement Direction とは併用しない（Follow Target Yaw が優先、補助は Disabled）。
// Target Facing + 補助は「ターゲットの向きに対する相対角」を Initial Yaw へ戻す。明示的な回転は最優先。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistFollowTargetYawTest, "ActionCameraFramework.Assistance.FollowTargetYawPriority", ACFTests::Flags)
bool FACFAssistFollowTargetYawTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	{
		FRig R([](UACFCameraNode_OrbitalFollow& O) { O.bFollowTargetYaw = true; O.InitialRotation = EACFCameraOrbitInitialRotation::BehindTarget; });
		R.Velocity = Heading(90.f, 400.f);
		bool bFollows = true;
		R.Run(3.f, Dt60, [&](int32 I)
		{
			const float TargetYaw = I * 0.5f;
			bFollows &= FMath::Abs(Delta(R.Yaw(), R.Target->GetComponentRotation().Yaw)) < 1e-3f;
			R.Target->SetWorldRotation(FRotator(0.f, TargetYaw, 0.f));
		});
		TestTrue(FString::Printf(TEXT("Movement Direction + Follow Target Yaw: disabled (%s)"), *Describe(R)),
			R.State() == EACFCameraAssistanceState::Disabled && R.Reason() == EACFCameraAssistanceReason::FollowTargetYaw);
		TestTrue(TEXT("The camera follows the target yaw only"), bFollows);
	}
	{
		FRig R([](UACFCameraNode_OrbitalFollow& O)
		{
			O.bFollowTargetYaw = true;
			O.RecenterHeading = EACFCameraRecenterHeading::TargetFacing;
			O.InitialRotation = EACFCameraOrbitInitialRotation::BehindTarget;
		});
		R.Target->SetWorldRotation(FRotator(0.f, 30.f, 0.f));
		R.Step(Dt60);
		R.Director->SetOrbitRotation(R.Handle, FRotator(-15.f, 90.f, 0.f), true); // 60 deg off the target's back
		R.Step(Dt60);
		TestTrue(FString::Printf(TEXT("Explicit rotation frame: Suppressed / Explicit Rotation (%s)"), *Describe(R)), R.Reason() == EACFCameraAssistanceReason::ExplicitRotation);
		TestTrue(TEXT("Explicit rotation applied"), FMath::IsNearlyEqual(R.Yaw(), 90.f, 1e-3f));
		R.Velocity = Heading(0.f, 400.f);
		float MaxStep = 0.f;
		float Last = R.Yaw();
		R.Run(3.f, Dt60, [&](int32) { MaxStep = FMath::Max(MaxStep, FMath::Abs(Delta(R.Yaw(), Last))); Last = R.Yaw(); });
		TestTrue(FString::Printf(TEXT("Target Facing + Follow Target Yaw: back behind the target (yaw %.2f, target 30)"), R.Yaw()), FMath::Abs(Delta(R.Yaw(), 30.f)) < 0.5f);
		TestTrue(FString::Printf(TEXT("Relative turn limited to 90 deg/s (max step %.4f)"), MaxStep), MaxStep <= 90.f * Dt60 + 1e-3f);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// テレポート（検出・NotifyTeleport）と不正な値: 速度の履歴を捨て、待ちからやり直す。NaN / 負の DeltaTime、極小の
// DeltaTime の巨大な速度、非有限の設定でも Yaw は飛ばない。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistTeleportTest, "ActionCameraFramework.Assistance.TeleportAndInvalidValues", ACFTests::Flags)
bool FACFAssistTeleportTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	FRig R;
	R.Velocity = Heading(90.f, 400.f);
	R.StepUntil(Dt60, 600, [&R] { return R.Assisting(); });
	R.Run(0.1f, Dt60);
	float Before = R.Yaw();
	R.Target->SetWorldLocation(R.Target->GetComponentLocation() + FVector(-5000.0, 0.0, 0.0)); // detected (> 1000 cm)
	R.Step(Dt60);
	TestTrue(FString::Printf(TEXT("Teleport frame: no step towards the jump (%.4f -> %.4f), waits restarted (%s)"), Before, R.Yaw(), *Describe(R)),
		FMath::IsNearlyEqual(R.Yaw(), Before, 1e-4f) && R.Reason() == EACFCameraAssistanceReason::RecenterDelay && R.Debug().AssistanceMovementTime == 0.f);
	const int32 Resume = R.StepUntil(Dt60, 120, [&R] { return R.Assisting(); });
	TestTrue(FString::Printf(TEXT("After the teleport: waits again (%d frames)"), Resume), Resume >= 29);

	Before = R.Yaw();
	R.Director->NotifyTeleport(false);
	R.Step(Dt60);
	TestTrue(FString::Printf(TEXT("NotifyTeleport: waits restarted (%s)"), *Describe(R)), R.Reason() == EACFCameraAssistanceReason::RecenterDelay && FMath::IsNearlyEqual(R.Yaw(), Before, 1e-4f));

	// Invalid delta times are evaluated as zero time by the director: nothing advances.
	R.Run(1.f, Dt60);
	const float Wait = R.Debug().AssistanceWaitTime;
	Before = R.Yaw();
	for (const float Bad : { NAN, -0.5f, INFINITY })
	{
		R.Step(Bad);
	}
	TestTrue(TEXT("NaN / negative / infinite delta time: no advance, yaw finite and unchanged"),
		R.Debug().AssistanceWaitTime == Wait && R.Yaw() == Before && FMath::IsFinite(R.Yaw()));

	// A tiny delta time with a big (non-teleport) move: a huge but finite speed. The step stays below Max * dt.
	Before = R.Yaw();
	R.Target->SetWorldLocation(R.Target->GetComponentLocation() + FVector(-900.0, 0.0, 0.0));
	R.Step(1e-6f);
	TestTrue(FString::Printf(TEXT("Tiny dt with a 9 m move: speed %.3g finite, step %.6f <= %.6f"), R.Debug().AssistanceMovementSpeed, FMath::Abs(Delta(R.Yaw(), Before)), 90.f * 1e-6f),
		FMath::IsFinite(R.Debug().AssistanceMovementSpeed) && FMath::Abs(Delta(R.Yaw(), Before)) <= 90.f * 1e-6f + 1e-4f);

	// Non-finite / zero settings: disabled, nothing invalid in the pose.
	for (const float Bad : { NAN, 0.f, -10.f })
	{
		FRig S([Bad](UACFCameraNode_OrbitalFollow& O) { O.MaxAssistanceYawSpeed = Bad; });
		S.Velocity = Heading(90.f, 400.f);
		S.Run(3.f, Dt60);
		TestTrue(FString::Printf(TEXT("Max yaw speed %f: Disabled / Invalid Settings, yaw unchanged (%s)"), Bad, *Describe(S)),
			S.Reason() == EACFCameraAssistanceReason::InvalidSettings && S.Yaw() == 0.f && ACFCameraValidation::IsValidPose(S.Director->GetLastResult().Pose));
	}
	{
		FRig S([](UACFCameraNode_OrbitalFollow& O) { O.AngularDeadZone = NAN; });
		S.Velocity = Heading(90.f, 400.f);
		S.Run(3.f, Dt60);
		TestTrue(TEXT("NaN dead zone: Invalid Settings"), S.Reason() == EACFCameraAssistanceReason::InvalidSettings && S.Yaw() == 0.f);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Suppress Camera Assistance: Source ごと・重複なし・複数同時・破棄で解除・別 World の Source は外れる。手動入力は抑制しない。
// 解除後は待ちからやり直す。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistSuppressionTest, "ActionCameraFramework.Assistance.SuppressionSources", ACFTests::Flags)
bool FACFAssistSuppressionTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	FRig R;
	UACFCameraDirector* D = R.Director.Get();
	R.Velocity = Heading(90.f, 400.f);
	R.StepUntil(Dt60, 600, [&R] { return R.Assisting(); });
	TStrongObjectPtr<USceneComponent> A(NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient));
	TStrongObjectPtr<USceneComponent> B(NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient));

	D->SuppressAssistance(A.Get());
	D->SuppressAssistance(A.Get());
	TestEqual(TEXT("Same source twice counts once"), D->GetNumAssistanceSuppressions(), 1);
	D->SuppressAssistance(B.Get());
	TestEqual(TEXT("Two sources"), D->GetNumAssistanceSuppressions(), 2);
	AddExpectedMessagePlain(TEXT("SuppressCameraAssistance ignored: source is null"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	D->SuppressAssistance(nullptr);
	TestEqual(TEXT("Null source ignored"), D->GetNumAssistanceSuppressions(), 2);
	const float Held = R.Yaw();
	R.Run(1.f, Dt60);
	TestTrue(FString::Printf(TEXT("Suppressed: Suppressed by source, yaw held (%s)"), *Describe(R)),
		R.Reason() == EACFCameraAssistanceReason::SuppressedBySource && R.Yaw() == Held && R.Debug().bAssistanceSuppressedBySource);
	R.Director->AddCameraInput(FVector2D(4.0, 0.0));
	R.Step(Dt60);
	TestTrue(TEXT("Manual input still works while assistance is suppressed"), FMath::IsNearlyEqual(Delta(R.Yaw(), Held), 4.f, 1e-3f));

	TestTrue(TEXT("Release A"), D->ReleaseAssistanceSuppression(A.Get()));
	TestFalse(TEXT("Release A again: not held"), D->ReleaseAssistanceSuppression(A.Get()));
	TestFalse(TEXT("Release null: false"), D->ReleaseAssistanceSuppression(nullptr));
	R.Run(0.5f, Dt60);
	TestTrue(TEXT("B still suppresses"), D->IsAssistanceSuppressed() && R.Reason() == EACFCameraAssistanceReason::SuppressedBySource);
	TestTrue(TEXT("Release B"), D->ReleaseAssistanceSuppression(B.Get()));
	const int32 Resume = R.StepUntil(Dt60, 120, [&R] { return R.Assisting(); });
	TestTrue(FString::Printf(TEXT("Released: waits again (%d frames, expected 30)"), Resume), Resume >= 29 && Resume <= 31);

	// A destroyed source releases by itself (next evaluation); a new object is never mistaken for it.
	{
		USceneComponent* Temp = NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		D->SuppressAssistance(Temp);
		R.Step(Dt60);
		TestTrue(TEXT("Temporary source suppresses"), D->IsAssistanceSuppressed());
		Temp->MarkAsGarbage();
		R.Step(Dt60);
		TestEqual(TEXT("Destroyed source dropped at the next evaluation"), D->GetNumAssistanceSuppressions(), 0);
		USceneComponent* Fresh = NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		TestFalse(TEXT("A new object (possibly at the same address) is not the old source"), D->ReleaseAssistanceSuppression(Fresh));
	}

	// World: a source of another World is dropped when the camera evaluates in a new World; destroying the World releases.
	{
		FTestWorldWrapper WorldA;
		FTestWorldWrapper WorldB;
		if (!TestTrue(TEXT("Test worlds"), WorldA.CreateTestWorld(EWorldType::Game) && WorldB.CreateTestWorld(EWorldType::Game)))
		{
			return false;
		}
		FActorSpawnParameters SpawnParams;
		AActor* InA = WorldA.GetTestWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
		AActor* InA2 = WorldA.GetTestWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
		auto StepIn = [&R](UWorld* World)
		{
			FACFCameraEvaluationContext Context;
			Context.DeltaTime = Dt60;
			Context.FrameNumber = ++R.Driver.Frame;
			R.Driver.Fill(Context);
			Context.World = World;
			R.Director->Evaluate(Context);
		};
		D->SuppressAssistance(InA);
		StepIn(WorldA.GetTestWorld());
		TestEqual(TEXT("Source of the current World kept"), D->GetNumAssistanceSuppressions(), 1);
		StepIn(WorldB.GetTestWorld());
		TestEqual(TEXT("World changed: the old World's source dropped"), D->GetNumAssistanceSuppressions(), 0);
		D->SuppressAssistance(InA2);
		StepIn(nullptr);
		TestEqual(TEXT("No World in the context: kept"), D->GetNumAssistanceSuppressions(), 1);
		WorldA.DestroyTestWorld(/*bForceGarbageCollect*/ false);
		StepIn(nullptr);
		TestEqual(TEXT("World destroyed: its actor's suppression released"), D->GetNumAssistanceSuppressions(), 0);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// 要求の切り替え: 隠れている Rig は進まない（再表示の瞬間に回らず、待ちからやり直す）。戻りの Blend 中の Rig（制御していない）は
// 補助しない。外部 ViewTarget（出力バイパス）と出力抑制の間も止まり、戻ったら待ちから。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistRequestsTest, "ActionCameraFramework.Assistance.RequestsBlendAndExternalView", ACFTests::Flags)
bool FACFAssistRequestsTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	FRig R;
	R.Velocity = Heading(90.f, 400.f);
	R.StepUntil(Dt60, 600, [&R] { return R.Assisting(); });
	R.Run(0.1f, Dt60);
	const float Hidden = R.Yaw();

	// A higher request (Lock-On / Finisher like) hides the orbit for 2 s while the player keeps moving.
	TStrongObjectPtr<UACFCameraPreset> Fixed(MakeFixedPreset(FVector(0.0, 0.0, 500.0), 70.f));
	FACFCameraRequestParams High;
	High.Priority = 10;
	const FACFCameraRequestHandle Other = R.Director->AddRequest(Fixed.Get(), High);
	R.Run(2.f, Dt60);
	TestTrue(TEXT("The other request is in control"), R.Director->GetActiveRequest() == Other);
	R.Director->RemoveRequest(Other);
	R.Step(Dt60);
	TestTrue(FString::Printf(TEXT("Shown again: no rotation jump (%.4f -> %.4f)"), Hidden, R.Yaw()), FMath::IsNearlyEqual(R.Yaw(), Hidden, 1e-4f));
	TestTrue(FString::Printf(TEXT("Shown again: waiting from the start (%s)"), *Describe(R)), R.Reason() == EACFCameraAssistanceReason::RecenterDelay && R.Debug().AssistanceWaitTime <= Dt60 + 1e-5f);
	const int32 Resume = R.StepUntil(Dt60, 120, [&R] { return R.Assisting(); });
	TestTrue(FString::Printf(TEXT("Resumes after the waits (%d frames)"), Resume), Resume >= 29 && Resume <= 31);

	// External view target (Sequencer camera cut): evaluated in the background, not assisted; the waits restart afterwards.
	R.Run(0.1f, Dt60);
	float Held = R.Yaw();
	R.Driver.bOutputBypassed = true;
	R.Run(1.f, Dt60);
	TestTrue(FString::Printf(TEXT("Output bypassed: View Not Shown, yaw held (%s)"), *Describe(R)), R.Reason() == EACFCameraAssistanceReason::ViewNotShown && R.Yaw() == Held);
	R.Driver.bOutputBypassed = false;
	R.Step(Dt60);
	TestTrue(TEXT("Back on screen: waiting from the start"), R.Reason() == EACFCameraAssistanceReason::RecenterDelay);
	R.Run(1.f, Dt60);

	// Suppress Output (another system owns the view): the same.
	TStrongObjectPtr<USceneComponent> Owner(NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient));
	Held = R.Yaw();
	R.Director->SuppressOutput(Owner.Get(), 0.f);
	R.Run(1.f, Dt60);
	TestTrue(FString::Printf(TEXT("Output suppressed: View Not Shown (%s)"), *Describe(R)), R.Reason() == EACFCameraAssistanceReason::ViewNotShown && R.Yaw() == Held);
	R.Director->ReleaseOutputSuppression(Owner.Get(), 0.f);
	R.Step(Dt60);
	TestTrue(TEXT("Output back: waiting from the start"), R.Reason() == EACFCameraAssistanceReason::RecenterDelay);

	// Released with a live blend out: the outgoing rig keeps evaluating but is not in control (no assistance).
	R.Run(1.f, Dt60);
	R.Preset->DefaultBlendOut = Linear(1.f);
	R.Preset->DefaultBlendOut.Source = EACFCameraBlendSource::Live;
	Held = R.Yaw();
	R.Director->RemoveRequest(R.Handle);
	bool bHeld = true;
	R.Run(0.8f, Dt60, [&](int32) { bHeld &= R.Yaw() == Held && R.Reason() == EACFCameraAssistanceReason::NotInControl; });
	TestTrue(FString::Printf(TEXT("Blending out to the UE camera: Not In Control, yaw held (%s)"), *Describe(R)), bHeld && R.Director->GetLastResult().bIsBlending);
	return true;
}

#if WITH_EDITOR
// -------------------------------------------------------------------------------------------------
// Preset Validation（B-8）: 補助の不正値と組み合わせ。既定値では何も出さない。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistValidationTest, "ActionCameraFramework.PresetValidation.MovementAssistance", ACFTests::Flags)
bool FACFAssistValidationTest::RunTest(const FString& Parameters)
{
	auto Issues = [](TFunctionRef<void(UACFCameraNode_OrbitalFollow&)> Configure)
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_OrbitalFollow* Orbit = NewObject<UACFCameraNode_OrbitalFollow>(Preset);
		Configure(*Orbit);
		Preset->Nodes.Add(Orbit);
		TArray<FACFCameraPresetIssue> Out;
		ACFCameraPresetValidation::ValidatePreset(*Preset, Out);
		return Out;
	};
	auto Has = [](const TArray<FACFCameraPresetIssue>& List, const TCHAR* Id, const TCHAR* Property = nullptr, EACFCameraPresetIssueSeverity Severity = EACFCameraPresetIssueSeverity::Warning)
	{
		return List.ContainsByPredicate([&](const FACFCameraPresetIssue& I) { return I.Id == FName(Id) && I.Severity == Severity && (!Property || I.Property.Contains(Property)); });
	};
	auto NumAssist = [](const TArray<FACFCameraPresetIssue>& List)
	{
		return static_cast<int32>(Algo::CountIf(List, [](const FACFCameraPresetIssue& I)
		{
			const FString Id = I.Id.ToString();
			return Id.StartsWith(TEXT("ACF.Assist.")) || I.Property.Contains(TEXT("Speed")) || I.Property.Contains(TEXT("Angle")) || I.Property.Contains(TEXT("DeadZone"))
				|| I.Property.Contains(TEXT("MovementDuration")) || I.Property.Contains(TEXT("RecenterHeading"));
		}));
	};
	using ESeverity = EACFCameraPresetIssueSeverity;

	TestEqual(TEXT("Defaults (assistance off): no assistance finding"), NumAssist(Issues([](UACFCameraNode_OrbitalFollow&) {})), 0);
	TestEqual(TEXT("Assistance on with its defaults: no finding"), NumAssist(Issues([](UACFCameraNode_OrbitalFollow& O) { O.bEnableMovementAssistance = true; O.RecenterHeading = EACFCameraRecenterHeading::MovementDirection; })), 0);
	TestEqual(TEXT("Assistance off: out-of-range assistance values are not reported (unused)"),
		NumAssist(Issues([](UACFCameraNode_OrbitalFollow& O) { O.StartSpeedThreshold = -5.f; O.MaxAssistanceYawSpeed = 0.f; O.BackwardExclusionAngle = 300.f; })), 0);
	TestTrue(TEXT("Assistance off: NaN still reported (warning)"), Has(Issues([](UACFCameraNode_OrbitalFollow& O) { O.AngularDeadZone = NAN; }), TEXT("ACF.Param.NonFinite"), TEXT("AngularDeadZone")));

	auto On = [](TFunctionRef<void(UACFCameraNode_OrbitalFollow&)> Extra)
	{
		return [Extra](UACFCameraNode_OrbitalFollow& O) { O.bEnableMovementAssistance = true; Extra(O); };
	};
	TestTrue(TEXT("Negative start speed"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.StartSpeedThreshold = -10.f; O.StopSpeedThreshold = -20.f; })), TEXT("ACF.Param.OutOfRange"), TEXT("StartSpeedThreshold")));
	TestTrue(TEXT("Negative stop speed"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.StopSpeedThreshold = -1.f; })), TEXT("ACF.Param.OutOfRange"), TEXT("StopSpeedThreshold")));
	TestTrue(TEXT("Start / Stop inverted"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.StartSpeedThreshold = 100.f; O.StopSpeedThreshold = 200.f; })), TEXT("ACF.Assist.ThresholdsInverted")));
	TestTrue(TEXT("Negative required duration"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.RequiredMovementDuration = -0.1f; })), TEXT("ACF.Param.OutOfRange"), TEXT("RequiredMovementDuration")));
	TestTrue(TEXT("Negative recenter delay"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.RecenterDelay = -1.f; })), TEXT("ACF.Param.OutOfRange"), TEXT("RecenterDelay")));
	TestTrue(TEXT("Angle above 180"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.BackwardExclusionAngle = 200.f; })), TEXT("ACF.Param.OutOfRange"), TEXT("BackwardExclusionAngle")));
	TestTrue(TEXT("Negative dead zone"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.AngularDeadZone = -1.f; })), TEXT("ACF.Param.OutOfRange"), TEXT("AngularDeadZone")));
	TestTrue(TEXT("Exclusion of 180: never turns"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.BackwardExclusionAngle = 180.f; })), TEXT("ACF.Assist.AlwaysExcluded")));
	TestTrue(TEXT("Dead zone covering every heading"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.BackwardExclusionAngle = 90.f; O.AngularDeadZone = 90.f; })), TEXT("ACF.Assist.DeadZoneCoversAll")));
	TestTrue(TEXT("NaN parameter: error"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.StartSpeedThreshold = NAN; })), TEXT("ACF.Param.NonFinite"), TEXT("StartSpeedThreshold"), ESeverity::Error));
	TestTrue(TEXT("Infinite parameter: error"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.MaxAssistanceYawSpeed = INFINITY; })), TEXT("ACF.Param.NonFinite"), TEXT("MaxAssistanceYawSpeed"), ESeverity::Error));
	TestTrue(TEXT("Max yaw speed 0: error"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.MaxAssistanceYawSpeed = 0.f; })), TEXT("ACF.Assist.NoYawSpeed"), nullptr, ESeverity::Error));
	TestTrue(TEXT("Max yaw speed negative: error"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.MaxAssistanceYawSpeed = -30.f; })), TEXT("ACF.Assist.NoYawSpeed"), nullptr, ESeverity::Error));
	TestTrue(TEXT("Control Rotation input"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.InputSource = EACFCameraOrbitInputSource::ControlRotation; })), TEXT("ACF.Assist.ControlRotationInput")));
	TestTrue(TEXT("Movement Direction + Follow Target Yaw"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.RecenterHeading = EACFCameraRecenterHeading::MovementDirection; O.bFollowTargetYaw = true; })), TEXT("ACF.Assist.FollowTargetYaw")));
	TestTrue(TEXT("Movement Direction without assistance"), Has(Issues([](UACFCameraNode_OrbitalFollow& O) { O.bRecenterWhenIdle = true; O.RecenterHeading = EACFCameraRecenterHeading::MovementDirection; }), TEXT("ACF.Assist.HeadingNeedsAssistance")));
	// Phase 7B-2: Input Activity Threshold (deg/s, noise level).
	TestTrue(TEXT("7B-2: negative input threshold"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.InputActivityThreshold = -1.f; })), TEXT("ACF.Param.OutOfRange"), TEXT("InputActivityThreshold")));
	TestTrue(TEXT("7B-2: NaN input threshold: error"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.InputActivityThreshold = NAN; })), TEXT("ACF.Param.NonFinite"), TEXT("InputActivityThreshold"), ESeverity::Error));
	TestTrue(TEXT("7B-2: threshold above noise level"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.InputActivityThreshold = 45.f; })), TEXT("ACF.Assist.InputThresholdHigh"), TEXT("InputActivityThreshold")));
	TestFalse(TEXT("7B-2: threshold 30 is still accepted"), Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.InputActivityThreshold = 30.f; })), TEXT("ACF.Assist.InputThresholdHigh")));
	TestFalse(TEXT("7B-2: high threshold without assistance: unused, not reported"), Has(Issues([](UACFCameraNode_OrbitalFollow& O) { O.InputActivityThreshold = 45.f; }), TEXT("ACF.Assist.InputThresholdHigh")));
	TestFalse(TEXT("7B-2: negative threshold with Control Rotation input: unused, not reported"),
		Has(Issues(On([](UACFCameraNode_OrbitalFollow& O) { O.InputSource = EACFCameraOrbitInputSource::ControlRotation; O.InputActivityThreshold = -1.f; })), TEXT("ACF.Param.OutOfRange"), TEXT("InputActivityThreshold")));
	return true;
}
#endif

// =================================================================================================
// Phase 7B-2: camera input and the assistance (Input Activity Threshold, README 29 章). Noise never blocks the assistance,
// clear input stops it in the same frame, the wait restarts at the last input frame. Mouse deltas (displacements per
// frame) and stick rates (per second) are measured in one unit (deg/s after Sensitivity) at every frame rate.

namespace ACFAssistTests
{
	/** Moves sideways (heading 90) until the assistance turns the camera. */
	inline bool StartAssisting(FRig& R, float Dt)
	{
		R.Velocity = Heading(90.f, 400.f);
		return R.StepUntil(Dt, FMath::CeilToInt(5.f / Dt), [&R] { return R.Assisting(); }) != INDEX_NONE;
	}

	inline bool IsManual(const FRig& R) { return R.Reason() == EACFCameraAssistanceReason::ManualInput; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistInputNoiseTest, "ActionCameraFramework.Assistance.Input.NoiseDoesNotBlock", ACFTests::Flags)
bool FACFAssistInputNoiseTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	// Stick drift (2 deg/s) every frame and mouse jitter (+-0.1 deg every third frame: at most 2.5 deg/s over the window)
	// from the start. Threshold 5 (default): the assistance starts and turns anyway. Threshold 0 (7B-1): it never does.
	for (const float Threshold : { 5.f, 0.f })
	{
		FRig R([Threshold](UACFCameraNode_OrbitalFollow& O) { O.InputActivityThreshold = Threshold; });
		R.Velocity = Heading(90.f, 400.f);
		int32 AssistFrames = 0;
		int32 ManualFrames = 0;
		float MaxActivity = 0.f;
		for (int32 I = 0; I < 180; ++I)
		{
			R.Director->SetCameraInputRate(FVector2D(2.0, 0.0));
			if (I % 3 == 0)
			{
				R.Director->AddCameraInput(FVector2D(I % 6 == 0 ? 0.1 : -0.1, 0.0));
			}
			R.Step(Dt60);
			AssistFrames += R.Assisting() ? 1 : 0;
			ManualFrames += IsManual(R) ? 1 : 0;
			MaxActivity = FMath::Max(MaxActivity, R.Debug().AssistanceInputActivity);
		}
		if (Threshold > 0.f)
		{
			TestEqual(TEXT("Threshold 5: the noise is never manual input"), ManualFrames, 0);
			TestTrue(FString::Printf(TEXT("Threshold 5: assisting despite the noise (%d frames, %s)"), AssistFrames, *Describe(R)), AssistFrames > 60 && R.Yaw() > 60.f);
			TestTrue(FString::Printf(TEXT("Activity is measured and stays below the threshold (max %.3f deg/s)"), MaxActivity), MaxActivity >= 2.f && MaxActivity < 5.f);
			TestTrue(TEXT("Debugger: input published with its threshold"), R.Debug().bHasAssistanceInput && R.Debug().AssistanceInputThreshold == 5.f);
		}
		else
		{
			TestEqual(TEXT("Threshold 0 (Phase 7B-1): every frame with input is manual input"), ManualFrames, 180);
			TestEqual(TEXT("Threshold 0: never assists"), AssistFrames, 0);
		}
	}

	// The noise still turns the camera (ACF has no dead zone: that is the game's input): standing, 1 s of 2 deg/s drift.
	{
		FRig R;
		for (int32 I = 0; I < 60; ++I)
		{
			R.Director->SetCameraInputRate(FVector2D(2.0, 0.0));
			R.Step(Dt60);
		}
		TestTrue(FString::Printf(TEXT("Drift below the threshold is still applied (yaw %.4f, expected 2)"), R.Yaw()), FMath::IsNearlyEqual(R.Yaw(), 2.f, 1e-3f));
		TestTrue(TEXT("Standing: waiting for movement, not manual input"), R.Reason() == EACFCameraAssistanceReason::BelowSpeed);
	}

	// Priority: while ACF is not on screen (output bypassed) noise reports View Not Shown, clear input Manual Input (above it).
	{
		FRig R;
		R.Driver.bOutputBypassed = true;
		R.Velocity = Heading(90.f, 400.f);
		R.Run(1.f, Dt60);
		R.Director->SetCameraInputRate(FVector2D(2.0, 0.0));
		R.Step(Dt60);
		TestTrue(FString::Printf(TEXT("Bypassed + noise: View Not Shown (%s)"), *Describe(R)), R.Reason() == EACFCameraAssistanceReason::ViewNotShown);
		R.Director->SetCameraInputRate(FVector2D(30.0, 0.0));
		R.Step(Dt60);
		TestTrue(FString::Printf(TEXT("Bypassed + clear input: Manual Input (%s)"), *Describe(R)), IsManual(R));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistInputStopResumeTest, "ActionCameraFramework.Assistance.Input.ClearInputStopsAndResumes", ACFTests::Flags)
bool FACFAssistInputStopResumeTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	for (const float Fps : { 30.f, 60.f, 120.f })
	{
		const float Dt = 1.f / Fps;
		FRig R;
		if (!TestTrue(FString::Printf(TEXT("%.0f fps: assisting"), Fps), StartAssisting(R, Dt)))
		{
			continue;
		}
		const int32 Expected = FMath::RoundToInt(0.5f / Dt); // Recenter Delay 0.5 s
		auto CheckResume = [&](const TCHAR* What)
		{
			const float Held = R.Yaw();
			bool bHeld = true;
			const int32 Resume = R.StepUntil(Dt, 4 * FMath::RoundToInt(Fps), [&]
			{
				bHeld &= R.Assisting() || R.Yaw() == Held;
				return R.Assisting();
			});
			TestTrue(FString::Printf(TEXT("%.0f fps, %s: yaw held while waiting"), Fps, What), bHeld);
			TestTrue(FString::Printf(TEXT("%.0f fps, %s: resumes after Recenter Delay from the last input frame (%d frames, expected %d)"), Fps, What, Resume, Expected),
				Resume != INDEX_NONE && FMath::Abs(Resume - Expected) <= 1);
		};

		// Stick at 10 deg/s (twice the threshold): manual input in the same frame, the camera moves by the input only.
		float Before = R.Yaw();
		R.Director->SetCameraInputRate(FVector2D(10.0, 0.0));
		R.Step(Dt);
		TestTrue(FString::Printf(TEXT("%.0f fps: stick stops the assistance at once (%s)"), Fps, *Describe(R)),
			IsManual(R) && FMath::IsNearlyEqual(Delta(R.Yaw(), Before), 10.f * Dt, 1e-3f));
		CheckResume(TEXT("stick"));

		// One mouse delta of 1 deg (10 deg/s over the window at every frame rate): the same.
		Before = R.Yaw();
		R.Director->AddCameraInput(FVector2D(1.0, 0.0));
		R.Step(Dt);
		TestTrue(FString::Printf(TEXT("%.0f fps: mouse flick stops the assistance at once (%s)"), Fps, *Describe(R)),
			IsManual(R) && FMath::IsNearlyEqual(Delta(R.Yaw(), Before), 1.f, 1e-3f));
		CheckResume(TEXT("mouse"));

		// Held stick for 1 s: never assists while it lasts, then the same wait.
		bool bNeverAssisted = true;
		for (int32 I = 0; I < FMath::RoundToInt(Fps); ++I)
		{
			R.Director->SetCameraInputRate(FVector2D(-20.0, 0.0));
			R.Step(Dt);
			bNeverAssisted &= IsManual(R);
		}
		TestTrue(FString::Printf(TEXT("%.0f fps: held stick blocks every frame"), Fps), bNeverAssisted);
		CheckResume(TEXT("held stick"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistInputFrameRateTest, "ActionCameraFramework.Assistance.Input.FrameRateIndependentUnits", ACFTests::Flags)
bool FACFAssistInputFrameRateTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	for (const float Fps : { 30.f, 60.f, 120.f, 240.f })
	{
		const float Dt = 1.f / Fps;
		// One jitter count of 0.3 deg weighs 3 deg/s at every frame rate (not 0.3 / Dt): never manual input.
		{
			FRig R;
			StartAssisting(R, Dt);
			R.Director->AddCameraInput(FVector2D(0.3, 0.0));
			R.Step(Dt);
			TestTrue(FString::Printf(TEXT("%.0f fps: one jitter count is 3 deg/s (%.4f) and not manual (%s)"), Fps, R.Debug().AssistanceInputActivity, *Describe(R)),
				FMath::IsNearlyEqual(R.Debug().AssistanceInputActivity, 3.f, 1e-3f) && R.Assisting());
		}
		// A slow steady mouse turn of 3 deg/s, delivered as per-frame deltas: below the threshold at every frame rate.
		// A steady 20 deg/s turn: manual input within 0.05 s and on every frame after that.
		for (const float Speed : { 3.f, 20.f })
		{
			FRig R;
			StartAssisting(R, Dt);
			int32 FirstManual = INDEX_NONE;
			int32 ManualAfterFirst = 0;
			const int32 N = FMath::RoundToInt(Fps);
			for (int32 I = 0; I < N; ++I)
			{
				R.Director->AddCameraInput(FVector2D(Speed * Dt, 0.0));
				R.Step(Dt);
				if (IsManual(R))
				{
					FirstManual = FirstManual == INDEX_NONE ? I : FirstManual;
					++ManualAfterFirst;
				}
			}
			if (Speed < 5.f)
			{
				TestTrue(FString::Printf(TEXT("%.0f fps: 3 deg/s mouse is never manual (activity %.3f)"), Fps, R.Debug().AssistanceInputActivity),
					FirstManual == INDEX_NONE && R.Debug().AssistanceInputActivity < 5.f);
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%.0f fps: 20 deg/s mouse is manual within 0.05 s (frame %d)"), Fps, FirstManual),
					FirstManual != INDEX_NONE && (FirstManual + 1) * Dt <= 0.05f + 1e-4f);
				TestEqual(FString::Printf(TEXT("%.0f fps: and on every frame after that"), Fps), ManualAfterFirst, N - FirstManual);
			}
		}
	}

	// Mouse reports at 125 Hz while rendering at 240 fps (frames without a delta between reports): a continuous slow turn
	// (20 deg/s) never lets the assistance resume while it lasts, and the wait restarts at the last report.
	{
		const float Dt = 1.f / 240.f;
		FRig R;
		StartAssisting(R, Dt);
		int32 Reports = 0;
		bool bAssistedWhileTurning = false;
		int32 Blocked = 0;
		for (int32 I = 1; I <= 480; ++I)
		{
			const int32 Due = FMath::FloorToInt(I * Dt * 125.f);
			if (Due > Reports)
			{
				R.Director->AddCameraInput(FVector2D(20.0 / 125.0 * (Due - Reports), 0.0));
				Reports = Due;
			}
			R.Step(Dt);
			Blocked += IsManual(R) ? 1 : 0;
			bAssistedWhileTurning |= I > 24 && R.Assisting();
		}
		TestFalse(TEXT("125 Hz mouse at 240 fps: no assistance while the player turns"), bAssistedWhileTurning);
		TestTrue(FString::Printf(TEXT("125 Hz mouse at 240 fps: the report frames are manual input (%d of 480 frames)"), Blocked), Blocked >= 230 && Blocked <= 250);
		const int32 Resume = R.StepUntil(Dt, 480, [&R] { return R.Assisting(); });
		TestTrue(FString::Printf(TEXT("Resumes about Recenter Delay after the last report (%d frames)"), Resume), Resume >= 118 && Resume <= 121);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistInputDevicesTest, "ActionCameraFramework.Assistance.Input.DeviceSwitchingAndSensitivity", ACFTests::Flags)
bool FACFAssistInputDevicesTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	{
		FRig R; // standing: nothing but the input turns the camera
		// Stick held, then the player switches to the mouse: the rate is per frame (not kept), so no stale stick input.
		for (int32 I = 0; I < 30; ++I)
		{
			R.Director->SetCameraInputRate(FVector2D(30.0, 0.0));
			R.Step(Dt60);
		}
		float Before = R.Yaw();
		R.Director->AddCameraInput(FVector2D(2.0, 0.0));
		R.Step(Dt60);
		TestTrue(FString::Printf(TEXT("Switch to mouse: exactly the delta (%.4f)"), Delta(R.Yaw(), Before)), FMath::IsNearlyEqual(Delta(R.Yaw(), Before), 2.f, 1e-4f));
		// Both in one frame (a player touching both): delta + rate x dt, measured as the larger activity.
		Before = R.Yaw();
		R.Director->AddCameraInput(FVector2D(0.1, 0.0));
		R.Director->SetCameraInputRate(FVector2D(30.0, 0.0));
		R.Step(Dt60);
		TestTrue(TEXT("Both devices in one frame: summed"), FMath::IsNearlyEqual(Delta(R.Yaw(), Before), 0.1f + 30.f * Dt60, 1e-4f));
		TestTrue(FString::Printf(TEXT("Both devices: activity is the stick's 30 deg/s (%.3f)"), R.Debug().AssistanceInputActivity), FMath::IsNearlyEqual(R.Debug().AssistanceInputActivity, 30.f, 1e-3f));
	}
	// The threshold is in degrees after Sensitivity (per-device sensitivity belongs to the game's input; ACF's applies to
	// both): rate 3 is 3 deg/s at Sensitivity 1 (noise) and 6 deg/s at Sensitivity 2 (manual). Pitch and inverted axes count.
	struct FCase { FVector2D Sensitivity; FVector2D Rate; bool bInvert; bool bManual; const TCHAR* Name; };
	const FCase Cases[] = {
		{ FVector2D(1.0, 1.0), FVector2D(3.0, 0.0), false, false, TEXT("rate 3, sensitivity 1") },
		{ FVector2D(2.0, 1.0), FVector2D(3.0, 0.0), false, true, TEXT("rate 3, sensitivity 2") },
		{ FVector2D(1.0, 1.0), FVector2D(0.0, 8.0), false, true, TEXT("pitch only, 8 deg/s") },
		{ FVector2D(1.0, 1.0), FVector2D(-8.0, 0.0), true, true, TEXT("inverted yaw, -8") },
		{ FVector2D(-2.0, 1.0), FVector2D(3.0, 0.0), false, true, TEXT("negative sensitivity -2") },
	};
	for (const FCase& Case : Cases)
	{
		FRig R([&Case](UACFCameraNode_OrbitalFollow& O) { O.Sensitivity = Case.Sensitivity; O.bInvertYaw = Case.bInvert; });
		StartAssisting(R, Dt60);
		R.Director->SetCameraInputRate(Case.Rate);
		R.Step(Dt60);
		TestEqual(FString::Printf(TEXT("%s: manual input (%s)"), Case.Name, *Describe(R)), IsManual(R), Case.bManual);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistInputTimeDilationTest, "ActionCameraFramework.Assistance.Input.TimeDilation", ACFTests::Flags)
bool FACFAssistInputTimeDilationTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	// Slow motion 0.25: the camera's DeltaTime is game time (a quarter of the real 1/60 s).
	const float Dilation = 0.25f;
	const float GameDt = Dilation / 60.f;
	for (const bool bRealTime : { false, true })
	{
		FRig R([bRealTime](UACFCameraNode_OrbitalFollow& O) { O.bInputRateIgnoresTimeDilation = bRealTime; });
		R.Driver.TimeDilation = Dilation;
		float Before = R.Yaw();
		R.Director->SetCameraInputRate(FVector2D(60.0, 0.0));
		R.Step(GameDt);
		const float Expected = bRealTime ? 1.f : 0.25f; // 60 deg/s x real 1/60 s, or x game 1/240 s
		TestTrue(FString::Printf(TEXT("Real time %d: stick step %.4f (expected %.2f)"), bRealTime, Delta(R.Yaw(), Before), Expected),
			FMath::IsNearlyEqual(Delta(R.Yaw(), Before), Expected, 1e-4f));
		Before = R.Yaw();
		R.Director->AddCameraInput(FVector2D(2.0, 0.0));
		R.Step(GameDt);
		TestTrue(FString::Printf(TEXT("Real time %d: mouse delta never scaled by time"), bRealTime), FMath::IsNearlyEqual(Delta(R.Yaw(), Before), 2.f, 1e-4f));

		// The mouse window runs in real time: 0.1 real s after a 0.3 deg count (6 game frames of 1/240 s = 6 real frames of
		// 1/60 s at 0.25) it has decayed to 3 / e, whatever the dilation.
		R.Step(GameDt * 10.f); // let the 2 deg delta fade
		R.Run(1.f, GameDt);
		R.Director->AddCameraInput(FVector2D(0.3, 0.0));
		R.Step(GameDt);
		for (int32 I = 0; I < 6; ++I)
		{
			R.Step(GameDt);
		}
		TestTrue(FString::Printf(TEXT("Real time %d: mouse activity decays in real time (%.4f, expected %.4f)"), bRealTime, R.Debug().AssistanceInputActivity, 3.f * FMath::Exp(-1.f)),
			FMath::IsNearlyEqual(R.Debug().AssistanceInputActivity, 3.f * FMath::Exp(-1.f), 1e-3f));
		// The stick rate is compared as it is (deg/s): 10 is manual input in slow motion too.
		StartAssisting(R, GameDt);
		R.Director->SetCameraInputRate(FVector2D(10.0, 0.0));
		R.Step(GameDt);
		TestTrue(FString::Printf(TEXT("Real time %d: stick 10 deg/s is manual input in slow motion"), bRealTime), IsManual(R));
	}

	// Invalid dilation from a caller (0, NaN, negative) is 1: no division by zero, nothing invalid in the pose.
	for (const float Bad : { 0.f, NAN, -1.f })
	{
		FRig R([](UACFCameraNode_OrbitalFollow& O) { O.bInputRateIgnoresTimeDilation = true; });
		R.Driver.TimeDilation = Bad;
		const float Before = R.Yaw();
		R.Director->SetCameraInputRate(FVector2D(60.0, 0.0));
		R.Step(Dt60);
		TestTrue(FString::Printf(TEXT("Dilation %f: treated as 1 (step %.4f)"), Bad, Delta(R.Yaw(), Before)), FMath::IsNearlyEqual(Delta(R.Yaw(), Before), 1.f, 1e-4f));
	}

	// Dilation 1 with Real Time on is bitwise the default path.
	{
		FRig A;
		FRig B([](UACFCameraNode_OrbitalFollow& O) { O.bInputRateIgnoresTimeDilation = true; });
		A.Velocity = B.Velocity = Heading(70.f, 300.f);
		FRandomStream Random(7);
		bool bSame = true;
		for (int32 I = 0; I < 300; ++I)
		{
			const FVector2D Rate(Random.FRandRange(-40.f, 40.f), Random.FRandRange(-20.f, 20.f));
			const FVector2D Mouse(Random.FRandRange(-1.f, 1.f), 0.0);
			for (FRig* R : { &A, &B })
			{
				if (I % 50 < 20)
				{
					R->Director->SetCameraInputRate(Rate);
					R->Director->AddCameraInput(Mouse);
				}
				R->Step(Dt60);
			}
			bSame &= A.Director->GetLastResult().Pose.Rotation == B.Director->GetLastResult().Pose.Rotation
				&& A.Director->GetLastResult().Pose.Location == B.Director->GetLastResult().Pose.Location;
		}
		TestTrue(TEXT("Dilation 1: Real Time on == off, bitwise"), bSame);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistInputCompatTest, "ActionCameraFramework.Assistance.Input.CompatibilityAndPlayers", ACFTests::Flags)
bool FACFAssistInputCompatTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	// Without Movement Assistance the threshold changes nothing (the idle recenter of Phase 2 still stops on any input).
	{
		auto Configure = [](float Threshold)
		{
			return [Threshold](UACFCameraNode_OrbitalFollow& O)
			{
				O.bEnableMovementAssistance = false;
				O.bRecenterWhenIdle = true;
				O.RecenterHeading = EACFCameraRecenterHeading::TargetFacing;
				O.InputActivityThreshold = Threshold;
			};
		};
		FRig A(Configure(0.f));
		FRig B(Configure(30.f));
		A.Velocity = B.Velocity = Heading(30.f, 200.f);
		FRandomStream Random(11);
		bool bSame = true;
		for (int32 I = 0; I < 400; ++I)
		{
			const FVector2D Rate(Random.FRandRange(-3.f, 3.f), 0.0); // drift-sized input: below either threshold
			for (FRig* R : { &A, &B })
			{
				if (I % 100 < 30)
				{
					R->Director->SetCameraInputRate(Rate);
				}
				R->Step(Dt60);
			}
			bSame &= A.Director->GetLastResult().Pose.Rotation == B.Director->GetLastResult().Pose.Rotation;
		}
		TestTrue(TEXT("Assistance off: the threshold does not change the idle recenter (bitwise)"), bSame);
		TestFalse(TEXT("Assistance off: no input row in the debugger"), A.Debug().bHasAssistanceInput);
	}

	// Two local players (two directors): one player's input never stops the other's assistance.
	{
		FRig P1;
		FRig P2;
		StartAssisting(P1, Dt60);
		StartAssisting(P2, Dt60);
		bool bP1Blocked = true;
		bool bP2Assisting = true;
		for (int32 I = 0; I < 120; ++I)
		{
			P1.Director->SetCameraInputRate(FVector2D(40.0, 0.0));
			P1.Step(Dt60);
			P2.Step(Dt60);
			bP1Blocked &= IsManual(P1);
			bP2Assisting &= P2.Assisting();
		}
		TestTrue(TEXT("Player 1 turns: blocked every frame"), bP1Blocked);
		TestTrue(TEXT("Player 2 untouched: assisting every frame"), bP2Assisting);
	}
	return true;
}

// Runtime Tuning of the recenter / assistance values (Phase 7B-2): the rows exist where the node uses them, take effect on
// the next update, are validated, reset, and never write the asset.
#if ACF_WITH_RUNTIME_TUNING
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistTuningTest, "ActionCameraFramework.Tuning.MovementAssistanceParameters", ACFTests::Flags)
bool FACFAssistTuningTest::RunTest(const FString& Parameters)
{
	using namespace ACFAssistTests;
	using EParam = EACFCameraTuningParameter;
	using EResult = EACFCameraTuningResult;
	const EParam AssistOnly[] = { EParam::AssistStartSpeed, EParam::AssistStopSpeed, EParam::AssistMovementDuration, EParam::AssistBackwardExclusion,
		EParam::AssistDeadZone, EParam::AssistMaxYawSpeed, EParam::AssistInputThreshold };
	auto NodeParameters = [](FRig& R)
	{
		TArray<FACFCameraTuningNodeInfo> Nodes;
		R.Director->GetRequestNodes(R.Handle, Nodes);
		return Nodes.Num() == 1 ? Nodes[0].Parameters : TArray<EParam>();
	};
	{
		FRig On;
		const TArray<EParam> P = NodeParameters(On);
		bool bAll = P.Contains(EParam::RecenterDelay) && P.Contains(EParam::RecenterTime);
		for (const EParam Parameter : AssistOnly)
		{
			bAll &= P.Contains(Parameter);
		}
		TestTrue(TEXT("Assistance on: recenter and all assistance parameters"), bAll);
		FRig Idle([](UACFCameraNode_OrbitalFollow& O) { O.bEnableMovementAssistance = false; O.bRecenterWhenIdle = true; });
		const TArray<EParam> IdleParameters = NodeParameters(Idle);
		TestTrue(TEXT("Idle recenter only: Recenter Delay / Time, no assistance parameter"), IdleParameters.Contains(EParam::RecenterDelay) && IdleParameters.Contains(EParam::RecenterTime) && !IdleParameters.Contains(EParam::AssistMaxYawSpeed));
		FRig None([](UACFCameraNode_OrbitalFollow& O) { O.bEnableMovementAssistance = false; });
		TestFalse(TEXT("No recenter: no recenter parameter"), NodeParameters(None).Contains(EParam::RecenterDelay));
		TestFalse(TEXT("No recenter: a reason is given"), None.Director->GetTuningUnavailableReason(None.Handle, EParam::AssistMaxYawSpeed).IsEmpty());
		TestFalse(TEXT("Assistance rows are listed only where they exist (not 'always listed')"), ACFCameraTuning::IsAlwaysListed(EParam::AssistMaxYawSpeed));
		TestTrue(TEXT("The 7A rows are still always listed"), ACFCameraTuning::IsAlwaysListed(EParam::CollisionRadius));
	}

	FRig R;
	TestEqual(TEXT("Start tuning"), R.Director->StartTuning(R.Handle), EResult::Ok);
	StartAssisting(R, Dt60);
	// Threshold 0 through tuning: the same drift now is manual input.
	TestEqual(TEXT("Set Input Activity Threshold 0"), R.Director->SetTuningValue(R.Handle, EParam::AssistInputThreshold, FVector(0.0)), EResult::Ok);
	R.Director->SetCameraInputRate(FVector2D(2.0, 0.0));
	R.Step(Dt60);
	TestTrue(TEXT("Tuned threshold takes effect on the next update"), IsManual(R));
	// Max yaw speed 20 deg/s: the assistance turns at most 20 x dt per frame (heading 90 from a camera turned back to 0).
	TestEqual(TEXT("Set Max Assistance Yaw Speed 20"), R.Director->SetTuningValue(R.Handle, EParam::AssistMaxYawSpeed, FVector(20.0)), EResult::Ok);
	R.Director->SetOrbitRotation(R.Handle, FRotator(-15.f, 0.f, 0.f), true);
	R.Step(Dt60);
	float MaxStep = 0.f;
	R.Run(2.f, Dt60, [&R, &MaxStep, Previous = R.Yaw()](int32) mutable
	{
		MaxStep = FMath::Max(MaxStep, FMath::Abs(Delta(R.Yaw(), Previous)));
		Previous = R.Yaw();
	});
	TestTrue(FString::Printf(TEXT("Tuned max yaw speed: largest step %.4f <= 20 / 60"), MaxStep), MaxStep > 0.f && MaxStep <= 20.f * Dt60 + 1e-4f);
	// Invalid values change nothing.
	TestEqual(TEXT("Backward Exclusion 200 rejected"), R.Director->SetTuningValue(R.Handle, EParam::AssistBackwardExclusion, FVector(200.0)), EResult::InvalidValue);
	TestEqual(TEXT("Max yaw speed 0 rejected"), R.Director->SetTuningValue(R.Handle, EParam::AssistMaxYawSpeed, FVector(0.0)), EResult::InvalidValue);
	TestEqual(TEXT("Negative threshold rejected"), R.Director->SetTuningValue(R.Handle, EParam::AssistInputThreshold, FVector(-1.0)), EResult::InvalidValue);
	TestEqual(TEXT("NaN rejected"), R.Director->SetTuningValue(R.Handle, EParam::RecenterDelay, FVector(NAN, 0.0, 0.0)), EResult::InvalidValue);
	TestEqual(TEXT("Recenter Time below 0.01 rejected"), R.Director->SetTuningValue(R.Handle, EParam::RecenterTime, FVector(0.0)), EResult::InvalidValue);
	TestEqual(TEXT("Two values changed"), R.Director->GetTuningStatus(R.Handle).NumChangedValues, 2);
	TestTrue(TEXT("The asset is untouched"), R.Orbit->MaxAssistanceYawSpeed == 90.f && R.Orbit->InputActivityThreshold == 5.f);
	FACFCameraTuningValue Value;
	TestEqual(TEXT("Read back"), R.Director->GetTuningValue(R.Handle, EParam::AssistMaxYawSpeed, INDEX_NONE, Value), EResult::Ok);
	TestTrue(TEXT("Read back: 20 (asset 90), scalar, property name"), Value.Value.X == 20.0 && Value.OriginalValue.X == 90.0 && Value.bScalar && Value.Property == FName(TEXT("MaxAssistanceYawSpeed")));
	// Reset: the noise no longer blocks.
	TestEqual(TEXT("Reset all"), R.Director->ResetAllTuningValues(R.Handle), EResult::Ok);
	R.Director->SetCameraInputRate(FVector2D(2.0, 0.0));
	R.Step(Dt60);
	TestFalse(TEXT("After reset: drift is noise again"), IsManual(R));
	TestEqual(TEXT("Stop tuning"), R.Director->StopTuning(R.Handle), EResult::Ok);
	return true;
}
#endif

#endif // WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
