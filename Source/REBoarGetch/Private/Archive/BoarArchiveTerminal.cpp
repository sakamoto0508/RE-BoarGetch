#include "Archive/BoarArchiveTerminal.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Player/BoarPlayerController.h"
#include "GameFramework/Pawn.h"
#include "Core/BoarFacilityGameMode.h"
#include "Engine/World.h"
ABoarArchiveTerminal::ABoarArchiveTerminal()
{
    Action=EHubPortalAction::Archive;bRequiresInteraction=true;
    TerminalVisual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TerminalVisual"));TerminalVisual->SetupAttachment(Trigger);
    TerminalVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);TerminalVisual->SetCanEverAffectNavigation(false);
}
void ABoarArchiveTerminal::Interact(ABoarPlayerController* Controller)
{
    const auto* Mode=GetWorld()->GetAuthGameMode<ABoarFacilityGameMode>();
    if(!Mode || !Mode->bArchive || !Controller || !Controller->IsLocalController() || !Controller->GetPawn() ||
       !Trigger->IsOverlappingActor(Controller->GetPawn()))return;
    if(bDisplaySelection)Controller->OpenArchiveDisplaySelection();else Controller->OpenFacilityMenu(true);
}
