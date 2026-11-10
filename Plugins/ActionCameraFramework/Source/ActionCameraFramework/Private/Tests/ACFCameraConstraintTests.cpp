#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestUtils.h"
#include "ACFCameraConstraintSolver.h"
#include "Components/SceneComponent.h"
#include "Effects/ACFCameraBuiltinEffects.h"
#include "Effects/ACFCameraEffect.h"
#include "Nodes/ACFCameraFraming.h"
#include "Nodes/ACFCameraNode_Combat.h"
#include "Nodes/ACFCameraNode_GroupFraming.h"
#include "Nodes/ACFCameraNode_LockOn.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "UObject/StrongObjectPtr.h"

/** Phase 4B unit tests: constraint solver (deoccluder, framing recovery) with injected geometry, Combat Camera. */
namespace ACFConstraintTests
{
	using namespace ACFTests;
	constexpr float Dt = 1.f / 60.f;
	constexpr float Aspect = 16.f / 9.f;

	/** Axis-aligned boxes standing in for level geometry. */
	struct FFakeWorld
	{
		TArray<FBox> Boxes;
		float Probe = 12.f;
		int32 Sweeps = 0;
		int32 Traces = 0;

		FVector Collide(const FVector& Pivot, const FVector& Want)
		{
			++Sweeps;
			double Best = 1.0;
			for (const FBox& Box : Boxes)
			{
				double Time = 1.0;
				if (SegmentHitsBox(Box.ExpandBy(Probe), Pivot, Want, &Time))
				{
					Best = FMath::Min(Best, Time);
				}
			}
			return Pivot + (Want - Pivot) * Best;
		}

		bool IsBlocked(const FVector& From, const FACFCameraFramingSubject& Subject)
		{
			++Traces;
			for (const FBox& Box : Boxes)
			{
				if (SegmentHitsBox(Box, From, Subject.Location))
				{
					return true;
				}
			}
			return false;
		}

		/** Slab test: does the segment A -> B touch the box? */
		static bool SegmentHitsBox(const FBox& Box, const FVector& A, const FVector& B, double* OutTime = nullptr)
		{
			double T0 = 0.0;
			double T1 = 1.0;
			const FVector D = B - A;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				if (FMath::Abs(D[Axis]) < 1e-9)
				{
					if (A[Axis] < Box.Min[Axis] || A[Axis] > Box.Max[Axis])
					{
						return false;
					}
					continue;
				}
				double Near = (Box.Min[Axis] - A[Axis]) / D[Axis];
				double Far = (Box.Max[Axis] - A[Axis]) / D[Axis];
				if (Near > Far)
				{
					Swap(Near, Far);
				}
				T0 = FMath::Max(T0, Near);
				T1 = FMath::Min(T1, Far);
				if (T0 > T1)
				{
					return false;
				}
			}
			if (OutTime)
			{
				*OutTime = T0;
			}
			return true;
		}
	};

	inline FACFCameraFramingSubject Subject(const FVector& Location, float Radius, float Weight, bool bPlayer, const void* Key)
	{
		FACFCameraFramingSubject S;
		S.Location = Location;
		S.Radius = Radius;
		S.Weight = Weight;
		S.bIsPlayer = bPlayer;
		S.Actor = static_cast<const AActor*>(Key); // identity only in these tests (never dereferenced)
		return S;
	}

	inline FACFCameraDeoccluderSettings Deoccluder()
	{
		FACFCameraDeoccluderSettings Settings;
		Settings.bEnabled = true;
		return Settings;
	}

	/** Pivot at the origin, camera 400 cm behind and 100 cm up, looking at the pivot. */
	inline FACFCameraPose BasePose()
	{
		FACFCameraPose Pose;
		Pose.Location = FVector(-400, 0, 100);
		Pose.Rotation = (FVector::ZeroVector - Pose.Location).Rotation();
		Pose.FieldOfView = 90.f;
		return Pose;
	}

	struct FRun
	{
		FACFCameraDeoccluderState State;
		FACFCameraSolverReport Report;
		FACFCameraPose Pose;
		int32 Searches = 0;
		int32 MaxTracesPerFrame = 0;

		void Step(FFakeWorld& World, const FACFCameraDeoccluderSettings& Settings, const FACFCameraFramingSubjects& Subjects, const FACFCameraPose& Input = BasePose())
		{
			auto Collide = [&World](const FVector& P, const FVector& W) { return World.Collide(P, W); };
			auto Blocked = [&World](const FVector& F, const FACFCameraFramingSubject& S) { return World.IsBlocked(F, S); };
			const FACFCameraSolverQueries Queries{ Collide, Blocked };
			Pose = Input;
			Report = FACFCameraSolverReport();
			FACFCameraConstraintSolver::Deocclude(Settings, FVector::ZeroVector, Subjects, Dt, State, Queries, Pose, Report);
			Searches += Report.bSearched ? 1 : 0;
			MaxTracesPerFrame = FMath::Max(MaxTracesPerFrame, Report.NumTraces);
		}
	};

	/** Thin pillar on the camera -> enemy line (enemy at (1000, 0, 0)), clear of the pivot -> camera line. */
	inline FBox EnemyBlocker() { return FBox(FVector(-220, -30, 75), FVector(-180, 30, 200)); }

	inline USceneComponent* MakeTarget(const FVector& Location)
	{
		USceneComponent* Component = NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetWorldLocation(Location);
		return Component;
	}
}

