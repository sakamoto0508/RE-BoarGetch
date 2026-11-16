#include "Stage/BoarLevelDestination.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult UBoarLevelDestination::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Level.IsNull())
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s: Level が未設定です"), *GetName())));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
