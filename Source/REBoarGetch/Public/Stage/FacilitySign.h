#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FacilitySign.generated.h"

class UStaticMeshComponent;
class UWidgetComponent;
class UMaterialInterface;
class AHubPortal;
class ABoarArchiveDisplay;
class ATextRenderActor;
class UPrimitiveComponent;

/** 表示階層。文字サイズ・発光量・余白はこの階層から決まり、個別に調整しません（値はFFacilitySignStyle）。 */
UENUM(BlueprintType)
enum class EFacilitySignTier : uint8
{
    AreaSign,       // エリア名：壁上部の大型サイン
    FacilityLabel,  // 設備名：設備に付属する中サイズのパネル
    Nameplate,      // 展示名札：展示台へ埋め込む小型表示
    Prompt          // 操作案内：近づいた時だけ表示する小型ホログラム
};

/** 階層ごとの見た目。値はWBP_FacilitySignのClass Defaults（TierStyles）で調整します。サイズの単位はcm（文字の高さ）。 */
USTRUCT(BlueprintType)
struct FFacilitySignStyle
{
    GENERATED_BODY()

    /** 見出し文字のフォントサイズ。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(Units="cm", ClampMin="0.1"))
    float TitleSize=14.f;

    /** 補助文字のフォントサイズ。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(Units="cm", ClampMin="0.1"))
    float SubtitleSize=7.f;

    /** パネル外周の余白。タイトルとサブタイトルの行間もこの値から決まります。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(Units="cm", ClampMin="0"))
    float Padding=12.f;

    /** 文字面MaterialのIntensity（発光倍率）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
    float Intensity=1.5f;

    /** パネルの厚み。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(Units="cm", ClampMin="0.1"))
    float Depth=4.f;

    /** 見出し文字の色。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FLinearColor TitleColor=FLinearColor(FColor(244, 248, 252));

    /** 補助文字の色。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FLinearColor SubtitleColor=FLinearColor(FColor(64, 212, 240));
};

/** 施設内のWorld Space表示。文字は必ず背面パネルを媒体として持ち、空中に浮かせません。Gameplay状態は持たず、既存Actorを購読して表示だけを切り替えます。 */
UCLASS()
class REBOARGETCH_API AFacilitySign : public AActor
{
    GENERATED_BODY()

public:
    AFacilitySign();
    virtual void OnConstruction(const FTransform& Transform) override;
    /** 設備が所有する名札の表示を、元データの更新通知から反映します。 */
    void SetDisplayText(const FText& NewTitle, bool bDimmed);
    /** 展示設備が所有する名札として、展示元と状態表示用の部品（リング等）を設定します。 */
    void BindSpecimenSource(ABoarArchiveDisplay* Source, const TArray<UPrimitiveComponent*>& Highlights);

    /** サインの各部品を配置するルート。 */
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> SceneRoot;

    /** 背面パネル。Mesh・Materialは変更可能。大きさは文字幅から自動計算します。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sign")
    TObjectPtr<UStaticMeshComponent> Panel;

    /** サインの階層や点灯状態を示すアクセントライン。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sign")
    TObjectPtr<UStaticMeshComponent> AccentLine;

    /** 文字面。既定はWBP_FacilitySign（Widget Class）。書体・配置はそのWBPのDesignerで編集します。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sign")
    TObjectPtr<UWidgetComponent> LabelWidget;

    /** 外観設定の選択に使うサインの階層。 */
    UPROPERTY(EditAnywhere, Category="Sign")
    EFacilitySignTier Tier=EFacilitySignTier::FacilityLabel;

    /** 表示する見出し。 */
    UPROPERTY(EditAnywhere, Category="Sign")
    FText Title;

    /** 表示する補助文字。 */
    UPROPERTY(EditAnywhere, Category="Sign")
    FText Subtitle;