// -------------------------------------------------------------------------------------------------
// Solver math

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSolverOrbitCorrectionTest, "ActionCameraFramework.Constraints.OrbitCorrectionMath", ACFTests::Flags)
bool FACFSolverOrbitCorrectionTest::RunTest(const FString& Parameters)
{
	using namespace ACFConstraintTests;
	const FACFCameraPose Base = BasePose();
	const ACFFraming::FLens Lens = ACFFraming::MakeLens(90.f, Aspect);
	auto PivotOnScreen = [&Lens](const FACFCameraPose& Pose)
	{
		FVector2D Screen;
		double Depth;
		ACFFraming::Project(Pose.Location, Pose.Rotation, Lens, FVector::ZeroVector, Screen, Depth);
		return Screen;
	};
	const FACFCameraPose Yawed = FACFCameraConstraintSolver::ApplyOrbitCorrection(Base, FVector::ZeroVector, FVector(90, 0, 0));
	TestTrue(TEXT("Yaw +90 orbits the camera around the pivot"), Yawed.Location.Equals(FVector(0, -400, 100), 0.01));
	TestTrue(TEXT("Yaw keeps the pivot centred"), PivotOnScreen(Yawed).Equals(FVector2D::ZeroVector, 1e-4));
	const FACFCameraPose Raised = FACFCameraConstraintSolver::ApplyOrbitCorrection(Base, FVector::ZeroVector, FVector(0, 20, 0));
	TestTrue(TEXT("Positive pitch raises the camera"), Raised.Location.Z > Base.Location.Z + 50.0);
	TestTrue(TEXT("Pitch keeps the arm length"), FMath::IsNearlyEqual(Raised.Location.Size(), Base.Location.Size(), 0.01));
	TestTrue(TEXT("Pitch keeps the pivot centred"), PivotOnScreen(Raised).Equals(FVector2D::ZeroVector, 1e-4));
	TestTrue(TEXT("Pitch does not roll"), FMath::IsNearlyZero(Raised.Rotation.Roll, 1e-3f));
	const FACFCameraPose Pulled = FACFCameraConstraintSolver::ApplyOrbitCorrection(Base, FVector::ZeroVector, FVector(0, 0, 0.5f));
	TestTrue(TEXT("Pull 0.5 halves the arm"), Pulled.Location.Equals(Base.Location * 0.5, 0.01));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Deoccluder

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDeoccluderClearTest, "ActionCameraFramework.Constraints.DeoccluderNoOcclusionAndTransient", ACFTests::Flags)
bool FACFDeoccluderClearTest::RunTest(const FString& Parameters)
{
	using namespace ACFConstraintTests;
	int32 PlayerKey, EnemyKey;
	FACFCameraFramingSubjects Subjects;
	Subjects.Add(Subject(FVector::ZeroVector, 50.f, 1.f, true, &PlayerKey));
	Subjects.Add(Subject(FVector(1000, 0, 0), 80.f, 1.f, false, &EnemyKey));
	const FACFCameraDeoccluderSettings Settings = Deoccluder();
	FFakeWorld World;
	FRun Run;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Run.Step(World, Settings, Subjects);
	}
	TestTrue(TEXT("No obstacle: no correction"), Run.State.Current.IsZero() && Run.Pose.Location.Equals(BasePose().Location));
	TestEqual(TEXT("No obstacle: no search"), Run.Searches, 0);
	TestEqual(TEXT("No obstacle: 1 sweep + 1 trace per subject per frame"), Run.MaxTracesPerFrame, 3);

	// Blocked for 0.15 s (< Occluded Delay 0.25 s): ignored, the camera does not move at all.
	World.Boxes.Add(EnemyBlocker());
	for (int32 Frame = 0; Frame < 9; ++Frame)
	{
		Run.Step(World, Settings, Subjects);
	}
	World.Boxes.Reset();
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Run.Step(World, Settings, Subjects);
	}
	TestEqual(TEXT("Transient occlusion: no search"), Run.Searches, 0);
	TestTrue(TEXT("Transient occlusion: camera never moved"), Run.State.Goal.IsZero() && Run.State.Current.IsZero());

	// Flicker (blocked 0.1 s, clear 0.1 s, ...): never reaches the delay, so no oscillation.
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		World.Boxes.Reset();
		if ((Frame / 6) % 2 == 0)
		{
			World.Boxes.Add(EnemyBlocker());
		}
		Run.Step(World, Settings, Subjects);
	}
	TestEqual(TEXT("Flickering occlusion: no search, no movement"), Run.Searches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDeoccluderPersistentTest, "ActionCameraFramework.Constraints.DeoccluderPersistentAndReturn", ACFTests::Flags)
