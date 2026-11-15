#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACFTransitionTypes.h"
#include "ACFTransitionTestListener.generated.h"

/** Phase 7D (tests only): receives UACFTransitionSubsystem::OnTransitionEvent like a Blueprint binding does (dynamic delegate). */
UCLASS(NotBlueprintable, HideDropdown, Transient)
class UACFTransitionTestListener : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void(FACFTransitionHandle, EACFTransitionEvent)> Callback;

	UFUNCTION()
	void OnTransitionEvent(FACFTransitionHandle Handle, EACFTransitionEvent Event)
	{
		if (Callback)
		{
			Callback(Handle, Event);
		}
	}
};
