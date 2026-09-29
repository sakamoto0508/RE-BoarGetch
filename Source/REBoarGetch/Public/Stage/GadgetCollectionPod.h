#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GadgetCollectionPod.generated.h"

/** Display only: explicit stable ID, no equip/unlock/input or gadget gameplay. */
UCLASS()
class REBOARGETCH_API AGadgetCollectionPod : public AActor
{
    GENERATED_BODY()
public:
    AGadgetCollectionPod();
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> PodMesh;
    UPROPERTY(EditInstanceOnly, Category="Collection") FName GadgetId;
    UPROPERTY(EditInstanceOnly, Category="Collection") TArray<TObjectPtr<AActor>> DisplayActors;
    UPROPERTY(EditInstanceOnly, Category="Collection") TObjectPtr<class ATextRenderActor> StatusLabel;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void RefreshDisplay();
};
