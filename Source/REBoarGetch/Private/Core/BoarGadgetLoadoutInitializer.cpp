#include "Core/BoarGadgetLoadoutInitializer.h"
#include "Component/GadgetComponent.h"
#include "Core/BoarGameInstance.h"
#include "Core/BoarFacilityGameMode.h"
#include "Gadget/BoarGadgetSubsystem.h"
#include "Gadget/GadgetBase.h"
#include "Engine/World.h"

FBoarGadgetLoadoutContext BoarGadgetLoadoutInitializer::ResolveContext(UWorld* World)
{
	FBoarGadgetLoadoutContext Context;
	if (!World) return Context;
	const auto* Instance = World->GetGameInstance<UBoarGameInstance>();
	Context.Progress = Instance ? Instance->GetSubsystem<UBoarGadgetSubsystem>() : nullptr;
	const auto* Facility = World->GetAuthGameMode<ABoarFacilityGameMode>();
	Context.bTemporary = Facility && Facility->bGadgetTest;
	if (Context.bTemporary) Context.TestGadgetClass = Facility->TestGadgetClass;
	return Context;
}

bool BoarGadgetLoadoutInitializer::Initialize(UGadgetComponent* Component, const FBoarGadgetLoadoutContext& Context)
{
	if (!Component) return false;
	if (Component->IsLoadoutInitialized()) return true;
	TArray<TSubclassOf<AGadgetBase>> Slots = Component->GetDefaultGadgetSlots();
	Slots.SetNum(UGadgetComponent::GetGadgetSlotCount());
	if (Context.Progress) Context.Progress->RestoreSavedLoadout(Slots);
	if (Context.bTemporary && Context.TestGadgetClass)
	{
		Slots.Init(nullptr, UGadgetComponent::GetGadgetSlotCount());
		Slots[0] = Context.TestGadgetClass;
	}
	return Component->InitializeResolvedLoadout(Slots, Context.Progress, Context.bTemporary);
}
