#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACFCameraSequence.h"
#include "ACFCameraSequenceTestListener.generated.h"

/** Tests only: receives UACFCameraSubsystem::OnCameraSequenceEvent like a Blueprint binding does (dynamic delegate). */
UCLASS(NotBlueprintable, HideDropdown, Transient)
class UACFCameraSequenceTestListener : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void(const FACFCameraSequenceEvent&)> Callback;

	UFUNCTION()
	void OnSequenceEvent(const FACFCameraSequenceEvent& Event)
	{
		if (Callback)
		{
			Callback(Event);
		}
	}
};
