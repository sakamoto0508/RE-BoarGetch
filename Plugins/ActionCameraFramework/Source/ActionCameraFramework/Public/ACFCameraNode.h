#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACFCameraTypes.h"
#include "Engine/EngineTypes.h"
#include "ACFCameraConstraintSolver.h"
#include "ACFCameraNode.generated.h"

struct FInstancedStruct;
class AActor;

/**
 * When a node runs. A rig always evaluates its nodes stage by stage (array order only matters
 * inside a stage), so a preset cannot accidentally aim before it positions:
 *
 *   Body      : camera position (Follow, Orbital Follow, Position Composer, Fixed Pose)
 *   Aim       : camera rotation (Look At, Rotation Composer)
 *   Framing   : composition once position AND rotation are known: combat distance / shoulder / FOV (Combat
 *               Camera, runs first), then fitting subjects on screen (Group Framing). Never changes rotation.
 *   PostBlend : corrections of the FINAL pose, run once after request blending (Collision).
 *               This keeps a blend between two cameras from passing through walls.
 */
UENUM(BlueprintType)
enum class EACFCameraNodeStage : uint8
{
	Body,
	Aim,
	Framing,
	PostBlend,
};

#if !UE_BUILD_SHIPPING
/** Debug information nodes publish while evaluating (compiled out of Shipping). */
struct FACFCameraRigDebug
{
	bool bHasFollowTarget = false;
	FVector FollowTarget = FVector::ZeroVector;
	bool bHasLookAtTarget = false;
	FVector LookAtTarget = FVector::ZeroVector;
	bool bHasPrediction = false;
	FVector PredictedTarget = FVector::ZeroVector;
	bool bHasOrbit = false;
	float OrbitYaw = 0.f;
	float OrbitPitch = 0.f;
	float OrbitDistance = 0.f;
	bool bHasComposition = false;
	FVector2D ScreenPosition = FVector2D::ZeroVector;
	FVector2D DeadZone = FVector2D::ZeroVector;
	FVector2D SoftZone = FVector2D::ZeroVector;
	FVector2D TargetOnScreen = FVector2D::ZeroVector;
	// Group framing: subjects (xyz = centre, w = radius after weighting), fixed size (no per-frame allocation).
	static constexpr int32 MaxGroupSubjects = 16;
	int32 NumGroupSubjects = 0;
	FVector4 GroupSubjects[MaxGroupSubjects];
	bool bHasGroup = false;
	FVector GroupCenter = FVector::ZeroVector;
	float GroupDolly = 0.f;
	float GroupFovDelta = 0.f;
	FVector2D GroupFramingSize = FVector2D::ZeroVector;
	bool bHasCombat = false;
	bool bInCombat = false;
	float CombatWeight = 0.f;
	float CombatDistanceScale = 1.f;
	float CombatFovOffset = 0.f;
	float CombatRange = -1.f;
	bool bHasLockOn = false;
	bool bLockOnTargetLost = false;
	FVector LockOnPoint = FVector::ZeroVector;
	FVector LockOnFocus = FVector::ZeroVector;
	float LockOnRange = 0.f;
	bool bHasCollision = false;
	bool bCollisionHit = false;
	FVector CollisionOrigin = FVector::ZeroVector;
	FVector CollisionHit = FVector::ZeroVector;
	FVector CollisionDesired = FVector::ZeroVector;
	// Phase 7B-1: movement assistance of the Orbital Follow (plain values; the debugger formats them at UI rate).
	bool bHasAssistance = false;
	bool bAssistanceEnabled = false;
	bool bAssistanceSuppressedBySource = false;
	EACFCameraRecenterHeading AssistanceHeading = EACFCameraRecenterHeading::TargetFacing;
	EACFCameraAssistanceState AssistanceState = EACFCameraAssistanceState::Disabled;
	EACFCameraAssistanceReason AssistanceReason = EACFCameraAssistanceReason::NotEnabled;
	bool bHasAssistanceTargetYaw = false;
	float AssistanceTargetYaw = 0.f;
	float AssistanceMovementSpeed = 0.f;
	float AssistanceWaitTime = 0.f;
	float AssistanceRecenterDelay = 0.f;
	float AssistanceMovementTime = 0.f;
	float AssistanceRequiredMovementTime = 0.f;
};
#endif

/**
 * Per-evaluation scratch shared by the nodes of ONE rig evaluation (not stored between frames).
 * Body nodes publish the pivot they orbit / follow, so post-blend Collision can sweep from it.
 */
