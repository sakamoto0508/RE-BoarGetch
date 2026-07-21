#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CaptureComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCapturedSignature, AActor*, Capturer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReleasedSignature);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UCaptureComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCaptureComponent();

	/** 捕獲を実行します。 */
	UFUNCTION(BlueprintCallable, Category="REBoarGetch|Capture")
	bool Capture(AActor* Capturer);

	/** 捕獲解除を実行します。 */
	UFUNCTION(BlueprintCallable, Category="REBoarGetch|Capture")
	bool Release();

	/** 捕獲中か返します。 */
	UFUNCTION(BlueprintPure, Category="REBoarGetch|Capture")
	bool IsCaptured() const { return bIsCaptured; }

	/** 捕獲イベントです。 */
	UPROPERTY(BlueprintAssignable, Category="REBoarGetch|Capture")
	FOnCapturedSignature OnCaptured;

	/** 捕獲解除イベントです。 */
	UPROPERTY(BlueprintAssignable, Category="REBoarGetch|Capture")
	FOnReleasedSignature OnReleased;

private:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="REBoarGetch|Capture", meta=(AllowPrivateAccess="true"))
	bool bIsCaptured = false;
};
