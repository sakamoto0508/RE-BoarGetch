#include "Nodes/ACFCameraNode_Combat.h"
#include "Nodes/ACFCameraNodeUtils.h"

namespace ACFCombat
{
	static bool ResolveLocation(const FACFCameraTarget& Target, FVector& Out)
	{
		FTransform Transform;
		const UObject* Object = nullptr;
		if (Target.Resolve(Transform, Object))
		{
			Out = Transform.GetLocation();
			return true;
		}
		return false;
	}

	static float Damp(float Current, float Goal, float Time, float DeltaTime)
	{
		return Current + (Goal - Current) * ACFCameraMath::ExpAlpha(Time, DeltaTime);
	}
}

void UACFCameraNode_Combat::ResetInterpolation(FInstancedStruct* RuntimeState) const
{
	if (FACFCameraCombatNodeState* State = ACFNodes::GetState<FACFCameraCombatNodeState>(RuntimeState))
	{
		State->bInitialized = false;
	}
}

void UACFCameraNode_Combat::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraCombatNodeState* State = ACFNodes::GetState<FACFCameraCombatNodeState>(RuntimeState);
	if (!State)
	{
		return;
	}
	const FACFCameraCombatState& Combat = Context.Evaluation.Combat;
	const FACFCameraCombatProfile& P = Combat.bOverrideProfile ? Combat.Profile : Profile;
	const float Dt = Context.GetDeltaTime();

	// 1. Player-enemy distance (enemy: focus target > lock-on point / look-at target > nearest group member).
	FVector Player = Context.Scratch.Pivot;
	const bool bHasPlayer = ACFCombat::ResolveLocation(Context.Request.FollowTarget, Player) || Context.Scratch.bHasPivot;
	FVector Enemy = FVector::ZeroVector;
	bool bHasEnemy = ACFCombat::ResolveLocation(Combat.FocusTarget, Enemy);
	if (!bHasEnemy && Context.Scratch.bHasLockOnTarget)
	{
		Enemy = Context.Scratch.LockOnTargetLocation;
		bHasEnemy = true;
	}
	if (!bHasEnemy)
	{
		bHasEnemy = ACFCombat::ResolveLocation(Context.Request.LookAtTarget, Enemy);
	}
	if (!bHasEnemy && bHasPlayer)
	{
		double Best = UE_BIG_NUMBER;
		for (const FACFCameraTargetGroupMember& Member : Context.Request.TargetGroup.Members)
		{
			FVector Location;
			const double Dist = ACFCombat::ResolveLocation(Member.Target, Location) ? FVector::DistSquared(Location, Player) : UE_BIG_NUMBER;
			if (Member.Weight > 0.f && Dist > 1.0 && Dist < Best) // > 1: skip the player itself
			{
				Best = Dist;
				Enemy = Location;
				bHasEnemy = true;
			}
		}
	}
	const float Range = bHasEnemy && bHasPlayer ? static_cast<float>(FVector::Dist(Player, Enemy)) : P.FarRange;
	const float AlphaGoal = P.FarRange > P.NearRange ? FMath::Clamp((Range - P.NearRange) / (P.FarRange - P.NearRange), 0.f, 1.f) : 1.f;

	// 2. Goals, scaled by the combat weight.
	const float Weight = Combat.bInCombat ? FMath::Clamp(Combat.Intensity, 0.f, 1.f) : 0.f;
	const float ScaleGoalAtRange = FMath::Lerp(P.NearDistanceScale, P.FarDistanceScale, State->bInitialized ? State->RangeAlpha : AlphaGoal);

	// 3. Damping (snap on activation / teleport; the request blend smooths activation).
	if (!State->bInitialized)
	{
		State->RangeAlpha = AlphaGoal;
		State->DistanceScale = FMath::Lerp(1.f, ScaleGoalAtRange, Weight);
		State->FovOffset = P.FieldOfViewOffset * Weight;
		State->Shoulder = P.ShoulderOffset * Weight;
		State->bInitialized = true;
	}
	else if (Dt > 0.f)
	{
		State->RangeAlpha = ACFCombat::Damp(State->RangeAlpha, AlphaGoal, RangeDampingTime, Dt);
		const float Time = Combat.bInCombat ? EnterTime : ExitTime;
		const float ScaleGoal = FMath::Lerp(1.f, FMath::Lerp(P.NearDistanceScale, P.FarDistanceScale, State->RangeAlpha), Weight);
		State->DistanceScale = ACFCombat::Damp(State->DistanceScale, ScaleGoal, Time, Dt);
		State->FovOffset = ACFCombat::Damp(State->FovOffset, P.FieldOfViewOffset * Weight, Time, Dt);
		const float Alpha = ACFCameraMath::ExpAlpha(Time, Dt);
		State->Shoulder += (P.ShoulderOffset * Weight - State->Shoulder) * Alpha;
	}
	State->Range = Range;
	State->bHasEnemy = bHasEnemy;

	// 4. Apply: arm around the pivot (keeps the pivot at its screen position), shoulder, FOV. Rotation untouched.
	if (Context.Scratch.bHasPivot)
	{
		InOutPose.Location = Context.Scratch.Pivot + (InOutPose.Location - Context.Scratch.Pivot) * State->DistanceScale;
	}
	InOutPose.Location += InOutPose.Rotation.RotateVector(State->Shoulder);
	InOutPose.FieldOfView = FMath::Clamp(InOutPose.FieldOfView + State->FovOffset, 1.f, 170.f);

#if !UE_BUILD_SHIPPING
	FACFCameraRigDebug& Debug = Context.Scratch.Debug;
	Debug.bHasCombat = true;
	Debug.bInCombat = Combat.bInCombat;
	Debug.CombatWeight = Weight;
	Debug.CombatDistanceScale = State->DistanceScale;
	Debug.CombatFovOffset = State->FovOffset;
	Debug.CombatRange = bHasEnemy ? Range : -1.f;
#endif
}