/**
 * Hard position constraint published by the Collision node of the camera in control. The director keeps
 * it for the final guard (UACFCameraCollisionGuardModifier), which re-applies it after the Weight blend
 * with the UE camera and after camera shakes.
 */
struct FACFCameraCollisionConstraint
{
	bool bValid = false;
	FVector Pivot = FVector::ZeroVector;
	float ProbeRadius = 0.f;
	ECollisionChannel Channel = ECC_Camera;
	TArray<const AActor*, TInlineAllocator<4>> IgnoredActors;
};

struct FACFCameraRigScratch
{
	/** Safe point the camera is attached to (target + pivot offset, without lookahead / damping). */
	bool bHasPivot = false;
	FVector Pivot = FVector::ZeroVector;

	/** Actor the pivot belongs to (ignored by collision when the Collision node asks for it). */
	const AActor* PivotActor = nullptr;

	/**
	 * Smoothed location of the locked target published by the Lock-On node (without its Lock Target Offset).
	 * Group Framing uses it for the Look At subject so framing glides with the camera on target switches.
	 */
	bool bHasLockOnTarget = false;
	FVector LockOnTargetLocation = FVector::ZeroVector;

	/**
	 * Subjects Group Framing fitted (after its Weight rule) and its framing area, published for the constraint
	 * solver (Collision node): it re-checks them after the camera was moved (deoccluder, collision).
	 */
	bool bHasFramingSubjects = false;
	FACFCameraFramingSubjects FramingSubjects;
	FVector2D FramingSize = FVector2D(1.0, 1.0);

	/** Filled by the Collision node's constraint solver. */
	bool bHasSolverReport = false;
	FACFCameraSolverReport SolverReport;

	/** Filled by the Collision node (PostBlend). */
	FACFCameraCollisionConstraint Constraint;

#if !UE_BUILD_SHIPPING
	FACFCameraRigDebug Debug;
#endif
};

/** Inputs handed to every node while a rig is evaluated (stage 3 of the pipeline). */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraNodeContext
{
	FACFCameraNodeContext(const FACFCameraEvaluationContext& InEvaluation, const FACFCameraRequestParams& InRequest)
		: Evaluation(InEvaluation), Request(InRequest)
	{
	}

	const FACFCameraEvaluationContext& Evaluation;
	const FACFCameraRequestParams& Request;

	/** Written by Body / Aim nodes, read by later nodes of the same rig evaluation. */
	mutable FACFCameraRigScratch Scratch;

	/**
	 * Phase 7B-1: this rig belongs to the request in control (the active request, also while it blends in). False for the
	 * outgoing camera of a blend and a released request blending out: they keep evaluating, but camera assistance does not
	 * turn them. Set by the director.
	 */
	bool bInControl = true;

	float GetDeltaTime() const { return Evaluation.DeltaTime; }
};

/**
 * One composable step of a camera rig (Follow, LookAt, Damping, Collision, ...).
 *
 * Nodes are instanced sub-objects of a UACFCameraPreset and therefore SHARED by every player
 * and every request that uses the preset. A node must never write to its own members at
 * runtime. Anything that changes per frame (previous damped location, timers, ...) lives in a
 * runtime state struct declared by GetRuntimeStateType(); the rig instance allocates one copy
 * per request, so local players never share state.
 *
 * Nodes run stage by stage (GetStage), in array order inside a stage, and each one refines the
 * pose produced by the previous one. The first node receives the upstream UE camera pose.
 * Nodes must not advance their state when DeltaTime <= 0 (frame guard contract).
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, BlueprintType)
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode : public UObject
{
	GENERATED_BODY()

public:
	/** Disabled nodes are skipped (handy while tuning a preset). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node")
	bool bEnabled = true;

	virtual EACFCameraNodeStage GetStage() const { return EACFCameraNodeStage::Body; }

	/** Order inside the stage (smaller first; ties keep the array order). Lets a node require running first in its stage. */
	virtual int32 GetStageOrder() const { return 0; }

	/**
	 * Struct type of the per-request runtime state, or null if the node is stateless.
	 * Allocated once when the request is created, never per frame.
	 */
	virtual const UScriptStruct* GetRuntimeStateType() const { return nullptr; }

	/** Called when the owning request becomes the active camera (also after being re-activated). */
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const {}

	/** Teleport / hard cut: drop interpolation history so the next evaluation snaps. */
	virtual void ResetInterpolation(FInstancedStruct* RuntimeState) const {}

	/** Refines InOutPose. RuntimeState is null when GetRuntimeStateType() returns null. */
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const {}
};
