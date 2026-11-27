#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BoarFacilityGameMode.generated.h"

/** ステージ以外の施設用GameMode。既存のPlayer・ガジェット・セーブデータを使い、ステージ進行は行わない。 */
UCLASS()
class REBOARGETCH_API ABoarFacilityGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    /** 図鑑施設として展示枠の変更を許可するか。 */
    UPROPERTY(EditDefaultsOnly, Category="Facility")
    bool bArchive=false;

    /** 施設でガジェット試用を有効にするか。 */
    UPROPERTY(EditDefaultsOnly, Category="Facility")
    bool bGadgetTest=false;

    /** 施設で初期装備する試用ガジェットのクラス。 */
    UPROPERTY(EditDefaultsOnly, Category="Facility")
    TSubclassOf<class AGadgetBase> TestGadgetClass;

protected:
	virtual void StartPlay() override;
};