bool FACFDeoccluderPersistentTest::RunTest(const FString& Parameters)
{
	using namespace ACFConstraintTests;
	int32 PlayerKey, EnemyKey;
	FACFCameraFramingSubjects Subjects;
	Subjects.Add(Subject(FVector::ZeroVector, 50.f, 1.f, true, &PlayerKey));
	Subjects.Add(Subject(FVector(1000, 0, 0), 80.f, 1.f, false, &EnemyKey));
	const FACFCameraDeoccluderSettings Settings = Deoccluder();
	FFakeWorld World;
	World.Boxes.Add(EnemyBlocker());
	FRun Run;

	int32 FirstSearchFrame = INDEX_NONE;
	double MaxStep = 0.0;
	FVector Previous = BasePose().Location;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Run.Step(World, Settings, Subjects);
		if (Run.Report.bSearched && FirstSearchFrame == INDEX_NONE)
		{
			FirstSearchFrame = Frame;
		}
		MaxStep = FMath::Max(MaxStep, FVector::Dist(Run.Pose.Location, Previous));
		Previous = Run.Pose.Location;
	}
	AddInfo(FString::Printf(TEXT("Persistent occlusion: first search at frame %d, goal (%.1f, %.1f, %.2f), max move %.1f cm/frame, %d searches, max %d traces in a frame"),
		FirstSearchFrame, Run.State.Goal.X, Run.State.Goal.Y, Run.State.Goal.Z, MaxStep, Run.Searches, Run.MaxTracesPerFrame));
	TestTrue(TEXT("Reacts only after the Occluded Delay (0.25 s)"), FirstSearchFrame >= 14 && FirstSearchFrame <= 16);
	TestEqual(TEXT("One search solves it"), Run.Searches, 1);
	TestFalse(TEXT("A correction was chosen"), Run.State.Goal.IsZero());
	TestTrue(TEXT("Correction within the limits"), FMath::Abs(Run.State.Goal.X) <= Settings.MaxYawOffset && Run.State.Goal.Y <= Settings.MaxPitchOffset && Run.State.Goal.Z <= Settings.MaxPullIn);
	TestEqual(TEXT("Enemy revealed (nothing unresolved)"), Run.Report.NumOccluded, 0);
	TestFalse(TEXT("Enemy visible from the corrected camera"), World.IsBlocked(Run.Pose.Location, Subjects[1]));
	TestFalse(TEXT("Player visible from the corrected camera"), World.IsBlocked(Run.Pose.Location, Subjects[0]));
	TestTrue(TEXT("Correction is damped (no jump)"), MaxStep < 25.0);
	TestTrue(TEXT("Search cost is bounded (<= 1 + 10 candidates x (1 + subjects))"), Run.MaxTracesPerFrame <= 3 + 10 * 3 + 3);

	// The obstacle goes away: hold for Clear Delay (0.5 s), then glide back.
	World.Boxes.Reset();
	const FVector Corrected = Run.Pose.Location;
	for (int32 Frame = 0; Frame < 25; ++Frame)
	{
		Run.Step(World, Settings, Subjects);
	}
	TestTrue(TEXT("Clear for < Clear Delay: correction kept"), Run.Pose.Location.Equals(Corrected, 1.0));
	Run.Step(World, Settings, Subjects);
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Run.Step(World, Settings, Subjects);
	}
	TestTrue(TEXT("Return is smooth (still on the way after 10 frames)"), !Run.State.Current.IsNearlyZero(0.5) && Run.State.Goal.IsZero());
	for (int32 Frame = 0; Frame < 300; ++Frame)
	{
		Run.Step(World, Settings, Subjects);
	}
	TestTrue(TEXT("Back to the original camera"), Run.Pose.Location.Equals(BasePose().Location, 0.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFDeoccluderMultiTest, "ActionCameraFramework.Constraints.DeoccluderMultipleAndUnsolvable", ACFTests::Flags)
bool FACFDeoccluderMultiTest::RunTest(const FString& Parameters)
{
	using namespace ACFConstraintTests;
	int32 PlayerKey, EnemyAKey, EnemyBKey;
	FACFCameraFramingSubjects Subjects;
	Subjects.Add(Subject(FVector(1000, 0, 0), 80.f, 1.f, false, &EnemyAKey));
	Subjects.Add(Subject(FVector(1200, 400, 0), 80.f, 2.f, false, &EnemyBKey));
	Subjects.Add(Subject(FVector::ZeroVector, 50.f, 1.f, true, &PlayerKey)); // listed last: still checked first
	const FACFCameraDeoccluderSettings Settings = Deoccluder();
	FFakeWorld World;
	World.Boxes.Add(EnemyBlocker());                                              // hides A from the base camera
	World.Boxes.Add(FBox(FVector(1150, 350, -50), FVector(1250, 450, 50)));      // B is inside a closed box: never visible
	FRun Run;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Run.Step(World, Settings, Subjects);
	}
	AddInfo(FString::Printf(TEXT("Two hidden enemies: goal (%.1f, %.1f, %.2f), unresolved %d, searches %d"), Run.State.Goal.X, Run.State.Goal.Y, Run.State.Goal.Z, Run.Report.NumOccluded, Run.Searches));
	TestFalse(TEXT("Revealable enemy revealed"), World.IsBlocked(Run.Pose.Location, Subjects[0]));
	TestEqual(TEXT("Unrevealable enemy reported"), Run.Report.NumOccluded, 1);
	TestFalse(TEXT("Player stays visible"), Run.Report.bPlayerOccluded);
	TestTrue(TEXT("Unresolved: searches are throttled (Retry Interval 0.3 s -> <= 7 in 1.75 s)"), Run.Searches <= 7);

	// Player itself unrevealable: bounded correction, reported, no NaN.
	FACFCameraFramingSubjects PlayerOnly;
	PlayerOnly.Add(Subject(FVector(0, 0, -20), 50.f, 1.f, true, &PlayerKey));
	FFakeWorld Sealed;
	Sealed.Boxes.Add(FBox(FVector(-30, -30, -40), FVector(30, 30, 0)));
	FRun Hidden;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Hidden.Step(Sealed, Settings, PlayerOnly);
	}
	TestTrue(TEXT("Hidden player reported"), Hidden.Report.bPlayerOccluded);
	TestTrue(TEXT("Unsolvable: correction stays within the limits"), FMath::Abs(Hidden.State.Current.X) <= Settings.MaxYawOffset + 0.01f
		&& Hidden.State.Current.Y <= Settings.MaxPitchOffset + 0.01f && Hidden.State.Current.Z <= Settings.MaxPullIn + 0.01f);
	TestFalse(TEXT("Unsolvable: finite"), Hidden.Pose.Location.ContainsNaN());

	// A subject disappears (destroyed) while hidden: its state is dropped, the camera returns.
	FACFCameraFramingSubjects Fewer;
	Fewer.Add(Subjects[2]);
	for (int32 Frame = 0; Frame < 400; ++Frame)
	{
		Run.Step(World, Settings, Fewer);
	}
	TestEqual(TEXT("Destroyed subject: state shrinks"), Run.State.NumSubjects, 1);
	TestEqual(TEXT("Destroyed subject: nothing unresolved"), Run.Report.NumOccluded, 0);
	TestTrue(TEXT("Destroyed subject: correction released"), Run.State.Current.IsNearlyZero(0.1));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Framing recovery (Collision vs Group Framing)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFFramingRecoveryTest, "ActionCameraFramework.Constraints.FramingRecoveryFovLimitAndPlayer", ACFTests::Flags)
