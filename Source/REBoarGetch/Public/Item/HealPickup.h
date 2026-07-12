#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HealPickup.generated.h"

class USphereComponent;
class UPrimitiveComponent;
struct FHitResult;

UCLASS()
class REBOARGETCH_API AHealPickup : public AActor
{
	GENERATED_BODY()

public:
	AHealPickup();

private:
	/// <summary>プレイヤー接触時に回復を適用して自身を削除します。</summary>
	UFUNCTION()
	void OnPickupBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/// <summary>取得判定用の当たり判定です。</summary>
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> PickupCollision;

	/// <summary>取得時に回復するHP量です。</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pickup", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float HealAmount = 1.0f;
};
