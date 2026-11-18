#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Stage/BoarLevelTransitionLibrary.h"
#include "HubPortal.generated.h"
class UBoxComponent;
class ASkeletalMeshActor;
class UMaterialInterface;
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
    /** 固定入口ではクラス既定の Destination を使います。旧配置は従来の優先順を維持します。 */
    UPROPERTY(EditDefaultsOnly, Category="Portal") bool bUseClassDestination=false;
    /** 旧配置互換。クラス既定の遷移先を使わない場合だけ、Destination より優先します。 */
    UPROPERTY(EditAnywhere, Category="Portal", meta=(EditCondition="!bUseClassDestination"))
    TObjectPtr<class UBoarLevelDestination> DestinationAsset;
    /** 固定入口の遷移先。bUseClassDestination が有効なら Class Defaults の値を使用します。 */
    UPROPERTY(EditAnywhere, Category="Portal") TSoftObjectPtr<UWorld> Destination;
    /** 使う演出の種類。Project Settings > Game > Boar Level Transitions の同じ名前の項目の演出になります（ロビーへ戻るポータルは Return To Lobby など）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Portal|Transition") EBoarLevelTransitionStyle TransitionStyle=EBoarLevelTransitionStyle::Portal;
    /** オンにすると、このポータルだけ Transition の演出を使います（TransitionStyle より優先）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Portal|Transition", meta=(InlineEditConditionToggle)) bool bOverrideTransition=false;
    /** このポータルで遷移するときの演出（Effect、または長さ・色まで調整したPreset）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Portal|Transition", meta=(EditCondition="bOverrideTransition")) FBoarLevelTransitionEntry Transition;
    /** 固定入口はクラス既定値、旧配置は DataAsset → 直接指定の順に解決します。 */
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
    /** 試用対象を表示する専用BP。PortalTestPreview タグの Child Actor Component を使用します。 */
    UPROPERTY(EditDefaultsOnly, Category="Portal|Test Preview")
    TMap<FName, TSubclassOf<ASkeletalMeshActor>> TestPreviewClasses;
    UPROPERTY(EditDefaultsOnly, Category="Portal|Test Preview") TObjectPtr<UMaterialInterface> TestPreviewMaterial;
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
    void RefreshTestPreview(FName GadgetId);
    bool bTravelRequested=false;
    void Activate(class ABoarPlayerController* Controller);
    UFUNCTION() void Enter(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,int32 BodyIndex,bool bSweep,const FHitResult& Hit);
};
