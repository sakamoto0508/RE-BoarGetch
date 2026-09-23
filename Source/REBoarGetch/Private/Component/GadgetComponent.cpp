#include "Component/GadgetComponent.h"

#include "Engine/World.h"
#include "Gadget/GadgetBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Player/BoarPlayerCharacter.h"
#include "BoarGameInstance.h"
#include "BoarSaveGame.h"
#include "GadgetDataAsset.h"


// Sets default values for this component's properties
UGadgetComponent::UGadgetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// ゲーム開始前からスロット数を固定しておく。 
	// 実際の初期装備内容はBeginPlayのInitializeDefaultSlotsで設定する。
	EquippedGadgetSlots.SetNum(MaxGadgetSlots);
}


void UGadgetComponent::BeginPlay()
{
	Super::BeginPlay();

	OwningPawn = Cast<APawn>(GetOwner());
	UE_LOG(LogTemp, Log, TEXT("[Gadget] Owner pawn: %s"), *GetNameSafe(OwningPawn.Get()));

	InitializeDefaultSlots();
	if (const UBoarGameInstance* Instance = GetWorld()->GetGameInstance<UBoarGameInstance>())
	{
		const UBoarSaveGame* Save = Instance->GetProgress();
		if (Save && Save->bHasSavedLoadout && Save->GadgetLoadout.Num() == MaxGadgetSlots)
		{
			TArray<TSubclassOf<AGadgetBase>> Restored;
			bool bCanRestore = true;
			for (FName Id : Save->GadgetLoadout)
			{
				TSubclassOf<AGadgetBase> Class = Instance->FindGadgetClass(Id);
				if (!Id.IsNone() && !Class) bCanRestore = false;
				Restored.Add(Class);
			}
			if (bCanRestore) EquippedGadgetSlots = Restored;
			else UE_LOG(LogTemp, Error, TEXT("[Gadget] Saved loadout has unresolved IDs; defaults retained, save unchanged."));
		}
	}

	// スロット0から順番に確認し最初にガジェットが登録されているスロットを初期装備にする。
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
		return false;
	
	UWorld* World = GetWorld();
	
	if (World == nullptr) 
		return false;

	FActorSpawnParameters SpawnParams;
	// Spawnされたガジェットの所有者をプレイヤーPawnに設定する。
	SpawnParams.Owner = OwningPawn;
	SpawnParams.Instigator = OwningPawn->GetInstigator();
	// 装備用ガジェットは衝突によってSpawnに失敗してほしくないため周囲のCollision状態にかかわらず必ず生成する。
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AGadgetBase* NewGadget = World->SpawnActor<AGadgetBase>
		(GadgetClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	if (NewGadget == nullptr) 
		return false;

	// ガジェットはCharacterのSkeletalMeshにあるSocketへ装備するため、所有者PawnをACharacterへ変換してMeshを取得する。
	ACharacter* OwningCharacter = Cast<ACharacter>(OwningPawn.Get());
	USkeletalMeshComponent* CharacterMesh = OwningCharacter ? OwningCharacter->GetMesh() : nullptr;
	// 装備先のSocket名はガジェット側に持たせる。これにより、ガジェットごとに異なるSocketへ装備できる。
	const FName EquipSocketName = NewGadget->GetEquipSocketName();
	
	// CharacterのMesh、Socket名、実際のSocketの存在を確認する。
	if (CharacterMesh == nullptr || EquipSocketName.IsNone() || !CharacterMesh->DoesSocketExist(EquipSocketName))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Gadget] Equip failed: Socket '%s' was not found on '%s'"),
			*EquipSocketName.ToString(), *GetNameSafe(CharacterMesh));
		NewGadget->Destroy();
		return false;
	}

	if (!NewGadget->AttachToComponent(CharacterMesh,FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			EquipSocketName))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Gadget] Equip failed: Could not attach '%s' to Socket '%s'"),
			*GetNameSafe(NewGadget), *EquipSocketName.ToString());
		NewGadget->Destroy();
		return false;
	}

	// Spawn に失敗した場合も現在の装備を失わないよう、新しい Actor の生成後に入れ替える。
	UnequipGadget();
	CurrentGadget = NewGadget;
	return true;
}

/**
 * 現在のガチャメカを外します。
 */
