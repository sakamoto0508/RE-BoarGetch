#include "ACFCameraRigInstance.h"
#include "ACFCameraNode.h"
#include "ACFCameraPreset.h"
#include "Algo/StableSort.h"

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
		return Nodes[Index] ? static_cast<int64>(Nodes[Index]->GetStage()) * 1000000 + FMath::Clamp(Nodes[Index]->GetStageOrder(), -400000, 400000) : 0;
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
	for (int32 Position = Begin; Position < End; ++Position)
	{
		const int32 Index = Order[Position];
		const UACFCameraNode* Node = Nodes[Index];
		if (Node && Node->bEnabled)
		{
			FInstancedStruct& State = NodeStates[Index];
			Node->Evaluate(Context, InOutPose, State.IsValid() ? &State : nullptr);
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
