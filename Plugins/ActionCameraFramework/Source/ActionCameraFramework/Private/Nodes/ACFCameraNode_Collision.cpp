#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNodeUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

void UACFCameraNode_Collision::ResetInterpolation(FInstancedStruct* RuntimeState) const
{
	if (FACFCameraCollisionState* State = ACFNodes::GetState<FACFCameraCollisionState>(RuntimeState))
	{
		State->bInitialized = false;
	}
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

	const FVector Desired = InOutPose.Location;
	const FVector ToCamera = Desired - Origin;
	const double FullDistance = ToCamera.Size();
	if (FullDistance < UE_KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector Direction = ToCamera / FullDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ACFCameraCollision), false);
	if (bIgnorePivotActor && PivotActor)
	{
		Params.AddIgnoredActor(PivotActor);
	}
	if (bIgnoreLookAtTargetActor)
	{
		if (const AActor* LookActor = Context.Request.LookAtTarget.Actor.Get())
		{
			Params.AddIgnoredActor(LookActor);
		}
	}
	if (bIgnoreControlledPawn && Context.Evaluation.PlayerController)
	{
		if (const APawn* Pawn = Context.Evaluation.PlayerController->GetPawn())
		{
			Params.AddIgnoredActor(Pawn);
		}
	}
	for (const TObjectPtr<AActor>& Ignored : Context.Request.CollisionIgnoredActors)
	{
		if (const AActor* Actor = Ignored.Get(); IsValid(Actor))
		{
			Params.AddIgnoredActor(Actor);
		}
	}

	const FCollisionShape Sphere = FCollisionShape::MakeSphere(ProbeRadius);
	FHitResult Hit;
	bool bHit = World->SweepSingleByChannel(Hit, Origin, Desired, FQuat::Identity, ProbeChannel, Sphere, Params);
	if (bHit && Hit.bStartPenetrating)
	{
		// The pivot itself is inside something (e.g. the character pressed into a wall). Ignore what we
		// start in and sweep again, otherwise the camera would collapse onto the pivot every frame.
		if (const UPrimitiveComponent* Started = Hit.GetComponent())
		{
			Params.AddIgnoredComponent(Started);
		}
		bHit = World->SweepSingleByChannel(Hit, Origin, Desired, FQuat::Identity, ProbeChannel, Sphere, Params);
		if (bHit && Hit.bStartPenetrating)
		{
			bHit = false;
		}
	}

	const double HitDistance = bHit ? FMath::Clamp(static_cast<double>(Hit.Distance), 0.0, FullDistance) : FullDistance;
	const double Allowed = FMath::Max(HitDistance, FMath::Min(static_cast<double>(MinDistance), FullDistance));
	const float Dt = Context.GetDeltaTime();
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

#if !UE_BUILD_SHIPPING
	FACFCameraRigDebug& Debug = Context.Scratch.Debug;
	Debug.bHasCollision = true;
	Debug.bCollisionHit = bHit;
	Debug.CollisionOrigin = Origin;
	Debug.CollisionDesired = Desired;
	Debug.CollisionHit = bHit ? Hit.Location : Desired;
#endif
}
