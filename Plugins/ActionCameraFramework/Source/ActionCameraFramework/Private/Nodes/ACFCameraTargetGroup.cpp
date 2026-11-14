#include "Nodes/ACFCameraTargetGroup.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

namespace ACFTargetGroup
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

	static void AddTarget(const FACFCameraTarget& Target, const FVector& Offset, float Weight, float Radius, const AActor* PlayerActor, bool bForcePlayer, FACFCameraFramingSubjects& Out)
	{
		if (Weight <= 0.f)
		{
			return;
		}
		FTransform Transform;
		const UObject* Object = nullptr;
		if (Target.Resolve(Transform, Object))
		{
			FACFCameraFramingSubject& Subject = Out.AddDefaulted_GetRef();
			Subject.Location = Transform.GetLocation() + Offset;
			Subject.Radius = FMath::Max(Radius, 0.f);
			Subject.Weight = Weight;
			Subject.SetActor(ActorOf(Object));
			Subject.bIsPlayer = bForcePlayer || (PlayerActor && Subject.Actor == PlayerActor);
		}
	}
}

void ACFTargetGroup::GatherSubjects(const FACFCameraRequestParams& Request, bool bIncludeGroup,
	const FACFCameraFramingSubjectSettings& FollowSubject, const FACFCameraFramingSubjectSettings& LookAtSubject, FACFCameraFramingSubjects& Out,
	const FVector* LookAtLocationOverride)
{
	Out.Reset();
	FTransform PlayerTransform;
	const UObject* PlayerObject = nullptr;
	const AActor* PlayerActor = Request.FollowTarget.Resolve(PlayerTransform, PlayerObject) ? ActorOf(PlayerObject) : nullptr;
	if (bIncludeGroup)
	{
		for (const FACFCameraTargetGroupMember& Member : Request.TargetGroup.Members)
		{
			AddTarget(Member.Target, Member.Offset, Member.Weight, Member.Radius, PlayerActor, false, Out);
		}
	}
	if (FollowSubject.bInclude)
	{
		AddTarget(Request.FollowTarget, FollowSubject.Offset, FollowSubject.Weight, FollowSubject.Radius, PlayerActor, true, Out);
	}
	if (LookAtSubject.bInclude && LookAtLocationOverride)
	{
		if (LookAtSubject.Weight > 0.f)
		{
			FACFCameraFramingSubject& Subject = Out.AddDefaulted_GetRef();
			Subject.Location = *LookAtLocationOverride + LookAtSubject.Offset;
			Subject.Radius = FMath::Max(LookAtSubject.Radius, 0.f);
			Subject.Weight = LookAtSubject.Weight;
			FTransform Unused;
			const UObject* LookObject = nullptr;
			Subject.SetActor(Request.LookAtTarget.Resolve(Unused, LookObject) ? ActorOf(LookObject) : nullptr);
		}
	}
	else if (LookAtSubject.bInclude)
	{
		AddTarget(Request.LookAtTarget, LookAtSubject.Offset, LookAtSubject.Weight, LookAtSubject.Radius, PlayerActor, false, Out);
	}
}

bool ACFTargetGroup::ComputeCenter(const FACFCameraFramingSubjects& Subjects, FVector& OutCenter, float& OutMaxWeight)
{
	FVector Sum = FVector::ZeroVector;
	double TotalWeight = 0.0;
	OutMaxWeight = 0.f;
	for (const FACFCameraFramingSubject& Subject : Subjects)
	{
		Sum += Subject.Location * Subject.Weight;
		TotalWeight += Subject.Weight;
		OutMaxWeight = FMath::Max(OutMaxWeight, Subject.Weight);
	}
	if (TotalWeight <= UE_SMALL_NUMBER)
	{
		return false;
	}
	OutCenter = Sum / TotalWeight;
	return true;
}

bool ACFTargetGroup::ComputeGroupCenter(const FACFCameraTargetGroup& Group, FVector& OutCenter)
{
	FVector Sum = FVector::ZeroVector;
	double TotalWeight = 0.0;
	for (const FACFCameraTargetGroupMember& Member : Group.Members)
	{
		FTransform Transform;
		const UObject* Object = nullptr;
		if (Member.Weight > 0.f && Member.Target.Resolve(Transform, Object))
		{
			Sum += (Transform.GetLocation() + Member.Offset) * Member.Weight;
			TotalWeight += Member.Weight;
		}
	}
	if (TotalWeight <= UE_SMALL_NUMBER)
	{
		return false;
	}
	OutCenter = Sum / TotalWeight;
	return true;
}
