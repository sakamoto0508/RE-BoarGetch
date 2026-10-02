#include "Gadget/BoarGadgetSubsystem.h"
#include "Core/BoarGameInstance.h"
#include "Core/BoarFacilityGameMode.h"
#include "Save/BoarSaveGame.h"
#include "Gadget/GadgetBase.h"
#include "GadgetDataAsset.h"
#include "Component/GadgetComponent.h"
#include "Engine/World.h"

UBoarGameInstance *UBoarGadgetSubsystem::GetProgressOwner() const
{
	return Cast<UBoarGameInstance>(GetGameInstance());
}
const TArray<TSubclassOf<AGadgetBase>> &UBoarGadgetSubsystem::GetCatalog() const
{
	const auto *Owner = GetProgressOwner();
	static const TArray<TSubclassOf<AGadgetBase>> Empty;
	return Owner ? Owner->GadgetCatalog : Empty;
}
bool UBoarGadgetSubsystem::IsGadgetUnlocked(FName GadgetId) const
{
	const auto *Owner = GetProgressOwner();
	const auto *Progress = Owner ? Owner->GetProgress() : nullptr;
	return Progress && !GadgetId.IsNone() && Progress->UnlockedGadgetIds.Contains(GadgetId);
}

TSubclassOf<AGadgetBase> UBoarGadgetSubsystem::FindGadgetClass(FName Id) const
{
	if (Id.IsNone())
		return nullptr;
	for (const TSubclassOf<AGadgetBase> &Class : GetCatalog())
	{
		const UGadgetDataAsset *Def = Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
		if (Def && Def->GadgetId == Id)
			return Class;
	}
	return nullptr;
}

void UBoarGadgetSubsystem::UpdateInitialUnlocks(UBoarSaveGame *Candidate) const
{
	if (!Candidate)
		return;
	for (const TSubclassOf<AGadgetBase> &Class : GetCatalog())
	{
		const UGadgetDataAsset *Def = Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
		if (Def && !Def->GadgetId.IsNone() && Def->bInitiallyUnlocked)
			Candidate->UnlockedGadgetIds.Add(Def->GadgetId);
	}
}

bool UBoarGadgetSubsystem::IsInGadgetLab() const
{
	const auto *Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarFacilityGameMode>() : nullptr;
	return Mode && !Mode->bArchive && !Mode->bGadgetTest;
}
int32 UBoarGadgetSubsystem::GetSpecialCoinCount() const
{
	const auto *Owner = GetProgressOwner();
	const auto *Progress = Owner ? Owner->GetProgress() : nullptr;
	return Progress ? Progress->SpecialCoinIds.Num() : 0;
}
bool UBoarGadgetSubsystem::CanUnlockGadget(FName Id) const
{
	const auto *Owner = GetProgressOwner();
	const auto *Progress = Owner ? Owner->GetProgress() : nullptr;
	const auto Class = FindGadgetClass(Id);
	const auto *Def = Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
	return Progress && Def && !IsGadgetUnlocked(Id) && GetSpecialCoinCount() >= FMath::Max(0, Def->RequiredSpecialCoinCount);
}
bool UBoarGadgetSubsystem::UnlockGadgetInLab(FName Id)
{
	const auto *Owner = GetProgressOwner();
	const auto *Progress = Owner ? Owner->GetProgress() : nullptr;
	if (!IsInGadgetLab() || !CanUnlockGadget(Id))
		return false;
	auto *Candidate = DuplicateObject<UBoarSaveGame>(Progress, GetProgressOwner());
	Candidate->UnlockedGadgetIds.Add(Id);
	return GetProgressOwner()->SaveCandidate(Candidate); // Coin IDs are never removed; publish only after save succeeds.
}
bool UBoarGadgetSubsystem::SelectTestGadget(FName Id)
{
	if (!IsInGadgetLab() || (!Id.IsNone() && (!FindGadgetClass(Id) || !IsGadgetUnlocked(Id))))
		return false;
	SelectedTestGadget = Id;
	GetProgressOwner()->OnTestGadgetChanged.Broadcast();
	OnTestGadgetChanged.Broadcast();
	return true;
}
TSoftObjectPtr<UWorld> UBoarGadgetSubsystem::GetSelectedTestLevel() const
{
	const auto Class = FindGadgetClass(SelectedTestGadget);
	const auto *Def = Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
	return Def && IsGadgetUnlocked(SelectedTestGadget) ? Def->TestLevel : TSoftObjectPtr<UWorld>();
}

void UBoarGadgetSubsystem::ClearTestGadget()
{
	if (SelectedTestGadget.IsNone())
		return;
	SelectedTestGadget = NAME_None;
	GetProgressOwner()->OnTestGadgetChanged.Broadcast();
	OnTestGadgetChanged.Broadcast();
}

bool UBoarGadgetSubsystem::IsLoadoutValid(const TArray<FName> &Ids) const
{
	if (Ids.Num() != UGadgetComponent::GetGadgetSlotCount())
		return false;
	TSet<FName> Used;
	for (FName Id : Ids)
	{
		if (Id.IsNone())
			continue;
		if (!IsGadgetUnlocked(Id) || !FindGadgetClass(Id) || Used.Contains(Id))
			return false;
		Used.Add(Id);
	}
	return true;
}

void UBoarGadgetSubsystem::RestoreSavedLoadout(TArray<TSubclassOf<AGadgetBase>> &Slots) const
{
	const auto *Owner = GetProgressOwner();
	const auto *Save = Owner ? Owner->GetProgress() : nullptr;
	if (!Save || !Save->bHasSavedLoadout || Save->GadgetLoadout.Num() != UGadgetComponent::GetGadgetSlotCount())
		return;
	TArray<TSubclassOf<AGadgetBase>> Restored;
	bool bCanRestore = true;
	for (FName Id : Save->GadgetLoadout)
	{
		TSubclassOf<AGadgetBase> Class = FindGadgetClass(Id);
		if (!Id.IsNone() && !Class)
			bCanRestore = false;
		Restored.Add(Class);
	}
	if (bCanRestore)
		Slots = Restored;
	else
		UE_LOG(LogTemp, Error, TEXT("[Gadget] Saved loadout has unresolved IDs; defaults retained, save unchanged."));
}
bool UBoarGadgetSubsystem::CanEquipGadgetClass(TSubclassOf<AGadgetBase> Class) const
{
	const auto *Definition = Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
	return !Definition || Definition->GadgetId.IsNone() || IsGadgetUnlocked(Definition->GadgetId);
}
bool UBoarGadgetSubsystem::SaveLoadoutClasses(const TArray<TSubclassOf<AGadgetBase>> &Slots)
{
	auto *Owner = GetProgressOwner();
	if (!Owner)
		return false;
	TArray<FName> Ids;
	for (const auto &Class : Slots)
	{
		const auto *Def = Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
		if (Class && (!Def || Def->GadgetId.IsNone()))
			return false;
		Ids.Add(Def ? Def->GadgetId : NAME_None);
	}
	return Owner->SaveGadgetLoadout(Ids);
}
