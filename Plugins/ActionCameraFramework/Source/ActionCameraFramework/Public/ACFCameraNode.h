#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACFCameraTypes.h"
#include "ACFCameraNode.generated.h"

struct FInstancedStruct;

/** Read-only inputs handed to every node while a rig is evaluated (stage 3 of the pipeline). */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraNodeContext
{
	FACFCameraNodeContext(const FACFCameraEvaluationContext& InEvaluation, const FACFCameraRequestParams& InRequest)
		: Evaluation(InEvaluation), Request(InRequest)
	{
	}

	const FACFCameraEvaluationContext& Evaluation;
	const FACFCameraRequestParams& Request;

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
 * Nodes run in array order and each one refines the pose produced by the previous one.
 * The first node receives the upstream UE camera pose, so a rig only has to override what it
 * cares about.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, BlueprintType)
class ACTIONCAMERAFRAMEWORK_API UACFCameraNode : public UObject
{
	GENERATED_BODY()

public:
	/** Disabled nodes are skipped (handy while tuning a preset). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Node")
	bool bEnabled = true;

	/**
	 * Struct type of the per-request runtime state, or null if the node is stateless.
	 * Allocated once when the request is created, never per frame.
	 */
	virtual const UScriptStruct* GetRuntimeStateType() const { return nullptr; }

	/** Called when the owning request becomes the active camera (also after being re-activated). */
	virtual void OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const {}

	/** Refines InOutPose. RuntimeState is null when GetRuntimeStateType() returns null. */
	virtual void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const {}
};