bool FACFFramingRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace ACFConstraintTests;
	// Collision pushed the camera to 150 cm behind the player; the enemies to the sides no longer fit at 70 deg.
	FACFCameraFramingSubjects Subjects;
	Subjects.Add(Subject(FVector::ZeroVector, 50.f, 1.f, true, nullptr));
	Subjects.Add(Subject(FVector(500, 350, 0), 80.f, 1.f, false, nullptr));
	Subjects.Add(Subject(FVector(500, -350, 0), 80.f, 1.f, false, nullptr));
	const FVector2D Size(0.85, 0.85);
	FACFCameraPose Pushed;
	Pushed.Location = FVector(-150, 0, 40);
	Pushed.Rotation = FRotator(-5.f, 0.f, 0.f);
	Pushed.FieldOfView = 70.f;

	auto Recover = [&](const FACFCameraFramingRecoverySettings& Settings, const FACFCameraPose& Input, int32 Frames, FACFCameraSolverReport& Report, float* OutFirst = nullptr)
	{
		FACFCameraFramingRecoveryState State;
		State.bInitialized = true; // as after activation: changes are damped
		FACFCameraPose Pose;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Pose = Input;
			Report = FACFCameraSolverReport();
			FACFCameraConstraintSolver::RecoverFraming(Settings, Subjects, Size, Aspect, Dt, State, Pose, Report);
			if (Frame == 0 && OutFirst)
			{
				*OutFirst = Pose.FieldOfView;
			}
		}
		return Pose;
	};
	const double Required = UACFCameraNode_GroupFraming::ComputeRequiredFov(Subjects, Pushed.Location, Pushed.Rotation, Aspect, Size, 0.f, 170.f);

	FACFCameraFramingRecoverySettings Settings;
	Settings.bEnabled = true;
	Settings.MaxFieldOfView = 120.f;
	Settings.MaxFieldOfViewIncrease = 40.f;
	FACFCameraSolverReport Report;
	float First = 0.f;
	const FACFCameraPose Recovered = Recover(Settings, Pushed, 90, Report, &First);
	AddInfo(FString::Printf(TEXT("Recovery: required %.2f deg, first frame %.2f, settled %.2f"), Required, First, Recovered.FieldOfView));
	TestTrue(TEXT("FOV widened to the required value"), FMath::IsNearlyEqual(Recovered.FieldOfView, static_cast<float>(Required), 0.05f));
	TestTrue(TEXT("Widening is damped"), First > 70.f && First < Required - 1.0);
	TestFalse(TEXT("Not limited"), Report.bFramingLimited);
	TestTrue(TEXT("Recovery does not move the camera"), Recovered.Location.Equals(Pushed.Location));

	// FOV limit reached: stop at the cap, report it.
	Settings.MaxFieldOfViewIncrease = 8.f;
	const FACFCameraPose Capped = Recover(Settings, Pushed, 90, Report);
	TestTrue(TEXT("FOV capped at incoming + Max Increase"), FMath::IsNearlyEqual(Capped.FieldOfView, 78.f, 0.05f));
	TestTrue(TEXT("Limit reported (enemies may be partly off screen)"), Report.bFramingLimited);
	Settings.MaxFieldOfViewIncrease = 40.f;
	Settings.MaxFieldOfView = 75.f;
	TestTrue(TEXT("Absolute Max FOV also caps"), FMath::IsNearlyEqual(Recover(Settings, Pushed, 90, Report).FieldOfView, 75.f, 0.05f));

	// Already framed: no change at all (no fight with Group Framing).
	FACFCameraPose Wide = Pushed;
	Wide.FieldOfView = static_cast<float>(Required) + 2.f;
	TestEqual(TEXT("Already framed: FOV untouched"), Recover(Settings, Wide, 30, Report).FieldOfView, Wide.FieldOfView);

	// Player off screen even at the limit: a bounded turn towards the player.
	FACFCameraPose Away = Pushed;
	Away.Rotation = FRotator(-5.f, 60.f, 0.f);
	Settings.MaxFieldOfView = 80.f;
	Settings.MaxPlayerRotation = 20.f;
	const FACFCameraPose Turned = Recover(Settings, Away, 120, Report);
	AddInfo(FString::Printf(TEXT("Player off screen: yaw %.2f -> %.2f"), Away.Rotation.Yaw, Turned.Rotation.Yaw));
	TestTrue(TEXT("Turned towards the player"), Report.bPlayerRotationCorrected && Turned.Rotation.Yaw < Away.Rotation.Yaw - 5.f);
	TestTrue(TEXT("Turn bounded by Max Player Rotation"), Away.Rotation.Yaw - Turned.Rotation.Yaw <= 20.01f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Combat Camera

namespace ACFConstraintTests
{
	inline UACFCameraPreset* MakeCombatOrbitPreset(UACFCameraNode_Combat** OutCombat, const FACFCameraBlendSettings& Blend = FACFCameraBlendSettings::MakeCut())
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_OrbitalFollow* Orbit = NewObject<UACFCameraNode_OrbitalFollow>(Preset);
		Orbit->InputSource = EACFCameraOrbitInputSource::None;
		Orbit->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
		Orbit->InitialPitch = 0.f;
		Orbit->PivotOffset = FVector::ZeroVector;
		Orbit->Distance = 400.f;
		UACFCameraNode_Combat* Combat = NewObject<UACFCameraNode_Combat>(Preset);
		Preset->Nodes = { Combat, Orbit }; // listed first: the Framing stage still runs it after the Body node
		Preset->DefaultBlendIn = Blend;
		Preset->DefaultBlendOut = Blend;
		*OutCombat = Combat;
		return Preset;
	}

	inline FFrameDriver Driver()
	{
		FFrameDriver D;
		D.Upstream.FieldOfView = 90.f;
		D.AspectRatio = Aspect;
		return D;
	}

	inline void Run(UACFCameraDirector* Director, FFrameDriver& D, float Seconds)
	{
		for (int32 Frame = 0; Frame < FMath::RoundToInt(Seconds / Dt); ++Frame)
		{
			D.Step(Director, Dt);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFCombatStateTest, "ActionCameraFramework.Constraints.CombatStateSwitch", ACFTests::Flags)
bool FACFCombatStateTest::RunTest(const FString& Parameters)
{
	using namespace ACFConstraintTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Player(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<USceneComponent> Enemy(MakeTarget(FVector(300, 0, 0)));
	UACFCameraNode_Combat* Combat = nullptr;
	TStrongObjectPtr<UACFCameraPreset> Preset(MakeCombatOrbitPreset(&Combat));
	FACFCameraRequestParams Params;
	Params.FollowTarget.Component = Player.Get();
	Params.LookAtTarget.Component = Enemy.Get();
	Director->AddRequest(Preset.Get(), Params);
	FFrameDriver D = Driver();
	Run(Director.Get(), D, 0.5f);
	const FACFCameraPose Explore = Director->GetLastResult().Pose;
	TestTrue(TEXT("Out of combat: plain orbit"), Explore.Location.Equals(FVector(-400, 0, 0), 0.01) && FMath::IsNearlyEqual(Explore.FieldOfView, 90.f));

	FACFCameraCombatState State;
	State.bInCombat = true;
	Director->SetCombatState(State);
	const FACFCameraPose FirstFrame = D.Step(Director.Get(), Dt).Pose;
	Run(Director.Get(), D, 3.f);
	const FACFCameraPose Fight = Director->GetLastResult().Pose;
	const FACFCameraCombatProfile& P = Combat->Profile;
	// Enemy at 300 cm = Near Range: arm x Near Distance Scale, + shoulder in camera space, FOV + offset.
	const FVector Expected = FVector(-400.0 * P.NearDistanceScale, P.ShoulderOffset.Y, P.ShoulderOffset.Z);
	AddInfo(FString::Printf(TEXT("Combat: first frame %s, settled %s (expected %s), FOV %.2f"), *FirstFrame.Location.ToString(), *Fight.Location.ToString(), *Expected.ToString(), Fight.FieldOfView));
	TestTrue(TEXT("Entering combat is smooth (first frame moves < 10 cm)"), FVector::Dist(FirstFrame.Location, Explore.Location) < 10.0);
	TestTrue(TEXT("Combat: near enemy -> Near Distance Scale + shoulder offset"), Fight.Location.Equals(Expected, 0.5));
	TestTrue(TEXT("Combat: FOV + offset"), FMath::IsNearlyEqual(Fight.FieldOfView, 90.f + P.FieldOfViewOffset, 0.05f));
	TestTrue(TEXT("Combat: rotation untouched"), Fight.Rotation.Equals(Explore.Rotation, 0.01f));

	// Far enemy -> Far scale (damped by Range Damping Time).
	Enemy->SetWorldLocation(FVector(3000, 0, 0));
	Run(Director.Get(), D, 4.f);
	TestTrue(TEXT("Far enemy -> Far Distance Scale"), FMath::IsNearlyEqual(-Director->GetLastResult().Pose.Location.X, 400.0 * P.FarDistanceScale, 0.5));

	// Intensity 0.5 and a runtime profile from gameplay.
	State.Intensity = 0.5f;
	State.bOverrideProfile = true;
	State.Profile = P;
	State.Profile.FieldOfViewOffset = 20.f;
	Director->SetCombatState(State);
	Run(Director.Get(), D, 4.f);
	TestTrue(TEXT("Intensity 0.5 x override profile FOV 20 -> +10"), FMath::IsNearlyEqual(Director->GetLastResult().Pose.FieldOfView, 100.f, 0.05f));

	// Exit: back to the exploration camera (slower Exit Time).
	State = FACFCameraCombatState();
	Director->SetCombatState(State);
	Run(Director.Get(), D, 0.3f);
	TestFalse(TEXT("Exiting is smooth (not there after 0.3 s)"), Director->GetLastResult().Pose.Location.Equals(Explore.Location, 1.0));
	Run(Director.Get(), D, 6.f);
	TestTrue(TEXT("Out of combat again: exploration camera"), Director->GetLastResult().Pose.Location.Equals(Explore.Location, 0.5)
		&& FMath::IsNearlyEqual(Director->GetLastResult().Pose.FieldOfView, 90.f, 0.05f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFCombatBlendEffectsTest, "ActionCameraFramework.Constraints.CombatBlendEffectsSuppressionFrameGuard", ACFTests::Flags)
bool FACFCombatBlendEffectsTest::RunTest(const FString& Parameters)
{
	using namespace ACFConstraintTests;
	TStrongObjectPtr<UACFCameraDirector> Director(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<UACFCameraDirector> Reference(NewObject<UACFCameraDirector>());
	TStrongObjectPtr<USceneComponent> Player(MakeTarget(FVector::ZeroVector));
	TStrongObjectPtr<USceneComponent> Enemy(MakeTarget(FVector(900, 300, 0)));
	UACFCameraNode_Combat* Combat = nullptr;
	TStrongObjectPtr<UACFCameraPreset> Orbit(MakeCombatOrbitPreset(&Combat));
	// Lock-On preset with its own Combat Camera node (lock-on combat).
	TStrongObjectPtr<UACFCameraPreset> Lock(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
	Lock->Nodes = { NewObject<UACFCameraNode_LockOn>(Lock.Get()), NewObject<UACFCameraNode_Combat>(Lock.Get()) };
	Lock->DefaultBlendIn = Linear(0.5f);
	Lock->DefaultBlendOut = Linear(0.5f);
	TStrongObjectPtr<UObject> Source(NewObject<UACFCameraPreset>());

	FACFCameraRequestParams Params;
	Params.FollowTarget.Component = Player.Get();
	Params.LookAtTarget.Component = Enemy.Get();
	Director->AddRequest(Orbit.Get(), Params);
	Reference->AddRequest(Orbit.Get(), Params);
	FFrameDriver D = Driver();
	FFrameDriver RefD = Driver();
	Run(Director.Get(), D, 0.5f);
	Run(Reference.Get(), RefD, 0.5f);

	// Combat starts while the lock-on blend runs: both rigs see it, no pop.
	FACFCameraCombatState State;
	State.bInCombat = true;
	Params.Priority = 10;
	Director->AddRequest(Lock.Get(), Params);
	Reference->AddRequest(Lock.Get(), Params);
	Director->SetCombatState(State);
	Reference->SetCombatState(State);
	double MaxStep = 0.0;
	double MaxFovStep = 0.0;
	FACFCameraPose Previous = Director->GetLastResult().Pose;
	bool bSame = true;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		const FACFCameraPose Now = D.Step(Director.Get(), Dt).Pose;
		const FACFCameraPose Again = D.Repeat(Director.Get(), Dt).Pose; // second PCM call in the same frame
		const FACFCameraPose Ref = RefD.Step(Reference.Get(), Dt).Pose;
		bSame &= Now.Location.Equals(Again.Location, 1e-4) && Now.Location.Equals(Ref.Location, 1e-3) && FMath::IsNearlyEqual(Now.FieldOfView, Ref.FieldOfView, 1e-3f);
		MaxStep = FMath::Max(MaxStep, FVector::Dist(Now.Location, Previous.Location));
		MaxFovStep = FMath::Max(MaxFovStep, static_cast<double>(FMath::Abs(Now.FieldOfView - Previous.FieldOfView)));
		Previous = Now;
	}
	AddInfo(FString::Printf(TEXT("Combat during the lock-on blend: max %.1f cm / %.2f deg per frame"), MaxStep, MaxFovStep));
	TestTrue(TEXT("Combat during a blend: no pop"), MaxStep < 40.0 && MaxFovStep < 1.0);
	TestTrue(TEXT("Double evaluation in a frame never double-steps combat damping"), bSame);
	const float CombatFov = Director->GetLastResult().Pose.FieldOfView;
	TestTrue(TEXT("Lock-on combat FOV offset applied"), FMath::IsNearlyEqual(CombatFov, 90.f + Combat->Profile.FieldOfViewOffset, 0.1f));

	// Effects on top of combat: FOV punch adds after the combat FOV.
	TStrongObjectPtr<UACFCameraEffectPreset> Punch(NewObject<UACFCameraEffectPreset>(GetTransientPackage(), NAME_None, RF_Transient));
	UACFCameraEffect_Punch* Layer = NewObject<UACFCameraEffect_Punch>(Punch.Get());
	Layer->FOV = 10.f;
	Layer->Envelope.Attack = 0.f;
	Layer->Envelope.Sustain = 1.f;
	Layer->Envelope.Release = 0.f;
	Punch->Layers.Add(Layer);
	Director->PlayEffect(Punch.Get(), FACFCameraEffectParams());
	Run(Director.Get(), D, 0.2f);
	TestTrue(TEXT("Effect FOV adds on top of the combat FOV"), FMath::IsNearlyEqual(Director->GetLastResult().Pose.FieldOfView, CombatFov + 10.f, 0.1f));
	Director->StopAllEffects(true);

	// Suppressed: combat ends meanwhile; the camera keeps evaluating and resumes already out of combat.
	Director->SuppressOutput(Source.Get(), 0.f);
	Director->SetCombatState(FACFCameraCombatState());
	Run(Director.Get(), D, 5.f);
	TestEqual(TEXT("Suppressed: weight 0"), Director->GetLastResult().Weight, 0.f);
	Director->ReleaseOutputSuppression(Source.Get(), 0.f);
	TestTrue(TEXT("Resumes with the combat profile already faded out"), FMath::IsNearlyEqual(D.Step(Director.Get(), Dt).Pose.FieldOfView, 90.f, 0.05f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
