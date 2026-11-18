#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GadgetCollectionPod.generated.h"

class ASkeletalMeshActor;
class UChildActorComponent;
class UMaterialInterface;
class UStaticMeshComponent;

/** Display only: explicit stable ID, no equip/unlock/input or gadget gameplay. */
UCLASS()
class REBOARGETCH_API AGadgetCollectionPod : public AActor
{
    GENERATED_BODY()
public:
    AGadgetCollectionPod();
    virtual void OnConstruction(const FTransform& Transform) override;

    UPROPERTY(VisibleAnywhere, Category="Collection") TObjectPtr<UStaticMeshComponent> PodMesh;
    UPROPERTY(VisibleAnywhere, Category="Collection") TObjectPtr<UChildActorComponent> Display;
    /** 表示BPを使用しない場合は、このコンポーネントに展示メッシュを設定します。 */
    UPROPERTY(VisibleAnywhere, Category="Collection") TObjectPtr<UStaticMeshComponent> DisplayMesh;
    UPROPERTY(VisibleAnywhere, Category="Collection") TObjectPtr<UChildActorComponent> Nameplate;
    UPROPERTY(EditAnywhere, Category="Collection") FName GadgetId;
    /** 展示専用の Skeletal Mesh Actor BP。位置と拡大率は Display コンポーネントで調整します。 */
    UPROPERTY(EditAnywhere, Category="Collection") TSubclassOf<ASkeletalMeshActor> DisplayActorClass;
    UPROPERTY(EditAnywhere, Category="Collection|Glass") TObjectPtr<UMaterialInterface> GlassMaterial;
    UPROPERTY(EditDefaultsOnly, Category="Collection|Glass") TArray<int32> GlassMaterialSlots;
    UPROPERTY(EditAnywhere, Category="Collection|Glass", meta=(ClampMin="0.0", ClampMax="1.0"))
    float GlassOpacity = 0.8f;
    UPROPERTY(EditAnywhere, Category="Collection|Glass")
    FLinearColor GlassTint = FLinearColor(0.021665f, 0.067100f, 0.072917f, 1.f);

    /** 既存マップとの互換用。新しいポッドは Display と Nameplate を使います。 */
    UPROPERTY(EditInstanceOnly, Category="Collection") TArray<TObjectPtr<AActor>> DisplayActors;
    UPROPERTY(EditInstanceOnly, Category="Collection") TObjectPtr<class ATextRenderActor> StatusLabel;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void ApplyGlassMaterial();
    UFUNCTION() void RefreshDisplay();
};
