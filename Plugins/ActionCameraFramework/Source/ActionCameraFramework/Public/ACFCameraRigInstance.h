#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "ACFCameraTypes.h"
#include "ACFCameraRigInstance.generated.h"

class UACFCameraPreset;
struct FACFCameraNodeContext;

/**
 * Runtime counterpart of a UACFCameraPreset: one per camera request.
 *
 * Holds the mutable per-node state (damping history, timers, ...) so that the preset and its
 * nodes stay immutable and shareable. State buffers are allocated once in Initialize(); the
 * per-frame Evaluate() does not allocate.
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

	/** Runs the node stack. InOutPose enters as the seed (upstream UE pose) and leaves as the rig output. */
	void Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose);

private:
	/** Matches NodeStates to the preset's current node list. Returns true if anything was reallocated. */
	bool SyncStatesWithPreset();

	UPROPERTY()
	TObjectPtr<const UACFCameraPreset> Preset = nullptr;

	/** Parallel to Preset->Nodes. Empty entries for stateless nodes. */
	UPROPERTY()
	TArray<FInstancedStruct> NodeStates;
};
