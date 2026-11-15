#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACFCameraZoneComponent.h"
#include "GameFramework/PlayerController.h"
#include "ACFCameraZoneTestListener.generated.h"

/** Phase 7C-3B (tests only): receives UACFCameraZoneSubsystem::OnZoneChanged like a Blueprint binding does. */
UCLASS(NotBlueprintable, HideDropdown, Transient)
class UACFCameraZoneTestListener : public UObject
{
	GENERATED_BODY()

public:
	struct FCall
	{
		TWeakObjectPtr<APlayerController> PlayerController;
		TWeakObjectPtr<UACFCameraZoneComponent> Previous;
		TWeakObjectPtr<UACFCameraZoneComponent> New;
	};
	TArray<FCall> Calls;
	/** Phase C: optional reaction (re-entrant calls from a Blueprint-like listener). */
	TFunction<void(APlayerController*, UACFCameraZoneComponent*, UACFCameraZoneComponent*)> Callback;

	UFUNCTION()
	void OnZoneChanged(APlayerController* PlayerController, UACFCameraZoneComponent* PreviousZone, UACFCameraZoneComponent* NewZone)
	{
		Calls.Add({ PlayerController, PreviousZone, NewZone });
		if (Callback)
		{
			Callback(PlayerController, PreviousZone, NewZone);
		}
	}
};
