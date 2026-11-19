#include "Stage/GadgetCollectionPod.h"
#include "Components/ChildActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/TextRenderActor.h"
#include "Animation/SkeletalMeshActor.h"
#include "Core/BoarGameInstance.h"
#include "Gadget/GadgetBase.h"
#include "GadgetDataAsset.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Stage/FacilitySign.h"

AGadgetCollectionPod::AGadgetCollectionPod()
{
    PrimaryActorTick.bCanEverTick = false;
    PodMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PodMesh"));
    SetRootComponent(PodMesh);
    PodMesh->SetCollisionProfileName(TEXT("NoCollision"));
    PodMesh->SetGenerateOverlapEvents(false);
    PodMesh->SetCanEverAffectNavigation(false);
    Display = CreateDefaultSubobject<UChildActorComponent>(TEXT("Display"));
    Display->SetupAttachment(PodMesh);
    DisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
    DisplayMesh->SetupAttachment(Display);
    DisplayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DisplayMesh->SetCanEverAffectNavigation(false);
    Nameplate = CreateDefaultSubobject<UChildActorComponent>(TEXT("Nameplate"));
    Nameplate->SetupAttachment(PodMesh);
}

void AGadgetCollectionPod::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Display->SetChildActorClass(DisplayActorClass.Get());
    DisplayMesh->SetHiddenInGame(DisplayActorClass != nullptr);
    ApplyGlassMaterial();
}

void AGadgetCollectionPod::ApplyGlassMaterial()
{
    if (!GlassMaterial || GlassMaterialSlots.IsEmpty())
        return;
    UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(GlassMaterial, this);
    Material->SetScalarParameterValue(TEXT("Opacity"), GlassOpacity);
    Material->SetVectorParameterValue(TEXT("GlassColor"), GlassTint);
    for (const int32 Slot : GlassMaterialSlots)
        if (Slot >= 0 && Slot < PodMesh->GetNumMaterials())
            PodMesh->SetMaterial(Slot, Material);
}

void AGadgetCollectionPod::BeginPlay()
{
    Super::BeginPlay();
    Display->SetChildActorClass(DisplayActorClass.Get());
    ApplyGlassMaterial();
    if(auto* GI=GetGameInstance<UBoarGameInstance>())
        GI->OnGadgetsUnlocked.AddUniqueDynamic(this,&AGadgetCollectionPod::RefreshDisplay);
    RefreshDisplay();
}
void AGadgetCollectionPod::EndPlay(const EEndPlayReason::Type Reason)
{
    if(auto* GI=GetGameInstance<UBoarGameInstance>())
        GI->OnGadgetsUnlocked.RemoveDynamic(this,&AGadgetCollectionPod::RefreshDisplay);
    Super::EndPlay(Reason);
}
void AGadgetCollectionPod::RefreshDisplay()
{
    const auto* GI=GetGameInstance<UBoarGameInstance>();
    const auto Class=GI ? GI->FindGadgetClass(GadgetId) : nullptr;
    const auto* Def=Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
    const bool bUnlocked=Def && GI->IsGadgetUnlocked(GadgetId);
    DisplayMesh->SetHiddenInGame(!bUnlocked || DisplayActorClass != nullptr);
    if (AActor* OwnedDisplay = Display->GetChildActor())
    {
        OwnedDisplay->SetActorHiddenInGame(!bUnlocked);
        OwnedDisplay->SetActorEnableCollision(false);
    }
    for (AActor* Actor : DisplayActors)
        if (Actor)
        {
            Actor->SetActorHiddenInGame(!bUnlocked);
            Actor->SetActorEnableCollision(false);
        }
    const FText Status = Def
        ? FText::Format(NSLOCTEXT("GadgetCollectionPod", "Status", "{0} / {1}"), Def->DisplayName,
                        bUnlocked ? NSLOCTEXT("GadgetCollectionPod", "Unlocked", "UNLOCKED")
                                  : NSLOCTEXT("GadgetCollectionPod", "Locked", "LOCKED"))
        : NSLOCTEXT("GadgetCollectionPod", "Unregistered", "UNREGISTERED");
    if (auto* Sign = Cast<AFacilitySign>(Nameplate->GetChildActor()))
        Sign->SetDisplayText(Status, !bUnlocked);
    if (StatusLabel)
        StatusLabel->GetTextRender()->SetText(Status);
}
