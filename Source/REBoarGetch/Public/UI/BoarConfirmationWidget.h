#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarConfirmationWidget.generated.h"

class UButton;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBoarConfirmationDecision, bool, bConfirmed);

/** 文面・装飾はBlueprint、選択後の処理は呼び出し元が管理します。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarConfirmationWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintAssignable, Category = "Confirmation") FOnBoarConfirmationDecision OnDecision;
	void FocusInitialChoice();
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	UPROPERTY(EditDefaultsOnly, Category = "Confirmation|Bindings") FName YesButtonName;
	UPROPERTY(EditDefaultsOnly, Category = "Confirmation|Bindings") FName NoButtonName;
private:
	UFUNCTION() void Confirm();
	UFUNCTION() void Cancel();
	UFUNCTION() void FocusYes();
	UFUNCTION() void FocusNo();
	UPROPERTY(Transient) TObjectPtr<UButton> YesButton;
	UPROPERTY(Transient) TObjectPtr<UButton> NoButton;
	bool bInitialFocusPending = true;
	bool bDecisionSent = false;
};
