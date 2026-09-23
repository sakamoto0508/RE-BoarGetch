#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Stage/StageRunData.h"
#include "BoarGameInstance.generated.h"

class UBoarSaveGame;
class UStageConfig;
class AGadgetBase;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGadgetsUnlocked);

/** 永続進行の読み書きを集約します。失敗・中断した挑戦から進行を保存しません。 */
UCLASS()
class REBOARGETCH_API UBoarGameInstance : public UGameInstance
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintAssignable) FOnGadgetsUnlocked OnGadgetsUnlocked;
	virtual void Init() override;
	/** 読み取り用の確定済み進行です。 */
	UFUNCTION(BlueprintPure) const UBoarSaveGame* GetProgress() const { return Progress; }
	/** NEWを確定してから保存します。失敗時は既存の確定データを維持します。 */
	bool CommitClearedRun(FStageRunData& Run);
	/** 出発時に最後に挑戦したStageだけを即時保存します。 */
	bool SaveLastAttemptedStage(FName StageId);
	/** 構成済み4スロットのIDを即時保存します。空スロットも保存します。 */
	bool SaveGadgetLoadout(const TArray<FName>& Ids);
	UFUNCTION(BlueprintPure) bool IsStageUnlocked(const UStageConfig* Config) const;
	UFUNCTION(BlueprintPure) bool IsGadgetUnlocked(FName GadgetId) const;
	/** UEで順番を指定する実装済みガジェット一覧です。IDからClassを解決します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Progress")
	TArray<TSubclassOf<AGadgetBase>> GadgetCatalog;
	TSubclassOf<AGadgetBase> FindGadgetClass(FName Id) const;
private:
	bool SaveCandidate(UBoarSaveGame* Candidate);
	void UpdateUnlockedGadgets(UBoarSaveGame* Candidate) const;
	UPROPERTY(Transient) TObjectPtr<UBoarSaveGame> Progress;
	/** 既存セーブの読込失敗時は上書きを禁止します。 */
	bool bSaveLoadFailed = false;
	UPROPERTY(EditDefaultsOnly, Category = "Progress") FString SaveSlotName = TEXT("REBoarGetch_Progress");
	UPROPERTY(EditDefaultsOnly, Category = "Progress") int32 SaveUserIndex = 0;
};
