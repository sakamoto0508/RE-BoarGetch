#include "Nodes/ACFCameraTargetTracking.h"
#include "ACFCameraHealth.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

namespace ACFTracking
{
	static const AActor* ActorOf(const UObject* Object)
	{
		if (const AActor* Actor = Cast<AActor>(Object))
		{
			return Actor;
		}
		const USceneComponent* Component = Cast<USceneComponent>(Object);
		return Component ? Component->GetOwner() : nullptr;
	}
}

bool FACFCameraTargetTrackingState::ResolveTarget(const FACFCameraTarget& Target, EACFCameraTargetLostBehavior LostBehavior, FTransform& OutTransform, const UObject*& OutObject)
{
	OutObject = nullptr;
	if (Target.Resolve(OutTransform, OutObject))
	{
		if (LastTargetObject.Get() != OutObject)
		{
			// New or switched target: do not drag the camera across the level through damping.
			LastTargetObject = OutObject;
			ResetInterpolation();
		}
		LastKnownTargetTransform = OutTransform;
		bHasLastKnownTarget = true;
		return true;
	}

	// Phase 6B / B-4: the target still exists but its transform is invalid this frame (Resolve rejected it): it is not
	// lost, so keep the last valid transform whatever the lost behavior (nothing invalid reaches the damping state).
	if (OutObject && OutObject == LastTargetObject.Get() && bHasLastKnownTarget)
	{
		OutTransform = LastKnownTargetTransform;
		return true;
	}
	if (Target.IsSet() && LostBehavior == EACFCameraTargetLostBehavior::HoldLastKnown && bHasLastKnownTarget)
	{
		OutTransform = LastKnownTargetTransform;
		return true;
	}
	return false;
}

bool FACFCameraTargetTrackingState::Track(const FACFCameraTarget& Target, const FACFCameraTargetMotionSettings& Settings, float DeltaTime, FACFCameraTrackedTarget& Out)
{
	const UObject* Object = nullptr;
	if (!ResolveTarget(Target, Settings.LostBehavior, Out.Transform, Object))
	{
		return false;
	}
	Out.Transform.SetScale3D(FVector::OneVector); // a scaled character must not scale offsets
	Out.Actor = ACFTracking::ActorOf(Object);
	UpdateMotion(Settings, DeltaTime, true, Out);
	return true;
}

bool FACFCameraTargetTrackingState::TrackPoint(bool bValid, const FTransform& Transform, const FACFCameraTargetMotionSettings& Settings, float DeltaTime, bool bDetectTeleport, FACFCameraTrackedTarget& Out)
{
	if (bValid && !ACFCameraValidation::IsValidTransform(Transform))
	{
		// A computed point (group center, lock-on point) built from invalid data: hold the last valid one (B-4).
		FACFCameraHealth::ReportActive(EACFCameraHealthIssue::InvalidTarget, NAME_None, TEXT("tracked point"), TEXT("last valid point held"));
		if (bHasLastKnownTarget)
		{
			Out.Transform = LastKnownTargetTransform;
			Out.Transform.SetScale3D(FVector::OneVector);
			Out.Actor = nullptr;
			UpdateMotion(Settings, DeltaTime, bDetectTeleport, Out);
			return true;
		}
		return false;
	}
	if (bValid)
	{
		LastKnownTargetTransform = Transform;
		bHasLastKnownTarget = true;
		Out.Transform = Transform;
	}
	else if (bHasLastKnownTarget && Settings.LostBehavior == EACFCameraTargetLostBehavior::HoldLastKnown)
	{
		Out.Transform = LastKnownTargetTransform;
	}
	else
	{
		return false;
	}
	Out.Transform.SetScale3D(FVector::OneVector);
	Out.Actor = nullptr;
	UpdateMotion(Settings, DeltaTime, bDetectTeleport, Out);
	return true;
}

