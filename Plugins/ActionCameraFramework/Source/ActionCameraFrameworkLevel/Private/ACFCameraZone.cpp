#include "ACFCameraZone.h"
#include "ACFCameraZoneComponent.h"
#include "Components/BoxComponent.h"

AACFCameraZone::AACFCameraZone()
{
	PrimaryActorTick.bCanEverTick = false;

	// The box is only the zone's shape: no collision, no overlap events (the zone subsystem tests the pawn location), not
	// visible in game. In the editor it is drawn as a wire box.
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(500.f, 500.f, 300.f));
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetGenerateOverlapEvents(false);
	Box->SetCanEverAffectNavigation(false);
	Box->SetHiddenInGame(true);
	Box->ShapeColor = FColor(80, 200, 255);
	RootComponent = Box;

	Zone = CreateDefaultSubobject<UACFCameraZoneComponent>(TEXT("Zone"));
}
