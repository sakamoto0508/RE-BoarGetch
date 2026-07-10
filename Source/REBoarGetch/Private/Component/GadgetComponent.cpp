#include "Component/GadgetComponent.h"

#include "Engine/World.h"
#include "Gadget/GadgetBase.h"
#include "GameFramework/Pawn.h"


// Sets default values for this component's properties
UGadgetComponent::UGadgetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}


// Called when the game starts
void UGadgetComponent::BeginPlay()
{
	Super::BeginPlay();

	OwningPawn = Cast<APawn>(GetOwner());
	if (DefaultGadgetClass)
	{
		EquipGadget(DefaultGadgetClass);
	}
}

///<summary> ガジェットを装備します。 ///</summary>
bool UGadgetComponent::EquipGadget(TSubclassOf<AGadgetBase> GadgetClass)
{
	if (GadgetClass == nullptr || OwningPawn == nullptr)
	{
		return false;
	}

	UnequipGadget();

	UWorld* World = GetWorld();
	if (World == nullptr) return false;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwningPawn;
	SpawnParams.Instigator = OwningPawn->GetInstigator();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	CurrentGadget = World->SpawnActor<AGadgetBase>
		(GadgetClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	if (CurrentGadget == nullptr) return false;

	CurrentGadget->AttachToActor(OwningPawn, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	return true;
}

/// <summary>
/// 現在のガチャメカを外します。
/// </summary>
void UGadgetComponent::UnequipGadget()
{
	if (CurrentGadget == nullptr)	return;

	CurrentGadget->Destroy();
	CurrentGadget = nullptr;
}

/// <summary>
/// 現在装備中のガチャメカを使います。
/// </summary>
bool UGadgetComponent::UseCurrentGadget()
{
	if (CurrentGadget == nullptr) return false;

	CurrentGadget->Use(OwningPawn.Get());
	return true;
}

/// <summary>
/// ガチャメカを切り替えます。
/// </summary>
bool UGadgetComponent::SwitchGadget(TSubclassOf<AGadgetBase> NewGadgetClass)
{
	if (NewGadgetClass == nullptr) return false;
	
	return EquipGadget(NewGadgetClass);
}
