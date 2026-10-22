#include "Rendering/RetroRenderWorldSubsystem.h"
#include "Rendering/RetroRenderActor.h"
#include "Rendering/RetroRenderComponent.h"
#include "Rendering/RetroRenderSettings.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogRetroAutoApply, Log, All);

URetroRenderSettings::URetroRenderSettings() = default;

bool URetroRenderWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void URetroRenderWorldSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	const URetroRenderSettings* Settings = GetDefault<URetroRenderSettings>();
	if (!Settings->bAutoApplyInGame || World.GetNetMode() == NM_DedicatedServer) return;
	// PIE package names carry a UEDPIE_N_ prefix; compare against the original map.
	const FString MapPackage = UWorld::RemovePIEPrefix(World.GetOutermost()->GetName());
	for (const FSoftObjectPath& Excluded : Settings->ExcludedMaps)
	{
		if (Excluded.GetLongPackageName() == MapPackage) return;
	}
	if (URetroRenderComponent::FindWorldOwner(&World))
	{
		UE_LOG(LogRetroAutoApply, Log, TEXT("%s has its own Retro Render actor; project default not applied."), *MapPackage);
		return;
	}
	FActorSpawnParameters Params;
	Params.Name = TEXT("RetroRender_ProjectDefault");
	Params.ObjectFlags |= RF_Transient;
	Params.bDeferConstruction = true;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARetroRenderActor* Actor = World.SpawnActor<ARetroRenderActor>(ARetroRenderActor::StaticClass(), FTransform::Identity, Params);
	if (!Actor) return;
	Actor->Renderer->InitialPreset = Settings->DefaultPreset;
	Actor->Renderer->bCRTOverlay = Settings->bDefaultCRTOverlay;
	Actor->FinishSpawning(FTransform::Identity);
	AutoActor = Actor;
	UE_LOG(LogRetroAutoApply, Log, TEXT("%s: project default %s%s applied."), *MapPackage, *RetroPresetName(Settings->DefaultPreset),
		Settings->bDefaultCRTOverlay ? TEXT(" + CRT") : TEXT(""));
}
