#include "Core/BoarGameInstance.h"
#include "Save/BoarSaveGame.h"
#include "Stage/StageConfig.h"
#include "Gadget/GadgetBase.h"
#include "GadgetDataAsset.h"
#include "Kismet/GameplayStatics.h"

void UBoarGameInstance::Init()
{
	Super::Init();
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
	{
		Progress = Cast<UBoarSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));
		bSaveLoadFailed = !Progress;
	}
	if (!Progress) Progress = NewObject<UBoarSaveGame>(this);
	UpdateUnlockedGadgets(Progress);
	if (bSaveLoadFailed) UE_LOG(LogTemp, Error, TEXT("[Save] Existing save could not be loaded; overwriting is disabled."));
}

bool UBoarGameInstance::SaveCandidate(UBoarSaveGame* Candidate)
{
	if (!Candidate || bSaveLoadFailed || !UGameplayStatics::SaveGameToSlot(Candidate, SaveSlotName, SaveUserIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("[Save] Progress could not be saved."));
		return false;
	}
	const bool bNewUnlocks = !Progress || Candidate->UnlockedGadgetIds.Num() > Progress->UnlockedGadgetIds.Num();
	Progress = Candidate;
	if (bNewUnlocks) OnGadgetsUnlocked.Broadcast();
	return true;
}

bool UBoarGameInstance::CommitClearedRun(FStageRunData& Run)
{
	if (!Progress || Run.StageId.IsNone()) return false;
	Progress->FreezeRunResult(Run);
	UBoarSaveGame* Candidate = DuplicateObject<UBoarSaveGame>(Progress, this);
	if (!Candidate->MergeClearedRun(Run)) return false;
	UpdateUnlockedGadgets(Candidate);
	return SaveCandidate(Candidate);
}

bool UBoarGameInstance::SaveLastAttemptedStage(FName StageId)
{
	if (!Progress || StageId.IsNone()) return false;
	UBoarSaveGame* Candidate = DuplicateObject<UBoarSaveGame>(Progress, this);
	Candidate->LastAttemptedStageId = StageId;
	return SaveCandidate(Candidate);
}

bool UBoarGameInstance::SaveGadgetLoadout(const TArray<FName>& Ids)
{
	if (!Progress || Ids.Num() != 4) return false;
	TSet<FName> Used;
	for (FName Id : Ids)
	{
		if (Id.IsNone()) continue;
		if (!IsGadgetUnlocked(Id) || !FindGadgetClass(Id) || Used.Contains(Id)) return false;
		Used.Add(Id);
	}
	UBoarSaveGame* Candidate = DuplicateObject<UBoarSaveGame>(Progress, this);
	Candidate->GadgetLoadout = Ids;
	Candidate->bHasSavedLoadout = true;
	return SaveCandidate(Candidate);
}

bool UBoarGameInstance::IsStageUnlocked(const UStageConfig* Config) const
{
	return Config && (Config->UnlockCondition.RequiredClearedStageId.IsNone()
		|| (Progress && Progress->ClearedStageIds.Contains(Config->UnlockCondition.RequiredClearedStageId)));
}

bool UBoarGameInstance::IsGadgetUnlocked(FName GadgetId) const
{
	return Progress && !GadgetId.IsNone() && Progress->UnlockedGadgetIds.Contains(GadgetId);
}

TSubclassOf<AGadgetBase> UBoarGameInstance::FindGadgetClass(FName Id) const
{
	if (Id.IsNone()) return nullptr;
	for (const TSubclassOf<AGadgetBase>& Class : GadgetCatalog)
	{
		const UGadgetDataAsset* Def = Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
		if (Def && Def->GadgetId == Id) return Class;
	}
	return nullptr;
}

void UBoarGameInstance::UpdateUnlockedGadgets(UBoarSaveGame* Candidate) const
{
	for (const TSubclassOf<AGadgetBase>& Class : GadgetCatalog)
	{
		const UGadgetDataAsset* Def = Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
		if (Def && !Def->GadgetId.IsNone() && (Def->RequiredClearedStageId.IsNone()
			|| Candidate->ClearedStageIds.Contains(Def->RequiredClearedStageId)))
			Candidate->UnlockedGadgetIds.Add(Def->GadgetId);
	}
}
