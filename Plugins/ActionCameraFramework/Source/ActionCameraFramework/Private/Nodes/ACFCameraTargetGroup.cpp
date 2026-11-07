#include "Nodes/ACFCameraTargetGroup.h"

namespace ACFTargetGroup
{
	static void AddTarget(const FACFCameraTarget& Target, const FVector& Offset, float Weight, float Radius, FACFCameraFramingSubjects& Out)
	{
		if (Weight <= 0.f)
		{
			return;
		}
		FTransform Transform;
		const UObject* Object = nullptr;
		if (Target.Resolve(Transform, Object))
		{
			Out.Add({ Transform.GetLocation() + Offset, FMath::Max(Radius, 0.f), Weight });
		}
	}
}

void ACFTargetGroup::GatherSubjects(const FACFCameraRequestParams& Request, bool bIncludeGroup,
	const FACFCameraFramingSubjectSettings& FollowSubject, const FACFCameraFramingSubjectSettings& LookAtSubject, FACFCameraFramingSubjects& Out,
	const FVector* LookAtLocationOverride)
{
	Out.Reset();
	if (bIncludeGroup)
	{
		for (const FACFCameraTargetGroupMember& Member : Request.TargetGroup.Members)
		{
			AddTarget(Member.Target, Member.Offset, Member.Weight, Member.Radius, Out);
		}
	}
	if (FollowSubject.bInclude)
	{
		AddTarget(Request.FollowTarget, FollowSubject.Offset, FollowSubject.Weight, FollowSubject.Radius, Out);
	}
	if (LookAtSubject.bInclude && LookAtLocationOverride)
	{
		if (LookAtSubject.Weight > 0.f)
		{
			Out.Add({ *LookAtLocationOverride + LookAtSubject.Offset, FMath::Max(LookAtSubject.Radius, 0.f), LookAtSubject.Weight });
		}
	}
	else if (LookAtSubject.bInclude)
	{
		AddTarget(Request.LookAtTarget, LookAtSubject.Offset, LookAtSubject.Weight, LookAtSubject.Radius, Out);
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
