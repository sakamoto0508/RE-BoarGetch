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
	/** プレイヤー接触時に回復を適用して自身を削除します。 */
	UFUNCTION()
	void OnPickupBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** 取得判定用の当たり判定です。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> PickupCollision;

	/** 取得時に回復するHP量です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pickup", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float HealAmount = 1.0f;
};