    /** 0なら文字幅から自動計算します。幅を揃えたい名札などだけ指定します。 */
    UPROPERTY(EditAnywhere, Category="Sign", meta=(Units="cm"))
    float FixedPanelWidth=0.f;

    /** 文字面用。Widgetの描画結果（SlateUI）をIntensity倍で自発光させます。 */
    UPROPERTY(EditAnywhere, Category="Sign|Style")
    TObjectPtr<UMaterialInterface> WidgetMaterial;

    /** サイン本体の外観マテリアル。 */
    UPROPERTY(EditAnywhere, Category="Sign|Style")
    TObjectPtr<UMaterialInterface> PanelMaterial;

    /** アクセントラインの基本マテリアル。 */
    UPROPERTY(EditAnywhere, Category="Sign|Style")
    TObjectPtr<UMaterialInterface> AccentMaterial;

    /** 指定時、このPortal / TerminalのTriggerにローカルPlayerが入っている間を「接近中」とします。 */
    UPROPERTY(EditInstanceOnly, Category="Sign|State")
    TObjectPtr<AHubPortal> ProximitySource;

    /** 接近中だけ表示します。Prompt階層の既定です。 */
    UPROPERTY(EditInstanceOnly, Category="Sign|State")
    bool bShowOnlyWhenNear=false;

    /** 状態に応じてMaterialを切り替える床ライン・リング等。 */
    UPROPERTY(EditInstanceOnly, Category="Sign|State")
    TArray<TObjectPtr<AActor>> HighlightActors;

    /** 所有元の設備から渡された、Materialを切り替える部品（BindSpecimenSourceで設定）。 */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UPrimitiveComponent>> HighlightComponents;

    /** 点灯中に使うアクセントマテリアル。 */
    UPROPERTY(EditAnywhere, Category="Sign|State")
    TObjectPtr<UMaterialInterface> HighlightOnMaterial;

    /** 消灯中に使うアクセントマテリアル。 */
    UPROPERTY(EditAnywhere, Category="Sign|State")
    TObjectPtr<UMaterialInterface> HighlightOffMaterial;

    /** 指定時、展示中個体の図鑑名をTitleへ表示します。空き枠はEmptyTitleを暗く表示します。 */
    UPROPERTY(EditInstanceOnly, Category="Sign|Specimen")
    TObjectPtr<ABoarArchiveDisplay> SpecimenSource;

    /** 展示枠が空のときに表示する見出し。 */
    UPROPERTY(EditAnywhere, Category="Sign|Specimen")
    FText EmptyTitle;

    /** 個体名が未設定のときに表示する見出し。 */
    UPROPERTY(EditAnywhere, Category="Sign|Specimen")
    FText UnnamedTitle;

    /** Gameplay側が文字を書き換える既存TextRenderActor。その文字をTitleとして表示し、元の文字は非表示にします。 */
    UPROPERTY(EditInstanceOnly, Category="Sign|Mirror")
    TObjectPtr<ATextRenderActor> MirrorTextSource;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    /** 既存表示との差分判定に使う表示済みの見出し。 */
    FText ShownTitle;
    /** 最後に表示へ反映した減光状態。 */
    bool bShownDimmed=false;
    /** 展示内容の定期反映を解除するタイマー。 */
    FTimerHandle MirrorTimer;
    void ApplyLayout(const FText& NewTitle, bool bDimmed);
    void SetNear(bool bNear);
    void SetHighlight(bool bOn);
    FText ReadMirrorText() const;
    /** 文字面にWBP（非NativeのUFacilitySignWidget派生）が設定されているか。 */
    bool HasSignWidgetClass() const;
    void PollMirrorText();

    UFUNCTION()
    void RefreshSpecimen();

    UFUNCTION()
    void OnProximityBegin(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,int32 BodyIndex,bool bSweep,const FHitResult& Hit);

    UFUNCTION()
    void OnProximityEnd(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,int32 BodyIndex);
};
