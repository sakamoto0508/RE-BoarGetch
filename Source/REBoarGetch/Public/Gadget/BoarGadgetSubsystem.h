#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BoarGadgetSubsystem.generated.h"

class AGadgetBase;
class UBoarSaveGame;
class UBoarGameInstance;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBoarGadgetProgressChanged);

/** Gadget progression and session-only trial selection; saving remains transactional in GI. */
UCLASS()
class REBOARGETCH_API UBoarGadgetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
  public:
	UPROPERTY() FOnBoarGadgetProgressChanged OnGadgetsUnlocked;
	UPROPERTY() FOnBoarGadgetProgressChanged OnTestGadgetChanged;
	const TArray<TSubclassOf<AGadgetBase>> &GetCatalog() const;
	TSubclassOf<AGadgetBase> FindGadgetClass(FName Id) const;
	bool IsGadgetUnlocked(FName Id) const;
	int32 GetSpecialCoinCount() const;
	bool CanUnlockGadget(FName Id) const;
	bool UnlockGadgetInLab(FName Id);
	bool SelectTestGadget(FName Id);
	FName GetSelectedTestGadget() const
	{
		return SelectedTestGadget;
	}
	TSoftObjectPtr<UWorld> GetSelectedTestLevel() const;
	bool IsInGadgetLab() const;
	void ClearTestGadget();
	void UpdateInitialUnlocks(UBoarSaveGame *Candidate) const;
	bool IsLoadoutValid(const TArray<FName> &Ids) const;
	void RestoreSavedLoadout(TArray<TSubclassOf<AGadgetBase>> &Slots) const;
	bool CanEquipGadgetClass(TSubclassOf<AGadgetBase> Class) const;
	bool SaveLoadoutClasses(const TArray<TSubclassOf<AGadgetBase>> &Slots);

  private:
	friend class FBoarGadgetProgressTest;
	UBoarGameInstance *GetProgressOwner() const;
	UPROPERTY(Transient) FName SelectedTestGadget;
};
