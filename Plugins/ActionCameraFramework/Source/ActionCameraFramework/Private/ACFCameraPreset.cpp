#include "ACFCameraPreset.h"
#include "ACFCameraNode.h"

#if WITH_EDITOR
#include "ACFCameraPresetValidation.h"
#include "Misc/DataValidation.h"

// Phase 6B / B-8: the checks live in ACFCameraPresetValidation (one implementation shared with sequences and tests).
// Data Validation calls this on save, from "Validate Assets" and from the DataValidation commandlet.
EDataValidationResult UACFCameraPreset::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Result = Super::IsDataValid(Context);
	TArray<FACFCameraPresetIssue> Issues;
	ACFCameraPresetValidation::ValidatePreset(*this, Issues);
	return CombineDataValidationResults(Result, ACFCameraPresetValidation::Report(Issues, Context));
}
#endif
