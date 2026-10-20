#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RetroRenderActor.generated.h"
class URetroRenderComponent;

// Drop into a level to apply a retro preset there (editor viewport preview + PIE/game). No debug UI.
UCLASS(meta=(DisplayName="Retro Render Settings"))
class RETRORENDER_API ARetroRenderActor : public AActor
{
	GENERATED_BODY()
public:
	ARetroRenderActor();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Retro") TObjectPtr<URetroRenderComponent> Renderer;
};
