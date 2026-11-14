#include "ACFCameraConstraintSolver.h"
#include "Nodes/ACFCameraFraming.h"
#include "Nodes/ACFCameraNode_GroupFraming.h"
#include "Nodes/ACFCameraTargetTracking.h"
#include "ACFCameraProfiler.h"

namespace ACFSolver
{
	/** Subjects checked this evaluation: player(s) first, then by weight (stable), at most Max. */
	struct FChecked
	{
		int32 Index[FACFCameraDeoccluderState::MaxSubjects];
		int32 Num = 0;
	};

	static void SelectSubjects(const FACFCameraFramingSubjects& Subjects, int32 Max, FChecked& Out)
	{
		Max = FMath::Clamp(Max, 1, FACFCameraDeoccluderState::MaxSubjects);
		// Insertion by (player, weight): small N, no allocation.
		for (int32 I = 0; I < Subjects.Num(); ++I)
		{
			auto Before = [&Subjects](int32 A, int32 B)
			{
				if (Subjects[A].bIsPlayer != Subjects[B].bIsPlayer)
				{
					return Subjects[A].bIsPlayer;
				}
				return Subjects[A].Weight > Subjects[B].Weight;
			};
			int32 Pos = Out.Num;
			while (Pos > 0 && Before(I, Out.Index[Pos - 1]))
			{
				--Pos;
			}
			if (Pos >= Max)
			{
				continue;
			}
			const int32 Last = FMath::Min(Out.Num, Max - 1);
			for (int32 K = Last; K > Pos; --K)
			{
				Out.Index[K] = Out.Index[K - 1];
			}
			Out.Index[Pos] = I;
			Out.Num = FMath::Min(Out.Num + 1, Max);
		}
	}

	/** B-3: the object key (index + serial number) when the subject has one: address reuse after GC cannot match. */
	static void SetKey(FACFCameraDeoccluderState::FSubjectVisibility& Visibility, const FACFCameraFramingSubject& Subject, int32 Slot)
	{
		const bool bHasKey = Subject.ActorKey != FObjectKey();
		Visibility.Object = Subject.ActorKey;
		Visibility.Address = bHasKey ? nullptr : static_cast<const void*>(Subject.Actor); // identity only, never dereferenced
		Visibility.Slot = (bHasKey || Subject.Actor) ? INDEX_NONE : Slot;
	}

	static float DampAxis(float Current, float Goal, float AwayTime, float ReturnTime, float DeltaTime)
	{
		const float Time = FMath::Abs(Goal) > FMath::Abs(Current) ? AwayTime : ReturnTime;
		return Current + (Goal - Current) * ACFCameraMath::ExpAlpha(Time, DeltaTime);
	}
}

FACFCameraPose FACFCameraConstraintSolver::ApplyOrbitCorrection(const FACFCameraPose& Pose, const FVector& Pivot, const FVector& Correction)
{
	if (Correction.IsNearlyZero(1e-4))
	{
		return Pose;
	}
	const FVector Arm = Pose.Location - Pivot;
	if (Arm.IsNearlyZero(1e-3))
	{
		return Pose;
	}
	FQuat Rotation = FQuat::Identity;
	const FVector PitchAxis = FVector::CrossProduct(Arm, FVector::UpVector); // positive angle raises the camera
	if (!FMath::IsNearlyZero(Correction.Y) && !PitchAxis.IsNearlyZero(1e-3))
	{
		Rotation = FQuat(PitchAxis.GetUnsafeNormal(), FMath::DegreesToRadians(Correction.Y));
	}
	if (!FMath::IsNearlyZero(Correction.X))
	{
		Rotation = FQuat(FVector::UpVector, FMath::DegreesToRadians(Correction.X)) * Rotation;
	}
	FACFCameraPose Out = Pose;
	Out.Location = Pivot + Rotation.RotateVector(Arm) * (1.0 - FMath::Clamp(static_cast<double>(Correction.Z), 0.0, 0.95));
	// The view turns with the arm (rigid orbit), so the pivot keeps its place on screen.
	Out.Rotation = (Rotation * Pose.Rotation.Quaternion()).Rotator();
	Out.Rotation.Roll = Pose.Rotation.Roll;
	return Out;
}

