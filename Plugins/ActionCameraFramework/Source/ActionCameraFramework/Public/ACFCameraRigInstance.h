#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "ACFCameraTypes.h"
#include "ACFCameraRigInstance.generated.h"

class UACFCameraNode;
class UACFCameraPreset;
struct FACFCameraNodeContext;

/**
 * Runtime counterpart of a UACFCameraPreset: one per camera request.
 *
 * Holds the mutable per-node state (damping history, orbit angles, ...) so that the preset and its
 * nodes stay immutable and shareable. State buffers are allocated once in Initialize(); the
 * per-frame Evaluate() does not allocate.
 *
 * Nodes run stage by stage: Body -> Aim (Evaluate), PostBlend (EvaluatePostBlend, called by the
 * director once on the final blended pose). Array order is kept inside a stage.
 */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraRigInstance
{
	GENERATED_BODY()

	void Initialize(const UACFCameraPreset* InPreset);
	bool IsInitialized() const { return Preset != nullptr; }
	const UACFCameraPreset* GetPreset() const { return Preset; }

	/** Called each time the request becomes the active camera. */
	void Activate(const FACFCameraNodeContext& Context);

	/** Body + Aim stages. InOutPose enters as the seed (upstream UE pose) and leaves as the rig output. */
	void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose);

	/** PostBlend stage on the final (blended) pose. Returns false if the preset has no such node. */
	bool EvaluatePostBlend(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose);

	bool HasPostBlendNodes() const { return PostBlendCount > 0; }

	/** Teleport / hard cut: every node drops its interpolation history. */
	void ResetInterpolation();

	/** Visits every enabled node with its runtime state (e.g. to push a runtime command). */
	void ForEachNode(TFunctionRef<void(const UACFCameraNode& Node, FInstancedStruct* State)> Visitor);

	/** GetStageOrder() beyond this is clamped (the stage always comes first). */
	static constexpr int32 MaxStageOrder = 400000;

	/** Evaluation order of a node: by stage, then GetStageOrder(); a stable sort keeps the array order of ties. Null = 0. */
	static int64 GetEvaluationSortKey(const UACFCameraNode* Node);

private:
	/** Matches NodeStates / order to the preset's current node list. Allocates only after edits. */
	void SyncStatesWithPreset();
	void RunNodes(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, int32 Begin, int32 End);

	UPROPERTY()
	TObjectPtr<const UACFCameraPreset> Preset = nullptr;

	/** Parallel to Preset->Nodes. Empty entries for stateless nodes. */
	UPROPERTY()
	TArray<FInstancedStruct> NodeStates;

	/** Node indices sorted by stage (stable). [0, PreBlendCount) = Body + Aim, rest = PostBlend. */
	TArray<int32> Order;
	TArray<TWeakObjectPtr<const UACFCameraNode>> OrderedNodes;
	int32 PreBlendCount = 0;
	int32 PostBlendCount = 0;
};
