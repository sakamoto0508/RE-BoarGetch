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
	UPROPERTY(BlueprintAssignable) FOnBoarEncyclopediaClosed OnClosed;
	void FocusInitialChoice();
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	UPROPERTY(EditDefaultsOnly, Category = "Encyclopedia") TArray<TObjectPtr<UStageConfig>> StageCatalog;
	UPROPERTY(EditDefaultsOnly, Category = "Encyclopedia") TSubclassOf<UBoarLoadoutEntry> EntryClass;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName ListWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName ProgressWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName TypeProgressWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName DetailWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName PhotoWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName PhotoStatusWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Bindings") FName BackButtonName;
private:
	void BuildEntries();
	UFUNCTION() void SelectEntry(int32 Index);
	UFUNCTION() void Close();
	UFUNCTION() void FocusBack();
	UPROPERTY(Transient) TObjectPtr<UScrollBox> List;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ProgressText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TypeProgressText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailText;
	UPROPERTY(Transient) TObjectPtr<UImage> Photo;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PhotoStatus;
	UPROPERTY(Transient) TObjectPtr<UButton> BackButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UBoarLoadoutEntry>> Entries;
	TArray<FIntPoint> Definitions;
	TArray<FText> Labels;
	bool bInitialFocusPending = true;
};
