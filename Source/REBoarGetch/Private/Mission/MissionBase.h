#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MissionBase.generated.h"

/** ミッション定義・進行処理を追加するための基底クラスです。 */
UCLASS(BlueprintType, Blueprintable)
class UMissionBase : public UObject
{
	GENERATED_BODY()
};
