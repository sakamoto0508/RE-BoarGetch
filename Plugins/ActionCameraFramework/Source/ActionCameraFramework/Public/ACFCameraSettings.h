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

	/**
	 * Re-apply the Collision constraint of the camera in control to the FINAL camera position, after the
	 * Weight blend with the UE camera and after camera shakes / other modifiers (adds a Priority 255
	 * modifier). Prevents interpolation and shake offsets from pushing the camera into walls.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Output")
	bool bEnableOutputCollisionGuard = true;

	/** Camera effects may not push the FOV outside this range (a base FOV already outside is left alone). */
	UPROPERTY(Config, EditAnywhere, Category = "Effects", meta = (ClampMin = "1.0", ClampMax = "170.0", Units = deg))
	float EffectFovMin = 20.f;

	UPROPERTY(Config, EditAnywhere, Category = "Effects", meta = (ClampMin = "1.0", ClampMax = "170.0", Units = deg))
	float EffectFovMax = 130.f;

	/** Presets used by Play ACF Camera Impulse / Recoil when no preset is passed (built-in defaults if empty). */
	UPROPERTY(Config, EditAnywhere, Category = "Effects")
	TSoftObjectPtr<class UACFCameraEffectPreset> DefaultImpulsePreset;

	UPROPERTY(Config, EditAnywhere, Category = "Effects")
	TSoftObjectPtr<class UACFCameraEffectPreset> DefaultRecoilPreset;

	/** Log request add / release / transitions (LogActionCamera, Verbose otherwise). */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bLogRequests = false;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	TSubclassOf<UACFCameraOutputModifier> ResolveOutputModifierClass() const;
};
