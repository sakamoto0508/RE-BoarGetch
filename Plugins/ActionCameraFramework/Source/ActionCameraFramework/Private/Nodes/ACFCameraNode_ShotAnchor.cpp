#include "Nodes/ACFCameraNode_ShotAnchor.h"
#include "Nodes/ACFCameraNodeUtils.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

namespace ACFShotAnchor
{
	static const AActor* ActorOf(const UObject* Object)
	{
		if (const AActor* Actor = Cast<AActor>(Object))
		{
			return Actor;
		}
		const UActorComponent* Component = Cast<UActorComponent>(Object);
		return Component ? Component->GetOwner() : nullptr;
	}

	static FQuat YawOnly(const FVector& Direction)
	{
		const FVector Flat(Direction.X, Direction.Y, 0.0);
		return Flat.SizeSquared() > UE_KINDA_SMALL_NUMBER ? FRotator(0.0, Flat.Rotation().Yaw, 0.0).Quaternion() : FQuat::Identity;
	}
}

void UACFCameraNode_ShotAnchor::OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const
{
	// Every activation is a new shot: a fixed shot captures its frame again, a dynamic one snaps.
	ResetInterpolation(RuntimeState);
}

void UACFCameraNode_ShotAnchor::ResetInterpolation(FInstancedStruct* RuntimeState) const
{
	if (FACFCameraShotAnchorState* State = ACFNodes::GetState<FACFCameraShotAnchorState>(RuntimeState))
	{
		State->bHasFrame = false;
	}
}

bool UACFCameraNode_ShotAnchor::ComputeFrame(const FACFCameraNodeContext& Context, FVector& OutAnchor, FQuat& OutBasis, const AActor*& OutPivotActor) const
{
	FTransform Follow, Look;
	const UObject* FollowObject = nullptr;
	const UObject* LookObject = nullptr;
	const bool bFollow = Context.Request.FollowTarget.Resolve(Follow, FollowObject);
	const bool bLook = Context.Request.LookAtTarget.Resolve(Look, LookObject);
	if (!bFollow && !bLook)
	{
		return false;
	}

	FVector Point;
	switch (AnchorPoint)
	{
	case EACFCameraShotAnchorPoint::FollowTarget:
		Point = bFollow ? Follow.GetLocation() : Look.GetLocation();
		break;
	case EACFCameraShotAnchorPoint::LookAtTarget:
		Point = bLook ? Look.GetLocation() : Follow.GetLocation();
		break;
	default:
		Point = bFollow && bLook ? FMath::Lerp(Follow.GetLocation(), Look.GetLocation(), static_cast<double>(BetweenAlpha))
			: (bFollow ? Follow.GetLocation() : Look.GetLocation());
		break;
	}
	OutAnchor = Point + AnchorOffset;
	OutPivotActor = ACFShotAnchor::ActorOf(bFollow ? FollowObject : LookObject);

	switch (Basis)
	{
	case EACFCameraShotAnchorBasis::World:
		OutBasis = FQuat::Identity;
		break;
	case EACFCameraShotAnchorBasis::FollowTargetYaw:
		OutBasis = FRotator(0.0, (bFollow ? Follow : Look).Rotator().Yaw, 0.0).Quaternion();
		break;
	default:
		if (bFollow && bLook && FVector::DistSquared2D(Follow.GetLocation(), Look.GetLocation()) > 1.0)
		{
			OutBasis = ACFShotAnchor::YawOnly(Look.GetLocation() - Follow.GetLocation());
		}
		else
		{
			OutBasis = FRotator(0.0, (bFollow ? Follow : Look).Rotator().Yaw, 0.0).Quaternion();
		}
		break;
	}
	return true;
}

void UACFCameraNode_ShotAnchor::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraShotAnchorState* State = ACFNodes::GetState<FACFCameraShotAnchorState>(RuntimeState);
	if (!State)
	{
		return;
	}

	if (!(bFreezeAtShotStart && State->bHasFrame))
	{
		FVector Anchor;
		FQuat BasisQuat;
		const AActor* PivotActor = nullptr;
		if (ComputeFrame(Context, Anchor, BasisQuat, PivotActor))
		{
			const float Dt = Context.GetDeltaTime();
			if (State->bHasFrame && !bFreezeAtShotStart && DampingTime > 0.f && Dt > 0.f)
			{
				const float Alpha = ACFCameraMath::ExpAlpha(DampingTime, Dt);
				State->Anchor = FMath::Lerp(State->Anchor, Anchor, static_cast<double>(Alpha));
				State->Basis = FQuat::Slerp(State->Basis, BasisQuat, Alpha);
			}
			else
			{
				State->Anchor = Anchor;
				State->Basis = BasisQuat;
			}
			State->PivotActor = PivotActor;
			State->bHasFrame = true;
		}
		// else: targets lost -> keep the last frame (if any).
	}
	if (!State->bHasFrame)
	{
		return;
	}

	InOutPose.Location = State->Anchor + State->Basis.RotateVector(CameraOffset);
	if (bAimAtAnchor)
	{
		const FVector ToAnchor = State->Anchor - InOutPose.Location;
		if (!ToAnchor.IsNearlyZero())
		{
			InOutPose.Rotation = ToAnchor.Rotation();
			InOutPose.Rotation.Roll = 0.0;
		}
	}
	if (bOverrideFieldOfView)
	{
		InOutPose.FieldOfView = FieldOfView;
	}

	Context.Scratch.bHasPivot = true;
	Context.Scratch.Pivot = State->Anchor;
	Context.Scratch.PivotActor = State->PivotActor.Get();
#if !UE_BUILD_SHIPPING
	Context.Scratch.Debug.bHasFollowTarget = true;
	Context.Scratch.Debug.FollowTarget = State->Anchor;
#endif
}
