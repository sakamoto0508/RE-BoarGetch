#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "ACFCameraOutputModifier.generated.h"

class UACFCameraSubsystem;

/**
 * Output adapter: the only place where ACF writes into Unreal's camera pipeline.
 *
 * Added to the existing APlayerCameraManager of the local player (no PCM subclass required).
 * APlayerCameraManager::UpdateViewTarget calls ApplyCameraModifiers after the view target's
 * CalcCamera, so this modifier receives the pose UE would have used, asks the subsystem for the
 * (once-per-frame) ACF result, and blends between them by the result's Weight.
 *
 * Default Priority is 64 so ACF runs before UCameraModifier_CameraShake (127): UE camera shakes
 * keep working on top of ACF cameras. Create a Blueprint / C++ subclass to change the priority
 * and select it in Project Settings > Plugins > Action Camera Framework.
 *
 * Always returns false from ModifyCamera so lower priority modifiers still run.
 */
UCLASS(Blueprintable, Transient)
class ACTIONCAMERAFRAMEWORK_API UACFCameraOutputModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	UACFCameraOutputModifier();

	void SetSubsystem(UACFCameraSubsystem* InSubsystem);
	UACFCameraSubsystem* GetSubsystem() const;
	APlayerCameraManager* GetOwningCameraManager() const { return CameraOwner; }

	//~ UCameraModifier
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;
	virtual void DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos) override;

private:
	TWeakObjectPtr<UACFCameraSubsystem> Subsystem;
};
