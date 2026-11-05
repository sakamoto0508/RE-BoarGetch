#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ACFCameraSettings.generated.h"

class UACFCameraOutputModifier;

/** Project Settings > Plugins > Action Camera Framework. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Action Camera Framework"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UACFCameraSettings();

	/**
	 * Modifier class added to each local player's PlayerCameraManager. Subclass
	 * UACFCameraOutputModifier to change its Priority relative to other camera modifiers.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Output")
	TSoftClassPtr<UACFCameraOutputModifier> OutputModifierClass;

	/** Log request add / release / transitions (LogActionCamera, Verbose otherwise). */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bLogRequests = false;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	TSubclassOf<UACFCameraOutputModifier> ResolveOutputModifierClass() const;
};
