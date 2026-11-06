#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "ACFCameraDebugShake.generated.h"

/**
 * Debug / sample camera shake used by "ACF.Sample.Shake": a visible location + roll oscillation
 * (2 s, fades out) to check how UE camera shakes interact with ACF Collision and the output guard.
 * Not meant for gameplay; hidden from class pickers.
 */
UCLASS(NotBlueprintable, HideDropdown, Transient)
class UACFDebugCameraShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	/** Camera-local amplitude (cm). X = forward/back, Y = sideways, Z = up/down. */
	UPROPERTY()
	FVector LocationAmplitude = FVector(50.f, 30.f, 20.f);

	UPROPERTY()
	float RollAmplitude = 4.f;

	UPROPERTY()
	float Frequency = 5.f;

	UPROPERTY()
	float Duration = 2.f;

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override { OutInfo.Duration = FCameraShakeDuration(Duration); }
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override { Elapsed = 0.f; }
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override
	{
		Elapsed += Params.DeltaTime;
		const float Fade = FMath::Clamp(1.f - Elapsed / FMath::Max(Duration, 0.01f), 0.f, 1.f);
		const float Phase = 2.f * UE_PI * Frequency * Elapsed;
		OutResult.Location = FVector(FMath::Sin(Phase) * LocationAmplitude.X, FMath::Sin(Phase * 1.3f) * LocationAmplitude.Y, FMath::Sin(Phase * 0.7f) * LocationAmplitude.Z) * Fade;
		OutResult.Rotation = FRotator(0.f, 0.f, FMath::Sin(Phase) * RollAmplitude * Fade);
	}
	// "Stop ACF Camera Effect" (non-immediate) ends this sample shake right away (it has no blend out).
	virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params) override { Elapsed = Duration; }
	virtual bool IsFinishedImpl() const override { return Elapsed >= Duration; }

	float Elapsed = 0.f;
};

UCLASS(NotBlueprintable, HideDropdown)
class UACFDebugCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UACFDebugCameraShake(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
		SetRootShakePattern(ObjectInitializer.CreateDefaultSubobject<UACFDebugCameraShakePattern>(this, TEXT("Pattern")));
	}
};
