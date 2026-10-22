#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarInitializationErrorWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBoarInitializationChoice);

/** Recovery remains available even when gameplay could not be initialized. */
UCLASS()
class REBOARGETCH_API UBoarInitializationErrorWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	FOnBoarInitializationChoice OnRetry;
	FOnBoarInitializationChoice OnLobby;
	void ShowError(const FText& Reason);
protected:
	virtual void NativeOnInitialized() override;
private:
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> Message;
	UPROPERTY(Transient) TObjectPtr<class UButton> RetryButton;
	UFUNCTION() void Retry() { OnRetry.Broadcast(); }
	UFUNCTION() void Lobby() { OnLobby.Broadcast(); }
};
