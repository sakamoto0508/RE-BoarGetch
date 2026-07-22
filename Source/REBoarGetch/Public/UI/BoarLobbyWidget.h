#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarLobbyWidget.generated.h"

/** ステージ選択、装備変更、解放確認を行うロビーUIの基底Widgetです。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarLobbyWidget : public UUserWidget
{
	GENERATED_BODY()
};
