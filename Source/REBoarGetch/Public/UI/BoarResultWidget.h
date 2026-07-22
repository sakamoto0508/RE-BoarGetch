#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarResultWidget.generated.h"

/** ステージ終了時のプレイ結果を表示する基底Widgetです。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarResultWidget : public UUserWidget
{
	GENERATED_BODY()
};
