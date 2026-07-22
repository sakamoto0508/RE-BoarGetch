#include "Component/GadgetComponent.h"

#include "Engine/World.h"
#include "Gadget/GadgetBase.h"
#include "GameFramework/Pawn.h"


// Sets default values for this component's properties
UGadgetComponent::UGadgetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	EquippedGadgetSlots.SetNum(MaxGadgetSlots);
}


void UGadgetComponent::BeginPlay()
{
	Super::BeginPlay();

	OwningPawn = Cast<APawn>(GetOwner());
	UE_LOG(LogTemp, Log, TEXT("[Gadget] Owner pawn: %s"), *GetNameSafe(OwningPawn.Get()));

	InitializeDefaultSlots();

	const int32 FirstSlot = FindFirstValidSlot();
	if (FirstSlot != INDEX_NONE)
	{
		const bool bEquipped = SwitchGadgetBySlot(FirstSlot);
		UE_LOG(LogTemp, Log, TEXT("[Gadget] Initial slot %d equip: %s"),
			FirstSlot, bEquipped ? TEXT("success") : TEXT("failed"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Gadget] No valid gadget slot is configured"));
	}
}

/** ガジェットを装備します。 */
bool UGadgetComponent::EquipGadget(TSubclassOf<AGadgetBase> GadgetClass)
{
	if (GadgetClass == nullptr || !IsValid(OwningPawn))
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (World == nullptr) return false;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwningPawn;
	SpawnParams.Instigator = OwningPawn->GetInstigator();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AGadgetBase* NewGadget = World->SpawnActor<AGadgetBase>
		(GadgetClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	if (NewGadget == nullptr) return false;

	// Spawn に失敗した場合も現在の装備を失わないよう、新しい Actor の生成後に入れ替える。
	UnequipGadget();
	CurrentGadget = NewGadget;
	CurrentGadget->AttachToActor(OwningPawn.Get(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	return true;
}

/**
 * 現在のガチャメカを外します。
 */
void UGadgetComponent::UnequipGadget()
{
	if (IsValid(CurrentGadget))
	{
		CurrentGadget->Destroy();
	}
	CurrentGadget = nullptr;
	CurrentGadgetSlotIndex = INDEX_NONE;
}

/**
 * 現在装備中のガチャメカを使います。
 */
bool UGadgetComponent::UseCurrentGadget()
{
	// 旧APIは互換維持のため開始APIへ委譲する。
	return BeginUseCurrentGadget();
}

/**
 * 現在装備中のガジェット使用を開始します。
 */
bool UGadgetComponent::BeginUseCurrentGadget()
{
	if (!IsValid(CurrentGadget))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Gadget] BeginUse failed: CurrentGadget is null"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[Gadget] BeginUse: %s"), *GetNameSafe(CurrentGadget.Get()));
	CurrentGadget->BeginUse(OwningPawn.Get());
	return true;
}

/**
 * 現在装備中のガジェット使用を終了します。
 */
bool UGadgetComponent::EndUseCurrentGadget()
{
	if (!IsValid(CurrentGadget))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Gadget] EndUse failed: CurrentGadget is null"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[Gadget] EndUse: %s"), *GetNameSafe(CurrentGadget.Get()));
	CurrentGadget->EndUse(OwningPawn.Get());
	return true;
}

/**
 * ガチャメカを切り替えます。
 */
bool UGadgetComponent::SwitchGadget(TSubclassOf<AGadgetBase> NewGadgetClass)
{
	if (NewGadgetClass == nullptr) return false;
	
	return EquipGadget(NewGadgetClass);
}

bool UGadgetComponent::SetGadgetSlot(int32 SlotIndex, TSubclassOf<AGadgetBase> GadgetClass)
{
	if (!IsValidSlotIndex(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Gadget] Invalid slot index: %d"), SlotIndex);
		return false;
	}

	EquippedGadgetSlots[SlotIndex] = GadgetClass;
	return true;
}

bool UGadgetComponent::SwitchGadgetBySlot(int32 SlotIndex)
{
	if (!IsValidSlotIndex(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Gadget] Switch failed: invalid slot index %d"), SlotIndex);
		return false;
	}

	TSubclassOf<AGadgetBase> SlotClass = EquippedGadgetSlots[SlotIndex];
	if (SlotClass == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Gadget] Switch failed: slot %d is empty"), SlotIndex);
		return false;
	}

	if (!EquipGadget(SlotClass))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Gadget] Switch failed: slot %d equip error"), SlotIndex);
		return false;
	}

	CurrentGadgetSlotIndex = SlotIndex;
	UE_LOG(LogTemp, Log, TEXT("[Gadget] Switched to slot %d (%s)"), SlotIndex, *GetNameSafe(SlotClass.Get()));
	return true;
}

bool UGadgetComponent::IsValidSlotIndex(int32 SlotIndex) const
{
	return SlotIndex >= 0 && SlotIndex < MaxGadgetSlots && EquippedGadgetSlots.IsValidIndex(SlotIndex);
}

int32 UGadgetComponent::FindFirstValidSlot() const
{
	for (int32 SlotIndex = 0; SlotIndex < EquippedGadgetSlots.Num(); ++SlotIndex)
	{
		if (EquippedGadgetSlots[SlotIndex] != nullptr)
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

void UGadgetComponent::InitializeDefaultSlots()
{
	EquippedGadgetSlots.SetNum(MaxGadgetSlots);
	for (int32 i = 0; i < MaxGadgetSlots; ++i)
	{
		EquippedGadgetSlots[i] = nullptr;
	}

	for (int32 i = 0; i < DefaultGadgetSlots.Num() && i < MaxGadgetSlots; ++i)
	{
		EquippedGadgetSlots[i] = DefaultGadgetSlots[i];
	}

	if (DefaultGadgetSlots.Num() == 0 && DefaultGadgetClass)
	{
		EquippedGadgetSlots[0] = DefaultGadgetClass;
	}
}

EGadgetUseStyle UGadgetComponent::GetCurrentGadgetUseStyle() const
{
	return CurrentGadget ? CurrentGadget->GetUseStyle() : EGadgetUseStyle::OneShot;
}
