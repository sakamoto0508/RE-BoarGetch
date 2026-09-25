#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BoarFlowGlowLibrary.generated.h"
class UUserWidget;

/** Keeps existing Widget classes and input delegates intact. */
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