void FACFCameraConstraintSolver::Deocclude(const FACFCameraDeoccluderSettings& Settings, const FVector& Pivot, const FACFCameraFramingSubjects& Subjects,
	float DeltaTime, FACFCameraDeoccluderState& State, const FACFCameraSolverQueries& Queries, FACFCameraPose& InOutPose, FACFCameraSolverReport& Report)
{
	using namespace ACFSolver;
	ACF_PROFILE_SCOPE(Deocclude);
	ACF_PROFILE_PURPOSE(DeoccluderCheck);
	FChecked Checked;
	SelectSubjects(Subjects, Settings.MaxCheckedSubjects, Checked);
	Report.NumCheckedSubjects = Checked.Num;
	const FACFCameraPose Input = InOutPose;

	// Visibility from the collided camera of a correction. Bit k = checked subject k blocked.
	auto Evaluate = [&](const FVector& Correction) -> uint32
	{
		const FACFCameraPose Candidate = ApplyOrbitCorrection(Input, Pivot, Correction);
		const FVector Camera = Queries.Collide(Pivot, Candidate.Location);
		++Report.NumTraces;
		uint32 Mask = 0;
		for (int32 K = 0; K < Checked.Num; ++K)
		{
			const FACFCameraFramingSubject& Subject = Subjects[Checked.Index[K]];
			const bool bBlocked = Queries.IsBlocked(Camera, Subject);
			++Report.NumTraces;
			Mask |= bBlocked ? (1u << K) : 0u;
#if !UE_BUILD_SHIPPING
			if (Report.NumDebugLines < FACFCameraSolverReport::MaxDebugLines && Correction.Equals(State.Goal))
			{
				Report.DebugFrom[Report.NumDebugLines] = Camera;
				Report.DebugTo[Report.NumDebugLines] = Subject.Location;
				Report.DebugBlocked[Report.NumDebugLines] = bBlocked;
				++Report.NumDebugLines;
			}
#endif
		}
		return Mask;
	};
	auto Score = [&](uint32 Mask, bool& bOutPlayerVisible) -> double
	{
		double Value = 0.0;
		bOutPlayerVisible = true;
		for (int32 K = 0; K < Checked.Num; ++K)
		{
			const FACFCameraFramingSubject& Subject = Subjects[Checked.Index[K]];
			const bool bVisible = (Mask & (1u << K)) == 0;
			if (Subject.bIsPlayer)
			{
				bOutPlayerVisible &= bVisible;
				Value += bVisible ? 1000.0 : 0.0;
			}
			else
			{
				Value += bVisible ? FMath::Max(Subject.Weight, 0.01f) : 0.0;
			}
		}
		return Value;
	};

	// 1. Visibility at the goal correction (decisions use the goal, not the camera gliding towards it).
	const uint32 GoalMask = Checked.Num > 0 ? Evaluate(State.Goal) : 0u;

	// 2. Hysteresis per subject (state matched by identity; destroyed subjects simply disappear).
	FACFCameraDeoccluderState::FSubjectVisibility Next[FACFCameraDeoccluderState::MaxSubjects];
	bool bConfirmed = false;
	for (int32 K = 0; K < Checked.Num; ++K)
	{
		const FACFCameraFramingSubject& Subject = Subjects[Checked.Index[K]];
		FACFCameraDeoccluderState::FSubjectVisibility Visibility;
		SetKey(Visibility, Subject, Checked.Index[K]);
		for (int32 P = 0; P < State.NumSubjects; ++P)
		{
			if (State.Subjects[P].SameSubject(Visibility))
			{
				Visibility = State.Subjects[P];
				break;
			}
		}
		if (GoalMask & (1u << K))
		{
			if (DeltaTime > 0.f)
			{
				Visibility.BlockedTime += DeltaTime;
			}
			Visibility.bOccluded |= Visibility.BlockedTime >= Settings.OccludedDelay;
		}
		else
		{
			Visibility.BlockedTime = 0.f;
			Visibility.bOccluded = false;
		}
		bConfirmed |= Visibility.bOccluded;
		Next[K] = Visibility;
	}
	for (int32 K = 0; K < Checked.Num; ++K)
	{
		State.Subjects[K] = Next[K];
	}
	State.NumSubjects = Checked.Num;
	if (DeltaTime > 0.f)
	{
		State.TimeSinceSearch += DeltaTime;
	}

	// 3. Search (only for a confirmed occlusion, throttled while unresolved). Ordered by camera movement.
	if (bConfirmed && DeltaTime > 0.f && State.TimeSinceSearch >= Settings.RetryInterval)
	{
		Report.bSearched = true;
		State.TimeSinceSearch = 0.f;
		ACF_PROFILE_PURPOSE(DeoccluderSearch);
		const float H = Settings.MaxYawOffset;
		const float P = Settings.MaxPitchOffset;
		const float L = Settings.MaxPullIn;
		const float Side = State.Goal.X < 0.f ? -1.f : 1.f; // keep turning the way we already turned (stability)
		const FVector Candidates[] = {
			FVector(Side * H * 0.5f, 0, 0), FVector(-Side * H * 0.5f, 0, 0), FVector(0, P * 0.5f, 0), FVector(0, 0, L * 0.5f),
			FVector(Side * H * 0.5f, P * 0.5f, 0), FVector(-Side * H * 0.5f, P * 0.5f, 0),
			FVector(Side * H, 0, 0), FVector(-Side * H, 0, 0), FVector(0, P, 0), FVector(0, 0, L),
		};
		bool bGoalPlayerVisible = true;
		double BestScore = Score(GoalMask, bGoalPlayerVisible);
		FVector Best = State.Goal;
		uint32 BestMask = GoalMask;
		for (const FVector& Candidate : Candidates)
		{
			if (Candidate.IsNearlyZero() || Candidate.Equals(State.Goal))
			{
				continue; // limit 0 on that axis, or already evaluated
			}
			const uint32 Mask = Evaluate(Candidate);
			bool bPlayerVisible = true;
			const double Value = Score(Mask, bPlayerVisible);
			if (Value > BestScore + 1e-6)
			{
				BestScore = Value;
				Best = Candidate;
				BestMask = Mask;
			}
			if (Mask == 0)
			{
				break; // everyone visible: the cheapest full solution
			}
		}
		State.Goal = Best;
		State.Unresolved = 0;
		State.bPlayerUnresolved = false;
		for (int32 K = 0; K < Checked.Num; ++K)
		{
			if (BestMask & (1u << K))
			{
				++State.Unresolved;
				State.bPlayerUnresolved |= Subjects[Checked.Index[K]].bIsPlayer;
			}
		}
	}
	else if (!bConfirmed)
	{
		State.Unresolved = 0;
		State.bPlayerUnresolved = false;
	}

	// 4. Release: once the uncorrected camera has been clear for ClearDelay, ease back to it.
	if (!bConfirmed && !State.Goal.IsNearlyZero())
	{
		const uint32 BaseMask = Evaluate(FVector::ZeroVector);
		State.BaseClearTime = BaseMask == 0 ? State.BaseClearTime + FMath::Max(DeltaTime, 0.f) : 0.f;
		if (State.BaseClearTime >= Settings.ClearDelay)
		{
			State.Goal = FVector::ZeroVector;
			State.BaseClearTime = 0.f;
		}
	}
	else
	{
		State.BaseClearTime = 0.f;
	}

	// 5. Damped correction (towards a correction: CorrectionDampingTime, back to none: ReturnDampingTime).
	if (DeltaTime > 0.f)
	{
		State.Current.X = DampAxis(State.Current.X, State.Goal.X, Settings.CorrectionDampingTime, Settings.ReturnDampingTime, DeltaTime);
		State.Current.Y = DampAxis(State.Current.Y, State.Goal.Y, Settings.CorrectionDampingTime, Settings.ReturnDampingTime, DeltaTime);
		State.Current.Z = DampAxis(State.Current.Z, State.Goal.Z, Settings.CorrectionDampingTime, Settings.ReturnDampingTime, DeltaTime);
	}
	InOutPose = ApplyOrbitCorrection(Input, Pivot, State.Current);
	Report.Correction = State.Current;
	Report.NumOccluded = State.Unresolved;
	Report.bPlayerOccluded = State.bPlayerUnresolved;
}

