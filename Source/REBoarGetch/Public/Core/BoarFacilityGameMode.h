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
    UPROPERTY(EditDefaultsOnly, Category="Facility")
    bool bArchive=false;

    UPROPERTY(EditDefaultsOnly, Category="Facility")
    bool bGadgetTest=false;

    UPROPERTY(EditDefaultsOnly, Category="Facility")
    TSubclassOf<class AGadgetBase> TestGadgetClass;

protected:
	virtual void StartPlay() override;
};
