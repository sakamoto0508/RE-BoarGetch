#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BoarFlowGlowLibrary.generated.h"
class UUserWidget;

/** 名前がFocusFrameのImageを使って、各Widget共通の発光枠の寸法と操作状態を更新します。 */
UCLASS()
class REBOARGETCH_API UBoarFlowGlowLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Hologram")
	static void ResizeFlowFrame(UUserWidget* Owner, FVector2D LocalSize);
	UFUNCTION(BlueprintCallable, Category="Hologram")
	static void SetFlowFrameState(UUserWidget* Owner, bool bFocused, bool bValue);
};
