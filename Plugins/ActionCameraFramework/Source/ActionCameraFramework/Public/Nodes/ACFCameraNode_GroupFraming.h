#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "Nodes/ACFCameraTargetGroup.h"
#include "ACFCameraNode_GroupFraming.generated.h"

/** How Group Framing makes the subjects fit. */
UENUM(BlueprintType)
enum class EACFCameraGroupFramingMode : uint8
{
	/** Move the camera along its view axis (keeps perspective, may be shortened by Collision afterwards). */
	Distance,
	/** Change the field of view only (camera position unchanged, works in tight spaces). */
	FieldOfView,
	/** Move first (within Max Dolly In / Out); if that is not enough, change the FOV as well. */
	DistanceThenFieldOfView,
};

/** Whether Group Framing only prevents subjects from leaving the frame or actively fills it. */
UENUM(BlueprintType)
enum class EACFCameraGroupFramingPolicy : uint8
{
	/** Only pull back / widen when a subject would leave the framing area; never tighten the incoming camera. */
	KeepInside,
	/** Move in or out (zoom in or out) so the subjects fill the framing area. */
	Fill,
};

/** Runtime state of Group Framing: the adjustment relative to the incoming camera. */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraGroupFramingState
{
	GENERATED_BODY()

	/** Distance moved back along the view axis (negative = moved in). */
	float Dolly = 0.f;
	/** Degrees added to the incoming FOV. */
	float FovDelta = 0.f;
	bool bInitialized = false;
};

/**
 * Framing: keeps several subjects (the request's Target Group, plus optionally its Follow / Look At targets)
 * inside FramingSize of the screen, as spheres of their Radius.
 *
 * It runs after Body (position) and Aim (rotation): the rotation is final, so it computes, for every subject
 * sphere, the exact distance along the view axis or the field of view at which the sphere touches the
 * framing rectangle, and uses the most demanding one. It never rotates the camera: aim at the group with
 * Look At / Rotation Composer (Target Source = Target Group) or with the Lock-On node.
 *
 * Weight: members are pulled towards the weighted centre by (1 - Weight / max Weight) for this test, so a
 * light member may leave the frame slightly and a member fading in (Weight 0 -> 1) moves the camera smoothly.
 * 0 subjects: the adjustment eases back to none. 1 subject: its sphere alone is framed (Radius matters).
 * The adjustment is damped (widen fast, narrow slowly) and is relative to the incoming camera, so it never
 * accumulates and the Body / Aim nodes keep their own damping.
 */
UCLASS(meta = (DisplayName = "Group Framing"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_GroupFraming : public UACFCameraNode
{
	GENERATED_BODY()

public:
	/** Frame the request's Target Group members. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subjects")
	bool bFrameTargetGroup = true;

	/** Also frame the request's Follow Target (e.g. the player in a lock-on camera). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subjects")
	FACFCameraFramingSubjectSettings FollowTargetSubject;

	/** Also frame the request's Look At Target (e.g. the lock-on enemy). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subjects")
	FACFCameraFramingSubjectSettings LookAtTargetSubject;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Framing")
	EACFCameraGroupFramingMode Mode = EACFCameraGroupFramingMode::DistanceThenFieldOfView;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Framing")
	EACFCameraGroupFramingPolicy Policy = EACFCameraGroupFramingPolicy::KeepInside;

	/** Part of the screen (1 = full width / height, centred) the subject spheres must stay inside. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Framing", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	FVector2D FramingSize = FVector2D(0.8f, 0.8f);

	/** How far the camera may move towards the subjects (Fill only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Framing", meta = (ClampMin = "0.0", Units = cm))
	float MaxDollyIn = 300.f;

	/** How far the camera may move back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Framing", meta = (ClampMin = "0.0", Units = cm))
	float MaxDollyOut = 1200.f;

	/** Subjects must stay at least this far in front of the camera (their sphere included). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Framing", meta = (ClampMin = "0.0", Units = cm))
	float NearClearance = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Framing", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = deg))
	float MinFieldOfView = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Framing", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = deg))
	float MaxFieldOfView = 100.f;

	/** Time constant when pulling back / widening (a subject is about to leave: react fast). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damping", meta = (ClampMin = "0.0", UIMax = "2.0", Units = s))
	float WidenDampingTime = 0.15f;

	/** Time constant when moving in / narrowing (calm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damping", meta = (ClampMin = "0.0", UIMax = "3.0", Units = s))
	float NarrowDampingTime = 0.8f;

	/** Required camera move along the view axis (positive = back) so every subject fits. Pure function (tests). */
	static double ComputeRequiredDolly(const FACFCameraFramingSubjects& Subjects, const FVector& CameraLocation, const FRotator& CameraRotation,
		float FieldOfView, float AspectRatio, const FVector2D& FramingSize, float NearClearance);

	/** Field of view at which every subject fits from CameraLocation (MaxFov if a subject is behind the camera). */
	static double ComputeRequiredFov(const FACFCameraFramingSubjects& Subjects, const FVector& CameraLocation, const FRotator& CameraRotation,
		float AspectRatio, const FVector2D& FramingSize, float NearClearance, float MaxFov);

	/** Applies the Weight rule: members are pulled towards the weighted centre (in place). False if empty. */
	static bool ApplyWeights(FACFCameraFramingSubjects& InOutSubjects, FVector& OutCenter);

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::Framing; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraGroupFramingState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override { ResetInterpolation(RuntimeState); }
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};
