#include "Nodes/ACFCameraTargetTracking.h"

bool FACFCameraTargetTrackingState::ResolveTarget(const FACFCameraTarget& Target, EACFCameraTargetLostBehavior LostBehavior, FTransform& OutTransform)
{
	const UObject* TargetObject = nullptr;
	if (Target.Resolve(OutTransform, TargetObject))
	{
		if (LastTargetObject.Get() != TargetObject)
		{
			// New or switched target: do not drag the camera across the level through damping.
			LastTargetObject = TargetObject;
			ResetDamping();
		}
		LastKnownTargetTransform = OutTransform;
		bHasLastKnownTarget = true;
		return true;
	}

	if (Target.IsSet() && LostBehavior == EACFCameraTargetLostBehavior::HoldLastKnown && bHasLastKnownTarget)
	{
		OutTransform = LastKnownTargetTransform;
		return true;
	}
	return false;
}

FVector FACFCameraTargetTrackingState::UpdateDamped(const FVector& Goal, float DampingTime, float DeltaTime)
{
	if (!bDampingInitialized)
	{
		DampedPoint = Goal;
		PreviousGoal = Goal;
		bDampingInitialized = true;
		return DampedPoint;
	}
	if (DeltaTime <= 0.f)
	{
		// Zero-time evaluation (e.g. PlayerCameraManager::InitializeFor): report, never advance.
		return DampingTime > 0.f ? DampedPoint : Goal;
	}

	DampedPoint = ACFCameraMath::DampTowardsMovingGoal(DampedPoint, PreviousGoal, Goal, DampingTime, DeltaTime);
	PreviousGoal = Goal;
	return DampedPoint;
}

FVector ACFCameraMath::DampTowardsMovingGoal(const FVector& Current, const FVector& PreviousGoal, const FVector& Goal, float DampingTime, float DeltaTime)
{
	if (DampingTime <= UE_KINDA_SMALL_NUMBER || DeltaTime <= 0.f)
	{
		return DampingTime <= UE_KINDA_SMALL_NUMBER ? Goal : Current;
	}

	// goal(t) = PreviousGoal + V * t over [0, dt]. Solution of x' = (goal - x) / T:
	//   x(dt) = Goal - V*T + (Current - PreviousGoal + V*T) * exp(-dt/T)
	const FVector Velocity = (Goal - PreviousGoal) / DeltaTime;
	const FVector Lag = Velocity * DampingTime;
	const double Decay = FMath::Exp(-static_cast<double>(DeltaTime) / DampingTime);
	return Goal - Lag + (Current - PreviousGoal + Lag) * Decay;
}
