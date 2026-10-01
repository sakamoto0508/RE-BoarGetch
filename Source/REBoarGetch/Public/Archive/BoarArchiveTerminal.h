#pragma once
#include "CoreMinimal.h"
#include "Stage/HubPortal.h"
#include "BoarArchiveTerminal.generated.h"

/** 同じ正式Terminal Meshを、展示選択または既存図鑑の入口として使います。 */
UCLASS(Blueprintable)
class REBOARGETCH_API ABoarArchiveTerminal : public AHubPortal
{
    GENERATED_BODY()
public:
    ABoarArchiveTerminal();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<class UStaticMeshComponent> TerminalVisual;
    UPROPERTY(EditAnywhere,Category="Archive") bool bDisplaySelection=false;
    virtual void Interact(class ABoarPlayerController* Controller) override;
};
