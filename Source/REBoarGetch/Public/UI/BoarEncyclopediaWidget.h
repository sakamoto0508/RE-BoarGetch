#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarEncyclopediaWidget.generated.h"

class UStageConfig;
class UBoarLoadoutEntry;
class UScrollBox;
class UTextBlock;
class UImage;
class UButton;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBoarEncyclopediaClosed);

/** 永続個体定義と確定済みSaveDataを読み取る図鑑です。保存処理は行いません。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarEncyclopediaWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 図鑑を閉じたことを親画面へ通知する。 */
	UPROPERTY(BlueprintAssignable)
	FOnBoarEncyclopediaClosed OnClosed;

	void FocusInitialChoice();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	/** 旧設定です。GameInstanceのStageCatalogが未設定の間だけ使います。設定後は空にしてください。 */
	UPROPERTY(EditDefaultsOnly, Category = "Encyclopedia")
	TArray<TObjectPtr<UStageConfig>> StageCatalog;

	/** 図鑑の個体一覧に使う項目Widgetクラス。 */
	UPROPERTY(EditDefaultsOnly, Category = "Encyclopedia")
	TSubclassOf<UBoarLoadoutEntry> EntryClass;

	/** WBPで個体一覧を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName ListWidgetName;

	/** WBPで総登録進捗欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName ProgressWidgetName;

	/** WBPで種別の登録進捗欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName TypeProgressWidgetName;

	/** WBPで個体詳細欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName DetailWidgetName;

	/** WBPで個体写真を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName PhotoWidgetName;

	/** WBPで写真の表示状態欄を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName PhotoStatusWidgetName;

	/** WBPで戻るボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Bindings")
	FName BackButtonName;

private:
	void BuildEntries();

	UFUNCTION()
	void SelectEntry(int32 Index);

	UFUNCTION()
	void Close();

	UFUNCTION()
	void FocusBack();

	/** 登録・未登録個体の項目を並べるコンテナ。 */
	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> List;

	/** 図鑑全体の登録進捗を表示するテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ProgressText;

	/** 種別ごとの登録進捗を表示するテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TypeProgressText;

	/** 選択した個体の説明を表示するテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DetailText;

	/** 選択した個体の写真を表示する画像。 */
	UPROPERTY(Transient)
	TObjectPtr<UImage> Photo;

	/** 写真を表示できない理由を示すテキスト。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PhotoStatus;

	/** 図鑑を閉じるボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> BackButton;

	/** 選択状態を反映する一覧項目の参照。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoarLoadoutEntry>> Entries;

	/** 表示行と元の個体定義の対応。XはStageCatalog内、Yはステージ内の添字。 */
	TArray<FIntPoint> Definitions;
	/** 一覧項目に表示するラベルの配列。 */
	TArray<FText> Labels;
	/** 図鑑を開いた後に初期フォーカスを設定する必要があるか。 */
	bool bInitialFocusPending = true;
};
