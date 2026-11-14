#pragma once

#include "CoreMinimal.h"
#include "ACFCameraTypes.h"
#include "UObject/ObjectKey.h"
#include "ACFCameraTargetGroup.generated.h"

/** Extra framing subject taken from the request's Follow / Look At target (no group setup needed). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraFramingSubjectSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject")
	bool bInclude = false;

	/** World-space offset from the resolved target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject", meta = (EditCondition = "bInclude"))
	FVector Offset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject", meta = (EditCondition = "bInclude", ClampMin = "0.0", UIMax = "4.0"))
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject", meta = (EditCondition = "bInclude", ClampMin = "0.0", Units = cm))
	float Radius = 60.f;
};

/** A resolved sphere to keep on screen. */
struct FACFCameraFramingSubject
{
	FVector Location = FVector::ZeroVector;
	float Radius = 0.f;
	float Weight = 0.f;
	/** Owning actor (ignored by its own line-of-sight trace). May be null. */
	const AActor* Actor = nullptr;
	/**
	 * Identity of Actor that survives address reuse (object index + serial number; B-3). Set with SetActor where a live
	 * actor is resolved. The constraint solver matches by it when set, else by the Actor address (identity only).
	 */
	FObjectKey ActorKey;
	/** The player (Follow Target): highest visibility priority in the constraint solver. */
	bool bIsPlayer = false;

	/** InActor must be a live actor (or null). */
	void SetActor(const AActor* InActor)
	{
		Actor = InActor;
		ActorKey = InActor ? FObjectKey(InActor) : FObjectKey();
	}
};

/** Stack buffer: up to 16 subjects without touching the heap (more is allowed, it then allocates). */
using FACFCameraFramingSubjects = TArray<FACFCameraFramingSubject, TInlineAllocator<16>>;

namespace ACFTargetGroup
{
	/**
	 * Resolves the live, weighted (Weight > 0) members of Group plus the optional Follow / Look At
	 * subjects into Out. Destroyed members are skipped (the director also removes them from the group).
	 * LookAtLocationOverride replaces the Look At target's location (the Lock-On node's smoothed point).
	 */
	ACTIONCAMERAFRAMEWORK_API void GatherSubjects(const FACFCameraRequestParams& Request, bool bIncludeGroup,
		const FACFCameraFramingSubjectSettings& FollowSubject, const FACFCameraFramingSubjectSettings& LookAtSubject, FACFCameraFramingSubjects& Out,
		const FVector* LookAtLocationOverride = nullptr);

	/** Weighted average of the subjects' locations. False if there is no subject. */
	ACTIONCAMERAFRAMEWORK_API bool ComputeCenter(const FACFCameraFramingSubjects& Subjects, FVector& OutCenter, float& OutMaxWeight);

	/** Weighted centre of the request's target group (aim source "Target Group"). False if it has no live member. */
	ACTIONCAMERAFRAMEWORK_API bool ComputeGroupCenter(const FACFCameraTargetGroup& Group, FVector& OutCenter);
}
