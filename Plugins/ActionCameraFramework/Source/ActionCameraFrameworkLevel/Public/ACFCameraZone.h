#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ACFCameraZone.generated.h"

class UBoxComponent;
class UACFCameraZoneComponent;

/**
 * Phase 7C-1A: a box Camera Zone to place in a level (README 32 章). The box is only the shape of the zone (no collision,
 * hidden in game); the logic is the UACFCameraZoneComponent, which can also be added to other actors with a Box or
 * Sphere component.
 */
UCLASS(meta = (DisplayName = "ACF Camera Zone"))
class ACTIONCAMERAFRAMEWORKLEVEL_API AACFCameraZone : public AActor
{
	GENERATED_BODY()

public:
	AACFCameraZone();

	UBoxComponent* GetBox() const { return Box; }
	UACFCameraZoneComponent* GetZone() const { return Zone; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Zone")
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(VisibleAnywhere, Category = "Zone")
	TObjectPtr<UACFCameraZoneComponent> Zone;
};
