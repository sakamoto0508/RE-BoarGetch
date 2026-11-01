#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarInitializationErrorWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBoarInitializationChoice);

/** Recovery remains available even when gameplay could not be initialized. レイアウトはWBPで作成します。 */
UCLASS(Blueprintable)
class REBOARGETCH_API UBoarInitializationErrorWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	FOnBoarInitializationChoice OnRetry;
	FOnBoarInitializationChoice OnLobby;
	void ShowError(const FText& Reason);
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	/** 固定名BindWidgetを使わず、Class Defaultsから各Widget名を指定します。 */
	UPROPERTY(EditDefaultsOnly, Category = "Widget References") FName MessageTextWidgetName = TEXT("Text_Message");
	UPROPERTY(EditDefaultsOnly, Category = "Widget References") FName RetryButtonWidgetName = TEXT("Button_Retry");
	UPROPERTY(EditDefaultsOnly, Category = "Widget References") FName LobbyButtonWidgetName = TEXT("Button_Lobby");
	/** {0} を失敗理由に置換します。 */
	UPROPERTY(EditDefaultsOnly, Category = "Text") FText MessageFormat = NSLOCTEXT("WorldInit", "Failure", "ゲームを開始できませんでした。\n\n{0}\n");
private:
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> Message;
	UPROPERTY(Transient) TObjectPtr<class UButton> RetryButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> LobbyButton;
	UFUNCTION() void Retry() { OnRetry.Broadcast(); }
	UFUNCTION() void Lobby() { OnLobby.Broadcast(); }
};