void UGadgetComponent::UnequipGadget()
{
	if (IsValid(CurrentGadget))
	{
		if (ABoarPlayerCharacter* Player = Cast<ABoarPlayerCharacter>(OwningPawn)) Player->InterruptGadgetUse();
		CurrentGadget->EndUse(OwningPawn);
		CurrentGadget->Destroy();
	}
	CurrentGadget = nullptr;
	CurrentGadgetSlotIndex = INDEX_NONE;
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
	// 固定4枠の外側へ書き込まないよう、配列アクセス前に番号を検証する。
	if (!IsValidSlotIndex(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Gadget] Invalid slot index: %d"), SlotIndex);
		return false;
	}

	if (EquippedGadgetSlots[SlotIndex] == GadgetClass)
	{
		// 保存失敗後も同じ候補の再選択で保存を再試行できます。
		bLastLoadoutSaveSuccessful = SaveCurrentLoadout();
		return true;
	}
	UBoarGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance<UBoarGameInstance>() : nullptr;
	const UGadgetDataAsset* Definition = GadgetClass ? GadgetClass.GetDefaultObject()->GetGadgetDefinition() : nullptr;
	if (Instance && Definition && !Definition->GadgetId.IsNone() && !Instance->IsGadgetUnlocked(Definition->GadgetId)) return false;
	TArray<TSubclassOf<AGadgetBase>> NewSlots = EquippedGadgetSlots;
	// 同じガジェットを選ぶと元の枠を空にして移動します。
	for (int32 Index = 0; Index < MaxGadgetSlots; ++Index)
		if (GadgetClass && NewSlots[Index] == GadgetClass) NewSlots[Index] = nullptr;
	NewSlots[SlotIndex] = GadgetClass;
	const int32 PreviousSlot = CurrentGadgetSlotIndex;
	if (IsValidSlotIndex(PreviousSlot) && NewSlots[PreviousSlot] != EquippedGadgetSlots[PreviousSlot])
	{
		if (NewSlots[PreviousSlot])
		{
			if (!EquipGadget(NewSlots[PreviousSlot])) return false;
			CurrentGadgetSlotIndex = PreviousSlot;
		}
		else
		{
			UnequipGadget();
			CurrentGadgetSlotIndex = PreviousSlot;
		}
	}
	EquippedGadgetSlots = NewSlots;
	bLastLoadoutSaveSuccessful = SaveCurrentLoadout();
	if (!bLastLoadoutSaveSuccessful)
		UE_LOG(LogTemp, Error, TEXT("[Gadget] Loadout changed but not saved; check GadgetId/catalog/save status."));
	OnGadgetLoadoutChanged.Broadcast();
	return true;
}

bool UGadgetComponent::SaveCurrentLoadout()
{
	auto* Instance = GetWorld() ? GetWorld()->GetGameInstance<UBoarGameInstance>() : nullptr;
	if (!Instance) return false;
	TArray<FName> Ids;
	for (const TSubclassOf<AGadgetBase>& Class : EquippedGadgetSlots)
	{
		const auto* Def = Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
		if (Class && (!Def || Def->GadgetId.IsNone())) return false;
		Ids.Add(Def ? Def->GadgetId : NAME_None);
	}
	return Instance->SaveGadgetLoadout(Ids);
}

bool UGadgetComponent::SwitchGadgetBySlot(int32 SlotIndex)
{
	// 切替に失敗した場合は現在装備を維持するため、先にスロットとクラスを検証する。
	if (!IsValidSlotIndex(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Gadget] Switch failed: invalid slot index %d"), SlotIndex);
		return false;
	}

	TSubclassOf<AGadgetBase> SlotClass = EquippedGadgetSlots[SlotIndex];
	if (CurrentGadgetSlotIndex == SlotIndex && IsValid(CurrentGadget)) return true;
	if (SlotClass == nullptr)
	{
		UnequipGadget();
		CurrentGadgetSlotIndex = SlotIndex;
		OnGadgetLoadoutChanged.Broadcast();
		return true;
	}

	if (!EquipGadget(SlotClass))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Gadget] Switch failed: slot %d equip error"), SlotIndex);
		return false;
	}

	CurrentGadgetSlotIndex = SlotIndex;
	UE_LOG(LogTemp, Log, TEXT("[Gadget] Switched to slot %d (%s)"), SlotIndex, *GetNameSafe(SlotClass.Get()));
	OnGadgetLoadoutChanged.Broadcast();
	return true;
}

TSubclassOf<AGadgetBase> UGadgetComponent::GetGadgetSlotClass(int32 SlotIndex) const
{
	return IsValidSlotIndex(SlotIndex) ? EquippedGadgetSlots[SlotIndex] : nullptr;
}

bool UGadgetComponent::IsValidSlotIndex(int32 SlotIndex) const
{
	// 定数上の最大数と、実際に確保済みの配列範囲の両方を満たす必要がある。
	return SlotIndex >= 0 && SlotIndex < MaxGadgetSlots && EquippedGadgetSlots.IsValidIndex(SlotIndex);
}

int32 UGadgetComponent::FindFirstValidSlot() const
{
	// 初期装備は番号の小さい有効スロットを優先する。
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
	// Editor配列の要素数にかかわらず、ランタイム側は常に4枠へ正規化する。
	EquippedGadgetSlots.SetNum(MaxGadgetSlots);
	for (int32 i = 0; i < MaxGadgetSlots; ++i)
	{
		EquippedGadgetSlots[i] = nullptr;
	}

	for (int32 i = 0; i < DefaultGadgetSlots.Num() && i < MaxGadgetSlots; ++i)
	{
		EquippedGadgetSlots[i] = DefaultGadgetSlots[i];
	}
}

EGadgetUseStyle UGadgetComponent::GetCurrentGadgetUseStyle() const
{
	// 未装備時は押下終了処理を要求しない安全な既定値としてOneShotを返す。
	return CurrentGadget ? CurrentGadget->GetUseStyle() : EGadgetUseStyle::OneShot;
}