void FACFCameraConstraintSolver::RecoverFraming(const FACFCameraFramingRecoverySettings& Settings, const FACFCameraFramingSubjects& Subjects, const FVector2D& FramingSize,
	float AspectRatio, float DeltaTime, FACFCameraFramingRecoveryState& State, FACFCameraPose& InOutPose, FACFCameraSolverReport& Report, float HorizontalFovScale)
{
	ACF_PROFILE_SCOPE(FramingRecovery);
	const float InFov = InOutPose.FieldOfView;
	double GoalFov = InFov;
	FVector2D GoalRotation = FVector2D::ZeroVector;
	if (Settings.bEnabled && !Subjects.IsEmpty())
	{
		const double Cap = FMath::Max(static_cast<double>(InFov), FMath::Min(static_cast<double>(Settings.MaxFieldOfView), static_cast<double>(InFov + Settings.MaxFieldOfViewIncrease)));
		const double Required = UACFCameraNode_GroupFraming::ComputeRequiredFov(Subjects, InOutPose.Location, InOutPose.Rotation, AspectRatio, FramingSize, 0.f, 170.f, HorizontalFovScale);
		if (Required > InFov + 0.05)
		{
			GoalFov = FMath::Min(Required, Cap);
			Report.bFramingLimited = Required > Cap + 0.05;
		}

		// Last resort for the player (priority 2): if it is off the framing area even at the goal FOV, turn towards it.
		if (Settings.bKeepPlayerOnScreen)
		{
			const ACFFraming::FLens Lens = ACFFraming::MakeLens(static_cast<float>(GoalFov), AspectRatio, HorizontalFovScale);
			const FVector2D Half(FMath::Clamp(FramingSize.X, 0.05, 1.0) * 0.5, FMath::Clamp(FramingSize.Y, 0.05, 1.0) * 0.5);
			for (const FACFCameraFramingSubject& Subject : Subjects)
			{
				if (!Subject.bIsPlayer || FVector::Dist(Subject.Location, InOutPose.Location) <= Subject.Radius)
				{
					continue; // not the player, or the camera is inside the player's sphere (it fills the view)
				}
				FVector2D Screen;
				double Depth = 0.0;
				const bool bInFront = ACFFraming::Project(InOutPose.Location, InOutPose.Rotation, Lens, Subject.Location, Screen, Depth);
				if (bInFront && FMath::Abs(Screen.X) <= Half.X && FMath::Abs(Screen.Y) <= Half.Y)
				{
					break;
				}
				const FVector2D Desired = bInFront
					? FVector2D(FMath::Clamp(Screen.X, -Half.X * 0.9, Half.X * 0.9), FMath::Clamp(Screen.Y, -Half.Y * 0.9, Half.Y * 0.9))
					: FVector2D(0.0, -Half.Y * 0.9);
				const FRotator Aimed = ACFFraming::AimAt(InOutPose.Rotation, InOutPose.Location, Lens, Subject.Location, Desired);
				const float Max = Settings.MaxPlayerRotation;
				GoalRotation.X = FMath::Clamp(FRotator::NormalizeAxis(Aimed.Yaw - InOutPose.Rotation.Yaw), -Max, Max);
				GoalRotation.Y = FMath::Clamp(Aimed.Pitch - InOutPose.Rotation.Pitch, -Max, Max);
				Report.bPlayerRotationCorrected = true;
				break;
			}
		}
	}

	const float GoalDelta = static_cast<float>(GoalFov - InFov);
	if (!State.bInitialized)
	{
		State.FovDelta = GoalDelta;
		State.Rotation = GoalRotation;
		State.bInitialized = true;
	}
	else if (DeltaTime > 0.f)
	{
		State.FovDelta = ACFSolver::DampAxis(State.FovDelta, GoalDelta, Settings.WidenDampingTime, Settings.NarrowDampingTime, DeltaTime);
		State.Rotation.X = ACFSolver::DampAxis(State.Rotation.X, GoalRotation.X, Settings.WidenDampingTime, Settings.NarrowDampingTime, DeltaTime);
		State.Rotation.Y = ACFSolver::DampAxis(State.Rotation.Y, GoalRotation.Y, Settings.WidenDampingTime, Settings.NarrowDampingTime, DeltaTime);
	}
	InOutPose.FieldOfView = FMath::Clamp(InFov + State.FovDelta, 1.f, 170.f);
	InOutPose.Rotation.Yaw += State.Rotation.X;
	InOutPose.Rotation.Pitch = FMath::Clamp(InOutPose.Rotation.Pitch + State.Rotation.Y, -89.f, 89.f);
	Report.FovRecovery = State.FovDelta;
}
