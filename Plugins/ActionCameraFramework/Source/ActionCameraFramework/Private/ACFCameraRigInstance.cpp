#include "ACFCameraRigInstance.h"
#include "ACFCameraNode.h"
#include "ACFCameraPreset.h"
#include "Algo/StableSort.h"
#include "ACFCameraProfiler.h"
#include "ACFCameraHealth.h"

namespace ACFRigHealth
{
	/**
	 * Phase 6B / B-4: a node turned a valid pose invalid. Drop the invalid components (keep the pose from before the node:
	 * "reject the node's output" per component) and, if location / rotation were invalid, reset the node's interpolation
	 * state (its damping history may hold the invalid value; without the reset it would never recover).
	 */
	static void RejectInvalidNodeOutput(const UACFCameraNode& Node, FInstancedStruct* State, const FACFCameraPose& Before, FACFCameraPose& InOutPose)
	{
		const bool bLocation = !ACFCameraValidation::IsFinite(InOutPose.Location);
		const bool bRotation = !ACFCameraValidation::IsFinite(InOutPose.Rotation);
		const bool bFov = !ACFCameraValidation::IsValidFov(InOutPose.FieldOfView);
		if (bLocation)
		{
			InOutPose.Location = Before.Location;
		}
		if (bRotation)
		{
			InOutPose.Rotation = Before.Rotation;
		}
		if (bFov)
		{
			InOutPose.FieldOfView = Before.FieldOfView;
		}
		if (bLocation || bRotation)
		{
			Node.ResetInterpolation(State);
		}
		const TCHAR* Parameter = bLocation ? (bRotation ? TEXT("location + rotation") : TEXT("location")) : (bRotation ? TEXT("rotation") : TEXT("field of view"));
		FACFCameraHealth::ReportActive(EACFCameraHealthIssue::InvalidNodeOutput, Node.GetClass()->GetFName(), Parameter,
			(bLocation || bRotation) ? TEXT("node output dropped for the invalid components, node interpolation reset") : TEXT("node field of view dropped"));
	}
}

void FACFCameraRigInstance::Initialize(const UACFCameraPreset* InPreset)
{
	Preset = InPreset;
	NodeStates.Reset();
	Order.Reset();
	OrderedNodes.Reset();
	SyncStatesWithPreset();
}

void FACFCameraRigInstance::SyncStatesWithPreset()
{
	if (!Preset)
	{
		NodeStates.Reset();
		Order.Reset();
		OrderedNodes.Reset();
		PreBlendCount = PostBlendCount = 0;
		return;
	}

	// Normally pointer compares only. Reallocates only when the preset was edited while the request
	// is alive (e.g. tuning in PIE), so a node never sees a state of the wrong type.
	const TArray<TObjectPtr<UACFCameraNode>>& Nodes = Preset->Nodes;
	bool bOrderDirty = NodeStates.Num() != Nodes.Num() || OrderedNodes.Num() != Nodes.Num();
	if (NodeStates.Num() != Nodes.Num())
	{
		NodeStates.SetNum(Nodes.Num());
	}
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const UACFCameraNode* Node = Nodes[Index];
		const UScriptStruct* Wanted = Node ? Node->GetRuntimeStateType() : nullptr;
		FInstancedStruct& State = NodeStates[Index];
		if (State.GetScriptStruct() != Wanted)
		{
			if (Wanted)
			{
				State.InitializeAs(Wanted);
			}
			else
			{
				State.Reset();
			}
			bOrderDirty = true;
		}
		if (!bOrderDirty && Order.IsValidIndex(Index) && OrderedNodes[Index].Get() != Nodes[Order[Index]])
		{
			bOrderDirty = true;
		}
	}
	if (!bOrderDirty)
	{
		return;
	}

	Order.Reset(Nodes.Num());
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		Order.Add(Index);
	}
	auto StageOf = [&Nodes](int32 Index) -> int64
	{
		return GetEvaluationSortKey(Nodes[Index]);
	};
	Algo::StableSortBy(Order, StageOf);
	OrderedNodes.Reset(Nodes.Num());
	PreBlendCount = 0;
	for (const int32 Index : Order)
	{
		OrderedNodes.Add(Nodes[Index].Get());
		if (!Nodes[Index] || Nodes[Index]->GetStage() != EACFCameraNodeStage::PostBlend)
		{
			++PreBlendCount;
		}
	}
	PostBlendCount = Order.Num() - PreBlendCount;
}

