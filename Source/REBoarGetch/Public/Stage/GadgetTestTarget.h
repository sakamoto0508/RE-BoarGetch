#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/AttackActivatable.h"
#include "GadgetTestTarget.generated.h"

class UStaticMeshComponent;

/** ガジェット試験場の的。攻撃が当たると一定時間だけ色が変わります。 */
UCLASS()
class REBOARGETCH_API AGadgetTestTarget : public AActor, public IAttackActivatable
{
	GENERATED_BODY()

public:
	AGadgetTestTarget();

	virtual void ActivateByAttack_Implementation(AActor* InstigatorActor, AActor* Source) override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 当たった後、元の色へ戻すまでの秒数。 */
	UPROPERTY(EditAnywhere, Category = "Gadget Test", meta = (ClampMin = "0", Units = "s"))
	float HitColorDuration = .6f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	FTimerHandle ResetTimer;

	void ResetColor();
};
