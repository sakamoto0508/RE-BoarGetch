#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HubPortal.generated.h"
class UBoxComponent;
UENUM(BlueprintType)
enum class EHubPortalAction : uint8 { Travel, Loadout, Archive, GadgetTest };
/** A local-player-only portal or terminal. Stage Select retains its own entrance class. */
UCLASS()
class REBOARGETCH_API AHubPortal : public AActor
{
    GENERATED_BODY()
public:
    AHubPortal();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UBoxComponent> Trigger;
    UPROPERTY(EditAnywhere, Category="Portal") EHubPortalAction Action=EHubPortalAction::Travel;
    UPROPERTY(EditAnywhere, Category="Portal") TSoftObjectPtr<UWorld> Destination;
    UPROPERTY(EditInstanceOnly, Category="Portal") TArray<TObjectPtr<AActor>> TestVisualActors;
    /** Frame stays visible; only effect actors belong in TestVisualActors. */
    UPROPERTY(EditInstanceOnly, Category="Portal") TArray<TObjectPtr<AActor>> TestBarrierActors;
    UPROPERTY(EditInstanceOnly, Category="Portal") bool bClearTestTargetOnTravel=false;
    UPROPERTY(EditInstanceOnly, Category="Portal") bool bRequiresInteraction=true;
    virtual void Interact(class ABoarPlayerController* Controller);
    UPROPERTY(EditInstanceOnly, Category="Portal") TObjectPtr<class ATextRenderActor> TestLabel;
    UPROPERTY(EditInstanceOnly, Category="Portal") TArray<TObjectPtr<class ATextRenderActor>> CollectionLabels;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void RefreshTestTarget();
    bool bTravelRequested=false;
    void Activate(class ABoarPlayerController* Controller);
    UFUNCTION() void Enter(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,int32 BodyIndex,bool bSweep,const FHitResult& Hit);
};
