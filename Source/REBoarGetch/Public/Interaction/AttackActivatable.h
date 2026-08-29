#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "AttackActivatable.generated.h"

class AActor;

/** BlueprintとC++のActorへ攻撃起動処理を実装可能にするInterface型です。 */
UINTERFACE(BlueprintType)
class REBOARGETCH_API UAttackActivatable : public UInterface
{
	GENERATED_BODY()
};

/** ガジェットなどの攻撃が命中したActorを共通手順で起動するための契約です。 */
class REBOARGETCH_API IAttackActivatable
{
	GENERATED_BODY()

public:
	/** 攻撃ヒットでオブジェクトを起動します。 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "REBoarGetch|Interaction")
	void ActivateByAttack(AActor* AttackInstigator, AActor* AttackSource);
};
