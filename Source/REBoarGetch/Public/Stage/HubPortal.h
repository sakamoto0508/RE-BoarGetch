#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HubPortal.generated.h"
class UBoxComponent;
UENUM(BlueprintType)
enum class EHubPortalAction : uint8 { Travel, Loadout, Archive, GadgetTest };
/** Gadget Labの看板1枚分の設定。表示するGadgetはGadgetIdで解決し、配列の順番やGadgetCatalogの並びに依存しません。 */
USTRUCT()
struct FGadgetCollectionLabel
{
    GENERATED_BODY()
    /** 表示するGadgetのID（UGadgetDataAsset::GadgetId）。 */
    UPROPERTY(EditAnywhere, Category="Portal") FName GadgetId;
    UPROPERTY(EditAnywhere, Category="Portal") TObjectPtr<class ATextRenderActor> Label;
};
/** A local-player-only portal or terminal. Stage Select retains its own entrance class. */
UCLASS()
class REBOARGETCH_API AHubPortal : public AActor
{
    GENERATED_BODY()
public:
    AHubPortal();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UBoxComponent> Trigger;
    UPROPERTY(EditAnywhere, Category="Portal") EHubPortalAction Action=EHubPortalAction::Travel;
    /** 行き先（Action = Travel）。設定されていれば Destination より優先します。行き先の変更はこのDataAssetの Level で行います。 */
    UPROPERTY(EditAnywhere, Category="Portal") TObjectPtr<class UBoarLevelDestination> DestinationAsset;
    /** 旧来の直接指定。DestinationAsset が未設定のときだけ使います。 */
    UPROPERTY(EditAnywhere, Category="Portal") TSoftObjectPtr<UWorld> Destination;
    /** Travel時に使う行き先（DestinationAsset優先、無ければDestination）を返します。 */
    TSoftObjectPtr<UWorld> GetTravelDestination() const;
    UPROPERTY(EditInstanceOnly, Category="Portal") TArray<TObjectPtr<AActor>> TestVisualActors;
    /** Frame stays visible; only effect actors belong in TestVisualActors. */
    UPROPERTY(EditInstanceOnly, Category="Portal") TArray<TObjectPtr<AActor>> TestBarrierActors;
    UPROPERTY(EditInstanceOnly, Category="Portal") bool bClearTestTargetOnTravel=false;
    UPROPERTY(EditInstanceOnly, Category="Portal") bool bRequiresInteraction=true;
    virtual void Interact(class ABoarPlayerController* Controller);
    UPROPERTY(EditInstanceOnly, Category="Portal") TObjectPtr<class ATextRenderActor> TestLabel;
    /** GadgetTest用の看板一覧。READY / LOCKEDと表示名を、各要素のGadgetIdから解決して表示します。 */
    UPROPERTY(EditInstanceOnly, Category="Portal", meta=(TitleProperty="GadgetId")) TArray<FGadgetCollectionLabel> GadgetLabels;
    /** 看板設定の不正（ID未設定・未登録ID・Label未設定・IDやLabelの重複）を列挙します。GIが無い場合は未登録IDを判定しません。 */
    static TArray<FString> FindGadgetLabelProblems(const TArray<FGadgetCollectionLabel>& Labels, const class UBoarGameInstance* GI);
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class FBoarHubPortalGadgetLabelTest;
    UFUNCTION() void RefreshTestTarget();
    bool bTravelRequested=false;
    void Activate(class ABoarPlayerController* Controller);
    UFUNCTION() void Enter(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,int32 BodyIndex,bool bSweep,const FHitResult& Hit);
};