void FACFCameraTargetTrackingState::UpdateMotion(const FACFCameraTargetMotionSettings& Settings, float DeltaTime, bool bDetectTeleport, FACFCameraTrackedTarget& Out)
{
	Out.Location = Out.Transform.GetLocation();
	Out.bTeleported = false;

	// Teleport: a jump larger than the threshold drops all interpolation history (snap).
	if (bDetectTeleport && bMotionInitialized && Settings.TeleportDistance > 0.f
		&& FVector::DistSquared(Out.Location, PreviousTargetLocation) > FMath::Square(Settings.TeleportDistance))
	{
		ResetInterpolation();
		Out.bTeleported = true;
	}

	const FACFCameraLookaheadSettings& Lookahead = Settings.Lookahead;
	if (!bMotionInitialized)
	{
		PreviousTargetLocation = Out.Location;
		SmoothedVelocity = FVector::ZeroVector;
		bMotionInitialized = true;
	}
	else if (DeltaTime > 0.f)
	{
		FVector Velocity = (Out.Location - PreviousTargetLocation) / DeltaTime;
		PreviousTargetLocation = Out.Location;
		if (Lookahead.bIgnoreVertical)
		{
			Velocity.Z = 0.0;
		}
		// Moving against the prediction (sharp turn / reversal): forget the old direction faster.
		const bool bAgainstPrediction = (Velocity | SmoothedVelocity) < 0.0;
		const float Smoothing = Lookahead.Smoothing * (bAgainstPrediction ? Lookahead.DirectionChangeSmoothingScale : 1.f);
		SmoothedVelocity += (Velocity - SmoothedVelocity) * ACFCameraMath::ExpAlpha(Smoothing, DeltaTime);
	}

	Out.Predicted = Out.Location;
	if (Lookahead.Time > 0.f)
	{
		FVector Offset = SmoothedVelocity * Lookahead.Time;
		if (Lookahead.MaxDistance > 0.f)
		{
			Offset = Offset.GetClampedToMaxSize(Lookahead.MaxDistance);
		}
		Out.Predicted += Offset;
	}
}

FVector FACFCameraTargetTrackingState::UpdateDamped(const FVector& Goal, float DampingTime, float DeltaTime)
{
	return UpdateDamped(Goal, FVector(DampingTime), FQuat::Identity, DeltaTime);
}

FVector FACFCameraTargetTrackingState::UpdateDamped(const FVector& Goal, const FVector& AxisTimes, const FQuat& Frame, float DeltaTime)
{
	const bool bRigid = AxisTimes.GetMax() <= UE_KINDA_SMALL_NUMBER;
	if (!bDampingInitialized || bRigid)
	{
		DampedPoint = Goal;
		PreviousGoal = Goal;
		bDampingInitialized = true;
		return DampedPoint;
	}
	if (DeltaTime <= 0.f)
	{
		return DampedPoint; // zero-time evaluation: report, never advance
	}

	// Work relative to the previous goal, in the damping frame's axes.
	const FVector Current = Frame.UnrotateVector(DampedPoint - PreviousGoal);
	const FVector Move = Frame.UnrotateVector(Goal - PreviousGoal);
	FVector Result;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		Result[Axis] = ACFCameraMath::DampTowardsMovingGoal1D(Current[Axis], 0.0, Move[Axis], AxisTimes[Axis], DeltaTime);
	}
	DampedPoint = PreviousGoal + Frame.RotateVector(Result);
	PreviousGoal = Goal;
	return DampedPoint;
}

FQuat FACFCameraTargetTrackingState::DampingFrame(const FACFCameraPositionDamping& Damping, const FQuat& TargetRotation, const FRotator& CameraRotation)
{
	switch (Damping.Space)
	{
	case EACFCameraDampingSpace::TargetLocal:
		return TargetRotation;
	case EACFCameraDampingSpace::CameraYaw:
		return FRotator(0.f, CameraRotation.Yaw, 0.f).Quaternion();
	default:
		return FQuat::Identity;
	}
}

double ACFCameraMath::DampTowardsMovingGoal1D(double Current, double PreviousGoal, double Goal, float DampingTime, float DeltaTime)
{
	if (DampingTime <= UE_KINDA_SMALL_NUMBER)
	{
		return Goal;
	}
	if (DeltaTime <= 0.f)
	{
		return Current;
	}
	// goal(t) = PreviousGoal + V * t over [0, dt]. Solution of x' = (goal - x) / T:
	//   x(dt) = Goal - V*T + (Current - PreviousGoal + V*T) * exp(-dt/T)
	const double Velocity = (Goal - PreviousGoal) / DeltaTime;
	const double Lag = Velocity * DampingTime;
	const double Decay = FMath::Exp(-static_cast<double>(DeltaTime) / DampingTime);
	return Goal - Lag + (Current - PreviousGoal + Lag) * Decay;
}

FVector ACFCameraMath::DampTowardsMovingGoal(const FVector& Current, const FVector& PreviousGoal, const FVector& Goal, float DampingTime, float DeltaTime)
{
	return FVector(
		DampTowardsMovingGoal1D(Current.X, PreviousGoal.X, Goal.X, DampingTime, DeltaTime),
		DampTowardsMovingGoal1D(Current.Y, PreviousGoal.Y, Goal.Y, DampingTime, DeltaTime),
		DampTowardsMovingGoal1D(Current.Z, PreviousGoal.Z, Goal.Z, DampingTime, DeltaTime));
}

float ACFCameraMath::ExpAlpha(float DampingTime, float DeltaTime)
{
	if (DeltaTime <= 0.f)
	{
		return 0.f;
	}
	if (DampingTime <= UE_KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}
	return 1.f - FMath::Exp(-DeltaTime / DampingTime);
}