void FACFCameraRigInstance::RunNodes(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, int32 Begin, int32 End)
{
	const TArray<TObjectPtr<UACFCameraNode>>& Nodes = Preset->Nodes;
#if ACF_WITH_PROFILER
	// Every node is measured here, in the one loop that runs nodes, so new node classes need no profiling code.
	FACFCameraProfiler* const Profiler = FACFCameraProfiler::GetActive();
#endif
	// B-4: only a node that turns a valid pose invalid is blamed (an invalid input pose is handled by the director).
	bool bPoseValid = ACFCameraValidation::IsValidPose(InOutPose);
	for (int32 Position = Begin; Position < End; ++Position)
	{
		const int32 Index = Order[Position];
		const UACFCameraNode* Node = Nodes[Index];
		if (Node && Node->bEnabled)
		{
			FInstancedStruct& State = NodeStates[Index];
			// Unreal Insights: one event per node class (only emitted while the CPU trace channel is on).
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Node->GetClass()->GetFName());
#if ACF_WITH_PROFILER
			const uint64 Start = Profiler ? FPlatformTime::Cycles64() : 0;
#endif
			const FACFCameraPose Before = InOutPose;
			Node->Evaluate(Context, InOutPose, State.IsValid() ? &State : nullptr);
			if (bPoseValid && !ACFCameraValidation::IsValidPose(InOutPose))
			{
				ACFRigHealth::RejectInvalidNodeOutput(*Node, State.IsValid() ? &State : nullptr, Before, InOutPose);
			}
			else if (!bPoseValid)
			{
				bPoseValid = ACFCameraValidation::IsValidPose(InOutPose); // a node may produce a valid pose from an invalid input
			}
#if ACF_WITH_PROFILER
			if (Profiler)
			{
				Profiler->AddNode(Node->GetClass()->GetFName(), static_cast<uint8>(Node->GetStage()), FPlatformTime::Cycles64() - Start);
			}
#endif
		}
	}
}

void FACFCameraRigInstance::Activate(const FACFCameraNodeContext& Context)
{
	SyncStatesWithPreset();
	if (!Preset)
	{
		return;
	}
	for (int32 Index = 0; Index < Preset->Nodes.Num(); ++Index)
	{
		const UACFCameraNode* Node = Preset->Nodes[Index];
		if (Node && Node->bEnabled)
		{
			FInstancedStruct& State = NodeStates[Index];
			Node->OnActivated(Context, State.IsValid() ? &State : nullptr);
		}
	}
}

void FACFCameraRigInstance::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose)
{
	SyncStatesWithPreset();
	if (Preset)
	{
		RunNodes(Context, InOutPose, 0, PreBlendCount);
	}
}

bool FACFCameraRigInstance::EvaluatePostBlend(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose)
{
	SyncStatesWithPreset();
	if (!Preset || PostBlendCount == 0)
	{
		return false;
	}
	RunNodes(Context, InOutPose, PreBlendCount, Order.Num());
	return true;
}

void FACFCameraRigInstance::ResetInterpolation()
{
	ForEachNode([](const UACFCameraNode& Node, FInstancedStruct* State) { Node.ResetInterpolation(State); });
}

bool FACFCameraRigInstance::RebindPreset(const UACFCameraPreset* NewPreset)
{
	bool bSameLayout = NewPreset != nullptr && Preset != nullptr && NewPreset->Nodes.Num() == NodeStates.Num();
	for (int32 Index = 0; bSameLayout && Index < NewPreset->Nodes.Num(); ++Index)
	{
		const UACFCameraNode* Node = NewPreset->Nodes[Index];
		bSameLayout = NodeStates[Index].GetScriptStruct() == (Node ? Node->GetRuntimeStateType() : nullptr);
	}
	Preset = NewPreset;
	SyncStatesWithPreset(); // same layout: only the evaluation order is rebuilt (node pointers changed), states stay
	return bSameLayout;
}

int64 FACFCameraRigInstance::GetEvaluationSortKey(const UACFCameraNode* Node)
{
	return Node ? static_cast<int64>(Node->GetStage()) * 1000000 + FMath::Clamp(Node->GetStageOrder(), -MaxStageOrder, MaxStageOrder) : 0;
}

void FACFCameraRigInstance::ForEachNode(TFunctionRef<void(const UACFCameraNode& Node, FInstancedStruct* State)> Visitor)
{
	SyncStatesWithPreset();
	if (!Preset)
	{
		return;
	}
	for (int32 Index = 0; Index < Preset->Nodes.Num(); ++Index)
	{
		if (const UACFCameraNode* Node = Preset->Nodes[Index]; Node && Node->bEnabled)
		{
			FInstancedStruct& State = NodeStates[Index];
			Visitor(*Node, State.IsValid() ? &State : nullptr);
		}
	}
}
