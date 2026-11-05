#include "ACFCameraPreset.h"
#include "ACFCameraNode.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

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
