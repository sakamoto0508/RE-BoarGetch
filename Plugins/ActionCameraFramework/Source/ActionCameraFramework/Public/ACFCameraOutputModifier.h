#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "ACFCameraOutputModifier.generated.h"

class UACFCameraSubsystem;

/** Shared plumbing of the camera modifiers ACF adds to a PlayerCameraManager. */
UCLASS(Abstract, Transient)
class ACTIONCAMERAFRAMEWORK_API UACFCameraModifierBase : public UCameraModifier
{
	GENERATED_BODY()

public:
	void SetSubsystem(UACFCameraSubsystem* InSubsystem);
	UACFCameraSubsystem* GetSubsystem() const;
	APlayerCameraManager* GetOwningCameraManager() const { return CameraOwner; }

protected:
	/**
	 * いま PCM が更新している ViewTarget。APlayerCameraManager::UpdateViewTarget は ViewTarget の POV を参照のまま
	 * Modifier へ渡すので、アドレスの一致で正確に分かる（ViewTarget Blend 中は切替元と行き先で 1 回ずつ呼ばれる）。
	 * 分からない場合（PCM の派生クラスが別の POV を渡したなど）は null。
	 */
	AActor* GetViewTargetBeingUpdated(const FMinimalViewInfo& POV) const;

protected:
	TWeakObjectPtr<UACFCameraSubsystem> Subsystem;
};

/**
 * Output adapter: the only place where ACF writes its camera into Unreal's camera pipeline.
 *
 * Added to the existing APlayerCameraManager of the local player (no PCM subclass required).
 * APlayerCameraManager::UpdateViewTarget calls ApplyCameraModifiers after the view target's
 * CalcCamera, so this modifier receives the pose UE would have used, asks the subsystem for the
 * (once-per-frame) ACF result, and blends between them by the result's Weight.
 *
 * Default Priority is 64 so ACF runs before UCameraModifier_CameraShake (127): UE camera shakes
 * keep working on top of ACF cameras. Modifiers with a priority below 64 run before ACF and are
 * overridden while ACF has full weight. Create a Blueprint / C++ subclass to change the priority
 * and select it in Project Settings > Plugins > Action Camera Framework.
 *
 * Always returns false from ModifyCamera so lower priority modifiers still run.
 */
UCLASS(Blueprintable, Transient)
class ACTIONCAMERAFRAMEWORK_API UACFCameraOutputModifier : public UACFCameraModifierBase
{
	GENERATED_BODY()

public:
	UACFCameraOutputModifier();

	//~ UCameraModifier
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;
	virtual void DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos) override;
};

/**
 * Final position guard (Priority 255, i.e. after every standard modifier including camera shakes).
 *
 * ACF's Collision node constrains the ACF camera, but two later steps can still move the final
 * camera into geometry: the Weight blend between the UE camera and the ACF camera (a straight
 * interpolation between two valid positions can cross a pillar) and position offsets added by
 * camera shakes / other modifiers after ACF. While ACF has influence and the camera in control has
 * a Collision node, this modifier re-sweeps from that camera's pivot to the final position with the
 * same probe and clamps it. Stateless (no damping), position only. Disable in Project Settings.
 */
UCLASS(Transient)
class ACTIONCAMERAFRAMEWORK_API UACFCameraCollisionGuardModifier : public UACFCameraModifierBase
{
	GENERATED_BODY()

public:
	UACFCameraCollisionGuardModifier();

	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;
};
