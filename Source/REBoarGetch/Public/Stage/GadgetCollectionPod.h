#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GadgetCollectionPod.generated.h"

class ASkeletalMeshActor;
class UChildActorComponent;
class UMaterialInterface;
class UStaticMeshComponent;

/** 展示専用。固定IDを持つだけで、装備・解放・入力・ガジェットの動作は行わない。 */
UCLASS()
class REBOARGETCH_API AGadgetCollectionPod : public AActor
{
    GENERATED_BODY()

public:
    AGadgetCollectionPod();
    virtual void OnConstruction(const FTransform& Transform) override;

    /** 収集ポッド本体を表示するメッシュ。 */
    UPROPERTY(VisibleAnywhere, Category="Collection")
    TObjectPtr<UStaticMeshComponent> PodMesh;

    /** ポッドに展示するガジェットのメッシュ。 */
    UPROPERTY(VisibleAnywhere, Category="Collection")
    TObjectPtr<UChildActorComponent> Display;

    /** 表示BPを使用しない場合は、このコンポーネントに展示メッシュを設定します。 */
    UPROPERTY(VisibleAnywhere, Category="Collection")
    TObjectPtr<UStaticMeshComponent> DisplayMesh;

    /** ガジェット名・状態を表示するネームプレート。 */
    UPROPERTY(VisibleAnywhere, Category="Collection")
    TObjectPtr<UChildActorComponent> Nameplate;

    /** 解放状態を照会するガジェットID。 */
    UPROPERTY(EditAnywhere, Category="Collection")
    FName GadgetId;

    /** 展示専用の Skeletal Mesh Actor BP。位置と拡大率は Display コンポーネントで調整します。 */
    UPROPERTY(EditAnywhere, Category="Collection")
    TSubclassOf<ASkeletalMeshActor> DisplayActorClass;

    /** ポッドの透明部に適用するマテリアル。 */
    UPROPERTY(EditAnywhere, Category="Collection|Glass")
    TObjectPtr<UMaterialInterface> GlassMaterial;

    /** ガラス用マテリアルを差し替えるメッシュスロット番号。 */
    UPROPERTY(EditDefaultsOnly, Category="Collection|Glass")
    TArray<int32> GlassMaterialSlots;

    /** ポッドのガラスの不透明度。 */
    UPROPERTY(EditAnywhere, Category="Collection|Glass", meta=(ClampMin="0.0", ClampMax="1.0"))
    float GlassOpacity = 0.8f;
    /** ポッドのガラスに掛ける色。 */
    UPROPERTY(EditAnywhere, Category="Collection|Glass")
    FLinearColor GlassTint = FLinearColor(0.021665f, 0.067100f, 0.072917f, 1.f);

    /** 既存マップとの互換用。新しいポッドは Display と Nameplate を使います。 */
    UPROPERTY(EditInstanceOnly, Category="Collection")
    TArray<TObjectPtr<AActor>> DisplayActors;

    /** 解放状況を表示するラベル。 */
    UPROPERTY(EditInstanceOnly, Category="Collection")
    TObjectPtr<class ATextRenderActor> StatusLabel;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    void ApplyGlassMaterial();

    UFUNCTION()
    void RefreshDisplay();
};
