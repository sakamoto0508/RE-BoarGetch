#pragma once

#include "CoreMinimal.h"
#include "ACFTransitionTypes.h"
#include "UObject/Object.h"
#include "ACFTransitionPreviewSettings.generated.h"

/** What the preview tab edits (shown in its details view). */
UCLASS(Transient)
class UACFTransitionPreviewSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Effect that covers the current scene. */
	UPROPERTY(EditAnywhere, Category = "Transition")
	FACFTransitionSettings Close;

	/** Effect that reveals the scene after the hold. Can be a different effect from Close. */
	UPROPERTY(EditAnywhere, Category = "Transition")
	FACFTransitionSettings Open;

	/** Seconds the preview holds the covered screen between Close and Open. */
	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "0", ClampMax = "3"))
	float HoldSeconds = 0.4f;

	/** Repeat Close, Hold and Open in this preview until Stop is pressed. */
	UPROPERTY(EditAnywhere, Category = "Preview")
	bool bLoop = false;
};

