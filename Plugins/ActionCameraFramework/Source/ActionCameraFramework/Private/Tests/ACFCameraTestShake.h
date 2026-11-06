#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "ACFCameraTestShake.generated.h"

/**
 * Test-only camera shake: a constant camera-local location offset (default 60 cm backwards), infinite.
 * Used by ActionCameraFramework.Integration.ShakeVersusCollision to push the camera into a wall
 * deterministically. UHT cannot exclude classes per build configuration, so this tiny class also
 * exists in non-test builds; it is hidden from class pickers.
 */
UCLASS(NotBlueprintable, HideDropdown, Transient)
class UACFTestCameraShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FVector LocalOffset = FVector(-60.f, 0.f, 0.f);

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override { OutInfo.Duration = FCameraShakeDuration::Infinite(); }
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override { OutResult.Location = LocalOffset; }
	virtual bool IsFinishedImpl() const override { return false; }
};

UCLASS(NotBlueprintable, HideDropdown)
class UACFTestCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UACFTestCameraShake(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
		SetRootShakePattern(ObjectInitializer.CreateDefaultSubobject<UACFTestCameraShakePattern>(this, TEXT("Pattern")));
	}
};
