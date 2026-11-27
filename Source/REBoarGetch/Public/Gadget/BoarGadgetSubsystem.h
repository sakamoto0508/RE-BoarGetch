#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BoarGadgetSubsystem.generated.h"

class AGadgetBase;
class UBoarSaveGame;
class UBoarGameInstance;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBoarGadgetProgressChanged);

/** ガジェットの進行と、セッション中だけの試用選択を扱う。保存はGameInstance側でまとめて行う。 */
UCLASS()
class REBOARGETCH_API UBoarGadgetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

  public:
	/** 新規ガジェットの解放を購読側へ通知する。 */
	UPROPERTY()
	FOnBoarGadgetProgressChanged OnGadgetsUnlocked;

	/** 試用ガジェットの変更を購読側へ通知する。 */
	UPROPERTY()
	FOnBoarGadgetProgressChanged OnTestGadgetChanged;

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

	/** 施設で現在選択中の試用ガジェットのクラス。 */
	UPROPERTY(Transient)
	FName SelectedTestGadget;
};
