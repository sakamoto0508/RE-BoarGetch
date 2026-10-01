#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoarArchiveDisplay.generated.h"

/** 展示Visualのみ。AI・移動・HP・捕獲・Save更新は持ちません。 */
UCLASS(Blueprintable)
class REBOARGETCH_API ABoarArchiveDisplay : public AActor
{
    GENERATED_BODY()
public:
    ABoarArchiveDisplay();
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class USkeletalMeshComponent> BoarVisual;
    /** Saveの展示枠。左から0・1・2です。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Archive", meta=(ClampMin="0",ClampMax="2")) int32 SlotIndex=0;
    /** BoarVisualだけのYaw回転速度。12度/秒なら30秒で一周します。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Archive", meta=(Units="deg/s")) float RotationSpeed=12.f;
    /** 個体IDから本編Boar Classを解決する既存StageConfig一覧です。 */
    UPROPERTY(EditAnywhere, Category="Archive") TArray<TObjectPtr<class UStageConfig>> StageCatalog;
    /** 本編Visualを取得できない場合のEditor表示用Mesh。Runtimeの未捕獲枠は埋めません。 */
    UPROPERTY(EditAnywhere, Category="Archive|Visual") TObjectPtr<class USkeletalMesh> PreviewMesh;
    UPROPERTY(EditAnywhere, Category="Archive|Visual") TArray<TObjectPtr<class UMaterialInterface>> VisualMaterials;
    /** Idleだけをループ再生します。未設定なら静止Pose。歩行・攻撃AnimBPは使いません。 */
    UPROPERTY(EditAnywhere, Category="Archive|Visual") TObjectPtr<class UAnimSequence> IdleAnimation;
    UPROPERTY(EditAnywhere, Category="Archive|Visual", meta=(ClampMin="0.01")) float DisplayScale=.82f;
    UPROPERTY(EditAnywhere, Category="Archive|Visual") FVector VisualOffset=FVector::ZeroVector;
    /** 配置確認用のBoar表示。Editor専用で、Play開始後はSaveから展示対象を決めます。 */
    UPROPERTY(EditAnywhere, Category="Archive|Visual") bool bPreviewInEditor=true;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void RefreshDisplay();
    void ConfigureVisual(USkeletalMesh* Mesh, const TArray<UMaterialInterface*>& Materials, UAnimSequence* Idle);
};
