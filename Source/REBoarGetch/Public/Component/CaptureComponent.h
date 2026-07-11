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

	/// <summary>捕獲を実行します。</summary>
	UFUNCTION(BlueprintCallable, Category="REBoarGetch|Capture")
	bool Capture(AActor* Capturer);

	/// <summary>捕獲解除を実行します。</summary>
	UFUNCTION(BlueprintCallable, Category="REBoarGetch|Capture")
	bool Release();

	/// <summary>捕獲中か返します。</summary>
	UFUNCTION(BlueprintPure, Category="REBoarGetch|Capture")
	bool IsCaptured() const { return bIsCaptured; }

	/// <summary>捕獲イベントです。</summary>
	UPROPERTY(BlueprintAssignable, Category="REBoarGetch|Capture")
	FOnCapturedSignature OnCaptured;

	/// <summary>捕獲解除イベントです。</summary>
	UPROPERTY(BlueprintAssignable, Category="REBoarGetch|Capture")
	FOnReleasedSignature OnReleased;

private:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="REBoarGetch|Capture", meta=(AllowPrivateAccess="true"))
	bool bIsCaptured = false;
};