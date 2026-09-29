#include "Stage/GadgetCollectionPod.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/TextRenderActor.h"
#include "Core/BoarGameInstance.h"
#include "Gadget/GadgetBase.h"
#include "GadgetDataAsset.h"

AGadgetCollectionPod::AGadgetCollectionPod()
{
    PrimaryActorTick.bCanEverTick=false;
    PodMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PodMesh"));SetRootComponent(PodMesh);
    PodMesh->SetCollisionProfileName(TEXT("NoCollision"));
    PodMesh->SetGenerateOverlapEvents(false);PodMesh->SetCanEverAffectNavigation(false);
}
void AGadgetCollectionPod::BeginPlay()
{
    Super::BeginPlay();
    if(auto* GI=GetGameInstance<UBoarGameInstance>())GI->OnGadgetsUnlocked.AddUniqueDynamic(this,&AGadgetCollectionPod::RefreshDisplay);
    RefreshDisplay();
}
void AGadgetCollectionPod::EndPlay(const EEndPlayReason::Type Reason)
{
    if(auto* GI=GetGameInstance<UBoarGameInstance>())GI->OnGadgetsUnlocked.RemoveDynamic(this,&AGadgetCollectionPod::RefreshDisplay);
    Super::EndPlay(Reason);
}
void AGadgetCollectionPod::RefreshDisplay()
{
    const auto* GI=GetGameInstance<UBoarGameInstance>();
    const auto Class=GI ? GI->FindGadgetClass(GadgetId) : nullptr;
    const auto* Def=Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
    const bool bUnlocked=Def && GI->IsGadgetUnlocked(GadgetId);
    for(const auto& Actor:DisplayActors)if(Actor)
    {Actor->SetActorHiddenInGame(!bUnlocked);Actor->SetActorEnableCollision(false);}
    if(StatusLabel)StatusLabel->GetTextRender()->SetText(Def ? FText::Format(
        FText::FromString(TEXT("{0} / {1}")),Def->DisplayName,
        FText::FromString(bUnlocked?TEXT("UNLOCKED"):TEXT("LOCKED"))) : FText::FromString(TEXT("UNREGISTERED")));
}
