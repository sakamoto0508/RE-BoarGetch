#include "ACFCameraPreset.h"
#include "ACFCameraNode.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_Composers.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_GroupFraming.h"
#include "Nodes/ACFCameraNode_LockOn.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"

#define LOCTEXT_NAMESPACE "ACFCameraPreset"

EDataValidationResult UACFCameraPreset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (!Nodes[Index])
		{
			Context.AddWarning(FText::Format(LOCTEXT("EmptyNode", "Node slot {0} is empty and will be skipped."), Index));
		}
	}
	// Responsibility conflicts (see README "Node の評価順序").
	int32 PositionOwners = 0;
	int32 AimNodes = 0;
	int32 Collisions = 0;
	int32 GroupFramings = 0;
	bool bPositionComposer = false;
	for (const UACFCameraNode* Node : Nodes)
	{
		if (!Node || !Node->bEnabled)
		{
			continue;
		}
		const bool bIsPositionComposer = Node->IsA<UACFCameraNode_PositionComposer>();
		bPositionComposer |= bIsPositionComposer;
		PositionOwners += (Node->IsA<UACFCameraNode_Follow>() || Node->IsA<UACFCameraNode_OrbitalFollow>() || Node->IsA<UACFCameraNode_LockOn>() || bIsPositionComposer) ? 1 : 0;
		AimNodes += (Node->IsA<UACFCameraNode_LookAt>() || Node->IsA<UACFCameraNode_RotationComposer>()) ? 1 : 0;
		Collisions += Node->IsA<UACFCameraNode_Collision>() ? 1 : 0;
		if (const UACFCameraNode_GroupFraming* Framing = Cast<UACFCameraNode_GroupFraming>(Node))
		{
			++GroupFramings;
			if (!Framing->bFrameTargetGroup && !Framing->FollowTargetSubject.bInclude && !Framing->LookAtTargetSubject.bInclude)
			{
				Context.AddWarning(LOCTEXT("FramingNoSubjects", "Group Framing has no subjects (Frame Target Group, Follow Target Subject and Look At Target Subject are all off); it does nothing."));
			}
		}
	}
	if (PositionOwners > 1)
	{
		Context.AddWarning(LOCTEXT("PositionOwners", "More than one enabled position node (Follow / Orbital Follow / Lock-On / Position Composer): the last Body node wins and the others only waste time and state."));
	}
	if (AimNodes > 1)
	{
		Context.AddWarning(LOCTEXT("AimNodes", "More than one enabled Aim node (Look At / Rotation Composer): the last one decides the rotation."));
	}
	if (bPositionComposer && AimNodes > 0)
	{
		Context.AddWarning(LOCTEXT("ComposerAim", "Position Composer frames with the incoming rotation; an Aim node re-rotates afterwards and breaks that framing."));
	}
	if (GroupFramings > 1)
	{
		Context.AddWarning(LOCTEXT("GroupFramings", "More than one Group Framing node: each one moves / zooms the camera again."));
	}
	if (Collisions > 1)
	{
		Context.AddWarning(LOCTEXT("Collisions", "More than one Collision node: each one sweeps again."));
	}

	if (Nodes.IsEmpty())
	{
		Context.AddWarning(LOCTEXT("NoNodes", "Preset has no nodes; it will reproduce the upstream UE camera unchanged."));
	}
	if (DefaultBlendIn.Curve == EACFCameraBlendCurve::Custom && !DefaultBlendIn.CustomCurve)
	{
		Context.AddWarning(LOCTEXT("NoBlendInCurve", "DefaultBlendIn uses Custom but has no curve; Linear will be used."));
	}
	if (DefaultBlendOut.Curve == EACFCameraBlendCurve::Custom && !DefaultBlendOut.CustomCurve)
	{
		Context.AddWarning(LOCTEXT("NoBlendOutCurve", "DefaultBlendOut uses Custom but has no curve; Linear will be used."));
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
