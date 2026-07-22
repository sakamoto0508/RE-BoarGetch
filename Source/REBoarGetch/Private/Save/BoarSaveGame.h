#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "BoarSaveGame.generated.h"

/** セーブ対象データを追加するための基底SaveGameです。 */
UCLASS()
class UBoarSaveGame : public USaveGame
{
	GENERATED_BODY()
};
