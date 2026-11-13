#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNodeUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ACFCameraProfiler.h"

void UACFCameraNode_Collision::ResetInterpolation(FInstancedStruct* RuntimeState) const
{
	if (FACFCameraCollisionState* State = ACFNodes::GetState<FACFCameraCollisionState>(RuntimeState))
	{
		State->bInitialized = false;
		State->Deoccluder.Reset();
		State->Recovery.Reset();
	}
}

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarACFSolver(TEXT("ACF.Debug.Solver"), 1,
	TEXT("Constraint solver (Deoccluder + Framing Recovery of Collision nodes). 0 = bypassed (comparison), 1 = as configured."), ECVF_Cheat);
#endif

bool ACFCameraCollision::IsSolverEnabled()
{
#if !UE_BUILD_SHIPPING
	return CVarACFSolver.GetValueOnGameThread() != 0;
#else
	return true;
#endif
}

void UACFCameraNode_Collision::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraCollisionState* State = ACFNodes::GetState<FACFCameraCollisionState>(RuntimeState);
	UWorld* World = Context.Evaluation.World;
	if (!State || !World)
	{
		return;
	}

	// Sweep origin: the rig's pivot, or the follow target as a fallback.
	FVector Origin;
	const AActor* PivotActor = Context.Scratch.PivotActor;
	if (Context.Scratch.bHasPivot)
	{
		Origin = Context.Scratch.Pivot;
	}
	else
	{
		FTransform TargetTransform;
		const UObject* TargetObject = nullptr;
		if (!Context.Request.FollowTarget.Resolve(TargetTransform, TargetObject))
		{
			return;
		}
		Origin = TargetTransform.GetLocation() + FallbackOriginOffset;
		PivotActor = Cast<AActor>(TargetObject);
		if (!PivotActor)
		{
			if (const UActorComponent* Component = Cast<UActorComponent>(TargetObject))
			{
				PivotActor = Component->GetOwner();
			}
		}
	}

	// Ignore list (also published as the hard constraint the final output guard re-applies).
	FACFCameraCollisionConstraint& Constraint = Context.Scratch.Constraint;
	Constraint.IgnoredActors.Reset();
	if (bIgnorePivotActor && PivotActor)
	{
		Constraint.IgnoredActors.Add(PivotActor);
	}
	if (bIgnoreLookAtTargetActor)
	{
		if (const AActor* LookActor = Context.Request.LookAtTarget.Actor.Get())
		{
			Constraint.IgnoredActors.Add(LookActor);
		}
	}
	if (bIgnoreControlledPawn && Context.Evaluation.PlayerController)
	{
		if (const APawn* Pawn = Context.Evaluation.PlayerController->GetPawn())
		{
			Constraint.IgnoredActors.Add(Pawn);
		}
	}
	// B-3: requests held by the director carry weak references only (IgnoredActorRefs); parameters used directly
	// (probes) may still carry the input list.
	for (const TWeakObjectPtr<AActor>& Ignored : Context.Request.IgnoredActorRefs)
	{
		if (const AActor* Actor = Ignored.Get(); IsValid(Actor)) // destroyed / garbage / GC'd actors resolve to null
		{
			Constraint.IgnoredActors.Add(Actor);
		}
	}
	for (const TObjectPtr<AActor>& Ignored : Context.Request.CollisionIgnoredActors)
	{
		if (const AActor* Actor = Ignored.Get(); IsValid(Actor))
		{
			Constraint.IgnoredActors.Add(Actor);
		}
	}
	Constraint.bValid = true;
	Constraint.Pivot = Origin;
	Constraint.ProbeRadius = ProbeRadius;
	Constraint.Channel = ProbeChannel;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ACFCameraCollision), false);
	for (const AActor* Actor : Constraint.IgnoredActors)
	{
		Params.AddIgnoredActor(Actor);
	}
	const float Dt = Context.GetDeltaTime();
	FACFCameraSolverReport& Report = Context.Scratch.SolverReport;
	Report = FACFCameraSolverReport();
	const bool bSolverAllowed = ACFCameraCollision::IsSolverEnabled();

	// Constraint solver step 1 (priority 2 / 3): deocclusion moves the DESIRED camera; the sweep below still wins.
	if (Deoccluder.bEnabled && Context.Request.bEnableDeoccluder && bSolverAllowed)
	{
		FACFCameraFramingSubjects DefaultSubjects;
		const FACFCameraFramingSubjects* Subjects = &Context.Scratch.FramingSubjects;
		if (!Context.Scratch.bHasFramingSubjects)
		{
			// No Group Framing: the player pivot and (optionally) the look-at / lock-on target.
			FACFCameraFramingSubject& Player = DefaultSubjects.AddDefaulted_GetRef();
			Player.Location = Origin;
			Player.Weight = 1.f;
			Player.SetActor(PivotActor);
			Player.bIsPlayer = true;
			FTransform LookTransform;
			const UObject* LookObject = nullptr;
			const bool bLookAlive = Context.Request.LookAtTarget.Resolve(LookTransform, LookObject);
			if (Deoccluder.bCheckLookAtTarget && (Context.Scratch.bHasLockOnTarget || bLookAlive))
			{
				FACFCameraFramingSubject& Look = DefaultSubjects.AddDefaulted_GetRef();
				Look.Location = Context.Scratch.bHasLockOnTarget ? Context.Scratch.LockOnTargetLocation : LookTransform.GetLocation();
				Look.Weight = 1.f;
				Look.SetActor(bLookAlive ? ACFCameraCollision::ActorOf(LookObject) : nullptr);
			}
			Subjects = &DefaultSubjects;
		}
		const float LosRadius = Deoccluder.TraceRadius;
		const ECollisionChannel Channel = ProbeChannel;
		const float Probe = ProbeRadius;
		const float MinDist = MinDistance;
		auto Collide = [World, &Params, Channel, Probe, MinDist](const FVector& Pivot, const FVector& Want) -> FVector
		{
			const FVector Arm = Want - Pivot;
			const double Full = Arm.Size();
			if (Full < UE_KINDA_SMALL_NUMBER)
			{
				return Want;
			}
			FCollisionQueryParams SweepParams = Params; // SweepCamera may add an ignored component
			FHitResult SweepHit;
			const bool bBlocked = ACFCameraCollision::SweepCamera(*World, Pivot, Want, Probe, Channel, SweepParams, SweepHit);
			const double Allowed = bBlocked ? FMath::Max(FMath::Clamp(static_cast<double>(SweepHit.Distance), 0.0, Full), FMath::Min(static_cast<double>(MinDist), Full)) : Full;
			return Pivot + Arm / Full * Allowed;
		};
		auto IsBlocked = [World, &Params, Channel, LosRadius](const FVector& From, const FACFCameraFramingSubject& Subject) -> bool
		{
			FHitResult LosHit;
			if (LosRadius > 0.f)
			{
				ACF_PROFILE_QUERY(Sweep);
			}
			else
			{
				ACF_PROFILE_QUERY(Line);
			}
			const bool bHit = LosRadius > 0.f
				? World->SweepSingleByChannel(LosHit, From, Subject.Location, FQuat::Identity, Channel, FCollisionShape::MakeSphere(LosRadius), Params)
				: World->LineTraceSingleByChannel(LosHit, From, Subject.Location, Channel, Params);
			// Reaching the subject's own collision first means it is visible.
			return bHit && (!Subject.Actor || LosHit.GetActor() != Subject.Actor);
		};
		const FACFCameraSolverQueries Queries{ Collide, IsBlocked };
		FACFCameraConstraintSolver::Deocclude(Deoccluder, Origin, *Subjects, Dt, State->Deoccluder, Queries, InOutPose, Report);
		Context.Scratch.bHasSolverReport = true;
	}
	else
	{
		State->Deoccluder.Reset();
	}

	// Step 2 (priority 1): the camera never ends up inside geometry.
	const FVector Desired = InOutPose.Location;
	const FVector ToCamera = Desired - Origin;
	const double FullDistance = ToCamera.Size();
	if (FullDistance < UE_KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector Direction = ToCamera / FullDistance;
	FHitResult Hit;
	const bool bHit = ACFCameraCollision::SweepCamera(*World, Origin, Desired, ProbeRadius, ProbeChannel, Params, Hit);

	const double HitDistance = bHit ? FMath::Clamp(static_cast<double>(Hit.Distance), 0.0, FullDistance) : FullDistance;
	const double Allowed = FMath::Max(HitDistance, FMath::Min(static_cast<double>(MinDistance), FullDistance));
	double Current = State->CurrentDistance;
	if (!State->bInitialized)
	{
		Current = Allowed; // activation / teleport: also covers a camera that starts inside a wall
		State->bInitialized = true;
	}
	else if (Allowed < Current)
	{
		Current = PullInDampingTime > 0.f ? Current + (Allowed - Current) * ACFCameraMath::ExpAlpha(PullInDampingTime, Dt) : Allowed;
	}
	else if (Current >= State->LastFullDistance - 0.5)
	{
		Current = Allowed; // was not obstructed: follow distance changes (FreeLook rings, blends) directly
	}
	else
	{
		Current = ReturnDampingTime > 0.f ? Current + (Allowed - Current) * ACFCameraMath::ExpAlpha(ReturnDampingTime, Dt) : Allowed;
	}
	Current = FMath::Min(Current, FullDistance);
	State->CurrentDistance = static_cast<float>(Current);
	State->LastFullDistance = static_cast<float>(FullDistance);
	InOutPose.Location = Origin + Direction * Current;

	// Step 3 (priority 3 / 4): if the subjects no longer fit after the camera was moved in, widen the FOV within
	// limits; as a last resort turn slightly to keep the player on screen.
	if (FramingRecovery.bEnabled && Context.Scratch.bHasFramingSubjects && bSolverAllowed)
	{
		FACFCameraConstraintSolver::RecoverFraming(FramingRecovery, Context.Scratch.FramingSubjects, Context.Scratch.FramingSize,
			Context.Evaluation.AspectRatio, Dt, State->Recovery, InOutPose, Report, Context.Evaluation.HorizontalFovScale);
		Context.Scratch.bHasSolverReport = true;
	}
	else
	{
		State->Recovery.Reset();
	}

#if !UE_BUILD_SHIPPING
	FACFCameraRigDebug& Debug = Context.Scratch.Debug;
	Debug.bHasCollision = true;
	Debug.bCollisionHit = bHit;
	Debug.CollisionOrigin = Origin;
	Debug.CollisionDesired = Desired;
	Debug.CollisionHit = bHit ? Hit.Location : Desired;
#endif
}

bool ACFCameraCollision::SweepCamera(const UWorld& World, const FVector& Origin, const FVector& Desired, float Radius,
	ECollisionChannel Channel, FCollisionQueryParams& Params, FHitResult& OutHit)
{
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);
	ACF_PROFILE_QUERY(Sweep); // purpose comes from the caller's scope (Collision, Deoccluder*, OutputGuard, ShotProbe)
	bool bHit = World.SweepSingleByChannel(OutHit, Origin, Desired, FQuat::Identity, Channel, Sphere, Params);
	if (bHit && OutHit.bStartPenetrating)
	{
		// The pivot itself is inside something (e.g. the character pressed into a wall). Ignore what we
		// start in and sweep again, otherwise the camera would collapse onto the pivot every frame.
		if (const UPrimitiveComponent* Started = OutHit.GetComponent())
		{
			Params.AddIgnoredComponent(Started);
		}
		ACF_PROFILE_QUERY(Sweep);
		bHit = World.SweepSingleByChannel(OutHit, Origin, Desired, FQuat::Identity, Channel, Sphere, Params);
		if (bHit && OutHit.bStartPenetrating)
		{
			bHit = false;
		}
	}
	return bHit;
}
