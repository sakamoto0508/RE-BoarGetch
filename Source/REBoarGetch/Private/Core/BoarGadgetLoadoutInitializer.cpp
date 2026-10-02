#include "Core/BoarGadgetLoadoutInitializer.h"
#include "Component/GadgetComponent.h"
#include "Core/BoarGameInstance.h"
#include "Core/BoarGameMode.h"
#include "Core/BoarFacilityGameMode.h"
#include "Gadget/BoarGadgetSubsystem.h"
#include "Player/BoarPlayerCharacter.h"
#include "Engine/World.h"

void BoarGadgetLoadoutInitializer::Initialize(UGadgetComponent *Component, bool bAllowStageDeferral)
{
	if (!Component || !Component->GetWorld() || Component->IsLoadoutInitialized())
		return;
	UWorld *World = Component->GetWorld();
	const auto *Stage = World->GetAuthGameMode<ABoarGameMode>();
	if (bAllowStageDeferral && Cast<ABoarPlayerCharacter>(Component->GetOwner()) && Stage &&
		Stage->GetStageState() == EBoarStageState::Preparing)
		return;

	const auto *Instance = World->GetGameInstance<UBoarGameInstance>();
	auto *Progress = Instance ? Instance->GetSubsystem<UBoarGadgetSubsystem>() : nullptr;
	TArray<TSubclassOf<AGadgetBase>> Slots = Component->GetDefaultGadgetSlots();
	Slots.SetNum(UGadgetComponent::GetGadgetSlotCount());
	if (Progress)
		Progress->RestoreSavedLoadout(Slots);
	const auto *Facility = World->GetAuthGameMode<ABoarFacilityGameMode>();
	const bool bTemporary = Facility && Facility->bGadgetTest;
	if (bTemporary && Facility->TestGadgetClass)
	{
		Slots.Init(nullptr, UGadgetComponent::GetGadgetSlotCount());
		Slots[0] = Facility->TestGadgetClass;
	}
	Component->InitializeResolvedLoadout(Slots, Progress, bTemporary);
}
