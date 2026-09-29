#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/AttackActivatable.h"
#include "GadgetTestTarget.generated.h"
class UStaticMeshComponent;
UCLASS()
class REBOARGETCH_API AGadgetTestTarget : public AActor, public IAttackActivatable
{
    GENERATED_BODY()
public:
    AGadgetTestTarget();
    virtual void ActivateByAttack_Implementation(AActor* InstigatorActor,AActor* Source) override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    FTimerHandle ResetTimer;
    void ResetColor();
};
