#include "Core/BoarGameInstance.h"
#include "Save/BoarSaveGame.h"
#include "Stage/StageConfig.h"
#include "Gadget/GadgetBase.h"
#include "Gadget/BoarGadgetSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Core/BoarFacilityGameMode.h"
#include "Engine/World.h"

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
	if (bNewUnlocks)
 {
  OnGadgetsUnlocked.Broadcast();
  if (auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>()) Gadgets->OnGadgetsUnlocked.Broadcast();
 }
	OnProgressChanged.Broadcast();
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
 const auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>();
 if (!Progress || !Gadgets || !Gadgets->IsLoadoutValid(Ids)) return false;
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

bool UBoarGameInstance::SaveArchiveDisplays(const TArray<FName>& Ids)
{
	const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarFacilityGameMode>() : nullptr;
	if (!Progress || !Mode || !Mode->bArchive) return false;
	auto* Candidate = DuplicateObject<UBoarSaveGame>(Progress, this);
	if (!Candidate->SetArchiveDisplays(Ids) || !SaveCandidate(Candidate)) return false;
	OnArchiveDisplaysChanged.Broadcast();
	return true;
}

TArray<FName> UBoarGameInstance::GetArchiveDisplayIds() const
{
	if (Progress) return Progress->ResolveArchiveDisplays();
	TArray<FName> Empty; Empty.SetNum(3); return Empty;
}

bool UBoarGameInstance::AssignArchiveDisplay(int32 Slot, FName Id)
{
	const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarFacilityGameMode>() : nullptr;
	if (!Progress || !Mode || !Mode->bArchive) return false;
	auto* Candidate = DuplicateObject<UBoarSaveGame>(Progress, this);
	if (!Candidate->AssignArchiveDisplay(Slot, Id) || !SaveCandidate(Candidate)) return false;
	OnArchiveDisplaysChanged.Broadcast();
	return true;
}

bool UBoarGameInstance::IsGadgetUnlocked(FName GadgetId) const
{
 const auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>();
 return Gadgets ? Gadgets->IsGadgetUnlocked(GadgetId) : false;
}
TSubclassOf<AGadgetBase> UBoarGameInstance::FindGadgetClass(FName Id) const
{
 const auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>();
 return Gadgets ? Gadgets->FindGadgetClass(Id) : nullptr;
}
int32 UBoarGameInstance::GetSpecialCoinCount() const
{
 const auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>();
 return Gadgets ? Gadgets->GetSpecialCoinCount() : 0;
}
bool UBoarGameInstance::CanUnlockGadget(FName Id) const
{
 const auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>();
 return Gadgets ? Gadgets->CanUnlockGadget(Id) : false;
}
bool UBoarGameInstance::IsInGadgetLab() const
{
 const auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>();
 return Gadgets ? Gadgets->IsInGadgetLab() : false;
}
FName UBoarGameInstance::GetSelectedTestGadget() const
{
 const auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>();
 return Gadgets ? Gadgets->GetSelectedTestGadget() : NAME_None;
}
TSoftObjectPtr<UWorld> UBoarGameInstance::GetSelectedTestLevel() const
{
 const auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>();
 return Gadgets ? Gadgets->GetSelectedTestLevel() : TSoftObjectPtr<UWorld>();
}
bool UBoarGameInstance::UnlockGadgetInLab(FName Id)
{
 auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>();
 return Gadgets && Gadgets->UnlockGadgetInLab(Id);
}
bool UBoarGameInstance::SelectTestGadget(FName Id)
{
 auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>();
 return Gadgets && Gadgets->SelectTestGadget(Id);
}
void UBoarGameInstance::ClearTestGadget()
{
 if (auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>()) Gadgets->ClearTestGadget();
}
void UBoarGameInstance::UpdateUnlockedGadgets(UBoarSaveGame* Candidate) const
{
 if (const auto* Gadgets = GetSubsystem<UBoarGadgetSubsystem>()) Gadgets->UpdateInitialUnlocks(Candidate);
}
