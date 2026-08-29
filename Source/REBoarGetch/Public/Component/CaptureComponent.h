#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CaptureComponent.generated.h"

/** 捕獲実行者とともに捕獲成立を通知するデリゲート型です。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCapturedSignature, AActor*, Capturer);
/** 捕獲状態から解放されたことを通知するデリゲート型です。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReleasedSignature);

/** Actorの捕獲・解放状態と、それに伴う移動停止およびイベント通知を管理します。 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UCaptureComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Tickを使用しない捕獲状態管理Componentの初期状態を構築します。 */
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
	/** Ownerが現在捕獲されているかを保持し、捕獲・解放の多重実行を防ぎます。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="REBoarGetch|Capture", meta=(AllowPrivateAccess="true"))
	bool bIsCaptured = false;
};
