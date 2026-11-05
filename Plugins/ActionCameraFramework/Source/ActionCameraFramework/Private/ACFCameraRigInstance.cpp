#include "ACFCameraRigInstance.h"
#include "ACFCameraNode.h"
#include "ACFCameraPreset.h"

void FACFCameraRigInstance::Initialize(const UACFCameraPreset* InPreset)
{
	Preset = InPreset;
	NodeStates.Reset();
	SyncStatesWithPreset();
}

bool FACFCameraRigInstance::SyncStatesWithPreset()
{
	if (!Preset)
	{
		NodeStates.Reset();
		return false;
	}

	// Normally a no-op (pointer compares only). It reallocates only when the preset was edited
	// while the request is alive (e.g. tuning in PIE), so a node never sees a state of the wrong type.
	bool bChanged = false;
	if (NodeStates.Num() != Preset->Nodes.Num())
	{
		NodeStates.SetNum(Preset->Nodes.Num());
		bChanged = true;
	}
	for (int32 Index = 0; Index < Preset->Nodes.Num(); ++Index)
	{
		const UACFCameraNode* Node = Preset->Nodes[Index];
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
			bChanged = true;
		}
	}
	return bChanged;
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
			Node->Evaluate(Context, InOutPose, State.IsValid() ? &State : nullptr);
		}
	}
}
