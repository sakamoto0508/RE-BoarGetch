#pragma once

#include "CoreMinimal.h"
#include "ACFCameraNode.h"
#include "Engine/EngineTypes.h"
#include "ACFCameraConstraintSolver.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "ACFCameraNode_Collision.generated.h"

/** Runtime state of the Collision node. */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraCollisionState
{
	GENERATED_BODY()

	/** Current allowed distance from the pivot (smoothed when returning). */
	float CurrentDistance = 0.f;
	/** Unobstructed distance of the previous evaluation (tells "returning from a hit" from "zooming out"). */
	float LastFullDistance = 0.f;
	bool bInitialized = false;

	// Constraint solver (plain C++ state, copied with the struct; fixed size).
	FACFCameraDeoccluderState Deoccluder;
	FACFCameraFramingRecoveryState Recovery;
};

/**
 * PostBlend: keeps the FINAL camera out of walls with a sphere sweep from the rig's pivot (the
 * follow / orbit target) to the camera, like USpringArmComponent's probe.
 *
 * It runs once per frame AFTER request blending (whatever its position in the node list), using
 * the pivot of the cameras being blended, so a blend between two cameras never cuts through
 * geometry. It runs BEFORE UE camera shakes (they are applied by the PlayerCameraManager after
 * ACF): keep ProbeRadius larger than the shake translation amplitude to stay clear of walls.
 *
 * Pull-in is instant by default (the camera never ends up inside a wall); moving back out once
 * the obstacle is gone is smoothed by ReturnDampingTime.
 */
UCLASS(meta = (DisplayName = "Collision"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode_Collision : public UACFCameraNode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "0.0", Units = cm))
	float ProbeRadius = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
	TEnumAsByte<ECollisionChannel> ProbeChannel = ECC_Camera;

	/** The camera never gets closer to the pivot than this (unless the desired camera is closer). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "0.0", Units = cm))
	float MinDistance = 20.f;

	/** Smoothing when an obstacle pushes the camera in. 0 = instant (recommended: no wall clipping). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "0.0", UIMax = "0.5", Units = s))
	float PullInDampingTime = 0.f;

	/** Smoothing when the obstacle is gone and the camera moves back to its desired distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "0.0", UIMax = "2.0", Units = s))
	float ReturnDampingTime = 0.35f;

	/** Ignore the actor that owns the pivot (the follow target), so the character never blocks its own camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
	bool bIgnorePivotActor = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
	bool bIgnoreLookAtTargetActor = false;

	/** Ignore the pawn controlled by this player (and its attached actors). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
	bool bIgnoreControlledPawn = true;

	/** Sweep origin when the rig publishes no pivot (e.g. Fixed Pose only): follow target + this offset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
	FVector FallbackOriginOffset = FVector(0.f, 0.f, 60.f);

	/**
	 * Constraint solver step 1 (runs before the sweep): keeps the player and important targets visible by orbiting
	 * / pulling the camera around the pivot. The sweep below still has the last word (no wall clipping).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint Solver")
	FACFCameraDeoccluderSettings Deoccluder;

	/** Constraint solver step 3 (after the sweep): widen the FOV within limits when the camera was pushed in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint Solver")
	FACFCameraFramingRecoverySettings FramingRecovery;

	virtual EACFCameraNodeStage GetStage() const override { return EACFCameraNodeStage::PostBlend; }
	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraCollisionState::StaticStruct(); }
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const override { ResetInterpolation(RuntimeState); }
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const override;
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const override;
};

namespace ACFCameraCollision
{
	/**
	 * Sphere sweep from Origin to Desired. If the sweep starts inside geometry (pivot pressed into a wall)
	 * the component it starts in is ignored and the sweep repeated once. True on a blocking hit; OutHit.Location
	 * is then the sphere centre at contact.
	 */
	ACTIONCAMERAFRAMEWORK_API bool SweepCamera(const UWorld& World, const FVector& Origin, const FVector& Desired, float Radius,
		ECollisionChannel Channel, FCollisionQueryParams& Params, FHitResult& OutHit);

	/** False while the debug cvar ACF.Debug.Solver is 0 (deoccluder and framing recovery bypassed). */
	ACTIONCAMERAFRAMEWORK_API bool IsSolverEnabled();

	inline const AActor* ActorOf(const UObject* Object)
	{
		if (const AActor* Actor = Cast<AActor>(Object))
		{
			return Actor;
		}
		const UActorComponent* Component = Cast<UActorComponent>(Object);
		return Component ? Component->GetOwner() : nullptr;
	}
}
