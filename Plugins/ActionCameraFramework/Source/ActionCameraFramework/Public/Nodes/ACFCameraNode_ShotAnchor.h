#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "ACFCameraNode_ShotAnchor.generated.h"

/** Point a Shot Anchor frames. */
UENUM(BlueprintType)
enum class EACFCameraShotAnchorPoint : uint8
{
	FollowTarget,
	LookAtTarget,
	/** Between follow and look-at target (Between Alpha: 0 = follow, 1 = look-at). Falls back to whichever exists. */
	Between,
};

/** Axes the camera offset is expressed in. */
UENUM(BlueprintType)
enum class EACFCameraShotAnchorBasis : uint8
{
	World,
	/** Yaw of the follow target (attacker facing). */
	FollowTargetYaw,
	/**
	 * Line of action: X = from follow target to look-at target (attacker -> target, on the ground plane), Y = right of
	 * it, Z = up. A shot authored this way frames the same side of the action wherever it happens in the level.
	 */
	LineOfAction,
};

/** Runtime state of a Shot Anchor (per request). */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraShotAnchorState
{
	GENERATED_BODY()

	bool bHasFrame = false;
	FVector Anchor = FVector::ZeroVector;
	FQuat Basis = FQuat::Identity;
	TWeakObjectPtr<const AActor> PivotActor;
};

/**
 * Body (+ optional aim): places the camera relative to the action of a shot (finisher / cinematic), e.g.
 * "350 cm to the right of the attacker -> target line, slightly behind, looking at the middle".
 *
 * Dynamic shot: re-computed every frame (optionally damped), follows attacker and target.
 * Fixed shot (Freeze At Shot Start): the anchor and basis are captured once when the shot's request becomes active
 * and never move again, so the camera stays still while the actors move inside the frame.
 *
 * Publishes the anchor as the rig pivot: Collision sweeps from the anchor to the camera, so a staged camera behind a
 * wall is pulled towards the action rather than through it. Lost targets: the last frame is held.
 */
UCLASS(meta = (DisplayName = "Shot Anchor"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_ShotAnchor : public UACFCameraNode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot Anchor")
	EACFCameraShotAnchorPoint AnchorPoint = EACFCameraShotAnchorPoint::Between;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot Anchor", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "AnchorPoint == EACFCameraShotAnchorPoint::Between"))
	float BetweenAlpha = 0.5f;

	/** World offset added to the anchor (e.g. chest height). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot Anchor")
	FVector AnchorOffset = FVector(0.f, 0.f, 80.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot Anchor")
	EACFCameraShotAnchorBasis Basis = EACFCameraShotAnchorBasis::LineOfAction;

	/** Camera position relative to the anchor, in Basis axes (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot Anchor")
	FVector CameraOffset = FVector(-120.f, 380.f, 40.f);

	/** Fixed shot: capture anchor + basis when the shot starts and keep them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot Anchor")
	bool bFreezeAtShotStart = false;

	/** Dynamic shot: time constant of the anchor (0 = rigid). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot Anchor", meta = (ClampMin = "0.0", UIMax = "2.0", Units = s, EditCondition = "!bFreezeAtShotStart"))
	float DampingTime = 0.1f;

	/** Look at the anchor (off: rotation comes from the upstream camera / Aim nodes such as Look At). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot Anchor")
	bool bAimAtAnchor = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot Anchor", meta = (InlineEditConditionToggle))
	bool bOverrideFieldOfView = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot Anchor", meta = (EditCondition = "bOverrideFieldOfView", UIMin = "5.0", UIMax = "170", ClampMin = "0.001", ClampMax = "360.0", Units = deg))
	float FieldOfView = 60.f;

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::Body; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraShotAnchorState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override;
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;

	/** Anchor + basis from the current targets. False if no target can be resolved. */
	bool ComputeFrame(const FACFCameraNodeContext& Context, FVector& OutAnchor, FQuat& OutBasis, const AActor*& OutPivotActor) const;
};
