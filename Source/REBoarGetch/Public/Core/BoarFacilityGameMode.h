#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BoarFacilityGameMode.generated.h"

/** Non-stage workspace: uses the existing player, gadgets and save data without stage progression. */
UCLASS()
class REBOARGETCH_API ABoarFacilityGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Facility") bool bArchive=false;
    UPROPERTY(EditDefaultsOnly, Category="Facility") bool bGadgetTest=false;
    UPROPERTY(EditDefaultsOnly, Category="Facility") TSubclassOf<class AGadgetBase> TestGadgetClass;
};
