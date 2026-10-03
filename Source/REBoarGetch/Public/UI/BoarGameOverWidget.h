#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarGameOverWidget.generated.h"

class UButton;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameOverChoice);

/** GameOver専用のRetry/Lobby選択です。既存Resultの任意入力とは分離します。 */
UCLASS(Blueprintable)
class REBOARGETCH_API UBoarGameOverWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	// 選択の意思だけをControllerへ渡し、レベル遷移や終了状態の管理は外側で行います。
	UPROPERTY(BlueprintAssignable) FOnGameOverChoice OnRetryRequested;
	UPROPERTY(BlueprintAssignable) FOnGameOverChoice OnLobbyRequested;
	UFUNCTION(BlueprintCallable) void RequestRetry();
	UFUNCTION(BlueprintCallable) void RequestLobby();
	void FocusInitialChoice();
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	/** 固定名BindWidgetを使わず、Class Defaultsからボタン名を指定します。 */
	UPROPERTY(EditDefaultsOnly, Category = "Widget References") FName RetryButtonWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Widget References") FName LobbyButtonWidgetName;
private:
	UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
	UPROPERTY(Transient) TObjectPtr<UButton> LobbyButton;
};
