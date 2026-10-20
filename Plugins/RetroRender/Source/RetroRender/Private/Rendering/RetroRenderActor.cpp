#include "Rendering/RetroRenderActor.h"
#include "Rendering/RetroRenderComponent.h"

ARetroRenderActor::ARetroRenderActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Renderer = CreateDefaultSubobject<URetroRenderComponent>(TEXT("RetroRenderer"));
	Renderer->InitialPreset = ERetroRenderPreset::PS1;
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif
}
