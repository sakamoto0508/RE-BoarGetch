#include "Stage/HubPortal.h"
#include "Components/BoxComponent.h"
#include "Player/BoarPlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "Core/BoarGameInstance.h"
#include "Gadget/GadgetBase.h"
#include "GadgetDataAsset.h"
#include "Engine/TextRenderActor.h"
#include "Components/TextRenderComponent.h"
AHubPortal::AHubPortal()
{
    PrimaryActorTick.bCanEverTick=false;
    Trigger=CreateDefaultSubobject<UBoxComponent>(TEXT("PortalTrigger"));SetRootComponent(Trigger);
    Trigger->SetBoxExtent(FVector(140,80,150));
    Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    Trigger->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    Trigger->SetCanEverAffectNavigation(false);
    Trigger->OnComponentBeginOverlap.AddDynamic(this,&AHubPortal::Enter);
}
void AHubPortal::Enter(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent*,int32,bool,const FHitResult&)
{
    const APawn* Pawn=Cast<APawn>(Other);
    auto* PC=Pawn?Cast<ABoarPlayerController>(Pawn->GetController()):nullptr;
    if(!PC||!PC->IsLocalController()||bTravelRequested)return;
    if(Action==EHubPortalAction::GadgetTest)
    {
        auto* GI=GetGameInstance<UBoarGameInstance>();
        if(!GI || !GI->IsInGadgetLab())return;
        Destination=GI->GetSelectedTestLevel();
    }
    else if(Action!=EHubPortalAction::Travel){if(!bRequiresInteraction)PC->OpenFacilityMenu(Action==EHubPortalAction::Archive);return;}
    // PIE rewrites soft references to visited worlds. Validate and travel using
    // the on-disk package, never the transient UEDPIE_<instance>_ package.
    const FString PackageName=UWorld::RemovePIEPrefix(Destination.ToSoftObjectPath().GetLongPackageName());
    if(Destination.IsNull()||!FPackageName::DoesPackageExist(PackageName))
    {UE_LOG(LogTemp,Error,TEXT("[HubPortal] Missing destination: %s"),*Destination.ToString());return;}
    bTravelRequested=true;
    if(bClearTestTargetOnTravel)
        if(auto* GI=GetGameInstance<UBoarGameInstance>())GI->ClearTestGadget();
    UGameplayStatics::OpenLevel(this,FName(*PackageName));
}
void AHubPortal::BeginPlay()
{
    Super::BeginPlay();
    if(Action==EHubPortalAction::GadgetTest)
    {
        if(auto* GI=GetGameInstance<UBoarGameInstance>())
        {
            GI->OnTestGadgetChanged.AddUniqueDynamic(this,&AHubPortal::RefreshTestTarget);
            GI->OnGadgetsUnlocked.AddUniqueDynamic(this,&AHubPortal::RefreshTestTarget);
        }
        RefreshTestTarget();
    }
}
void AHubPortal::Interact(ABoarPlayerController* Controller)
{
    if(!bRequiresInteraction || !Controller || !Controller->IsLocalController() ||
       !Controller->GetPawn() || !Trigger->IsOverlappingActor(Controller->GetPawn()))return;
    if(Action==EHubPortalAction::Loadout || Action==EHubPortalAction::Archive)
        Controller->OpenFacilityMenu(Action==EHubPortalAction::Archive);
}
void AHubPortal::EndPlay(const EEndPlayReason::Type Reason)
{
    if(auto* GI=GetGameInstance<UBoarGameInstance>())
    {
        GI->OnTestGadgetChanged.RemoveDynamic(this,&AHubPortal::RefreshTestTarget);
        GI->OnGadgetsUnlocked.RemoveDynamic(this,&AHubPortal::RefreshTestTarget);
    }
    Super::EndPlay(Reason);
}
void AHubPortal::RefreshTestTarget()
{
    auto* GI=GetGameInstance<UBoarGameInstance>();
    for(int32 I=0;I<CollectionLabels.Num();++I)
    {
        auto* Label=CollectionLabels[I].Get();if(!Label)continue;
        const auto Class=GI && GI->GadgetCatalog.IsValidIndex(I) ? GI->GadgetCatalog[I] : nullptr;
        const auto* Def=Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
        Label->SetActorHiddenInGame(!Def);
        if(Def)Label->GetTextRender()->SetText(FText::Format(FText::FromString(TEXT("{0}  {1}")),FText::FromString(GI->IsGadgetUnlocked(Def->GadgetId)?TEXT("READY"):TEXT("LOCKED")),Def->DisplayName));
    }
    const bool bShow=GI && GI->IsInGadgetLab() && !GI->GetSelectedTestGadget().IsNone() && GI->IsGadgetUnlocked(GI->GetSelectedTestGadget());
    SetActorHiddenInGame(false);
    const bool bHasLevel=bShow && !GI->GetSelectedTestLevel().IsNull();
    Trigger->SetCollisionEnabled(bHasLevel ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    for(const auto& A:TestVisualActors)if(A)A->SetActorHiddenInGame(!bHasLevel);
    for(const auto& A:TestBarrierActors)if(A)
    {
        A->SetActorHiddenInGame(bHasLevel);
        A->SetActorEnableCollision(!bHasLevel);
    }
    if(TestLabel)
    {
        TestLabel->SetActorHiddenInGame(false);
        const auto Class=GI ? GI->FindGadgetClass(GI->GetSelectedTestGadget()) : nullptr;
        const auto* Def=Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
        TestLabel->GetTextRender()->SetText(Def ? FText::Format(FText::FromString(TEXT("TEST : {0}{1}")),Def->DisplayName,FText::FromString(bHasLevel ? TEXT("") : TEXT(" - Level未登録"))) : FText::FromString(TEXT("SELECT GADGET TO TEST")));
    }
}
