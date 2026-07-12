#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "AttackActivatable.generated.h"

class AActor;

UINTERFACE(BlueprintType)
class REBOARGETCH_API UAttackActivatable : public UInterface
{
	GENERATED_BODY()
};

class REBOARGETCH_API IAttackActivatable
{
	GENERATED_BODY()

public:
	///<summary> 攻撃ヒットでオブジェクトを起動します。 </summary>
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "REBoarGetch|Interaction")
	void ActivateByAttack(AActor* AttackInstigator, AActor* AttackSource);
};
