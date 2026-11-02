#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BoarMovementComponent.generated.h"

/**
 * Boar用のCharacterMovement。演出用の移動時間倍率だけを追加します。
 * 倍率はMovementの経過時間に掛けるため、MaxWalkSpeedやVelocityの値は変えず、移動・加速・旋回が同じ割合で遅くなります。
 * Actor Tick、AI（StateTree）、Timer、World時間には影響しません。
 */
UCLASS()
class REBOARGETCH_API UBoarMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/** 演出用の移動時間倍率を設定します（1で通常）。設定するのは捕獲演出だけです。 */
	void SetPresentationTimeScale(float Scale) { PresentationTimeScale = FMath::Clamp(Scale, 0.f, 1.f); }
	float GetPresentationTimeScale() const { return PresentationTimeScale; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

private:
	float PresentationTimeScale = 1.f;
};
