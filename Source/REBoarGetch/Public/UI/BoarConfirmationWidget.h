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
	/** 確認操作で選んだ承認・否定を通知する。 */
	UPROPERTY(BlueprintAssignable, Category = "Confirmation")
	FOnBoarConfirmationDecision OnDecision;

	void FocusInitialChoice();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

	/** WBPで承認ボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Confirmation|Bindings")
	FName YesButtonName;

	/** WBPで否定ボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Confirmation|Bindings")
	FName NoButtonName;

private:
	UFUNCTION()
	void Confirm();

	UFUNCTION()
	void Cancel();

	UFUNCTION()
	void FocusYes();

	UFUNCTION()
	void FocusNo();

	/** 確認対象の操作を承認するボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> YesButton;

	/** 確認対象の操作を取り消すボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> NoButton;

	/** 画面表示後に初期フォーカスを設定する必要があるか。 */
	bool bInitialFocusPending = true;
	/** 現在の表示中に承認・否定の結果を通知済みか。 */
	bool bDecisionSent = false;
};
