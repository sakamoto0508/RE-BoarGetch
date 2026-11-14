#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "Effects/ACFCameraEffectTypes.h"
#include "ACFCameraEffectStack.generated.h"

class UACFCameraEffect;
class UACFCameraEffectPreset;

/** Runtime state of one layer (one UACFCameraEffect) of a played preset. */
USTRUCT()
struct FACFCameraEffectLayerInstance
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UACFCameraEffect> Effect = nullptr;

	/** Per-play state of the effect (shake instance, recoil spread, motion history...). */
	UPROPERTY()
	FInstancedStruct State;

	float Time = 0.f;
	bool bStarted = false;
	bool bReleasing = false;
	float ReleaseStart = 0.f;
	float ReleaseFrom = 1.f;
	bool bFinished = false;

	/** This frame: raw output and final weight (envelope x weight x scale x attenuation x curve). */
	FACFCameraEffectOutput Output;
	float FinalWeight = 0.f;
};

/** Runtime state of one played effect preset (one handle). Removed (and its state freed) when all layers end. */
USTRUCT()
struct FACFCameraEffectInstance
{
	GENERATED_BODY()

	FACFCameraEffectHandle Handle;

	UPROPERTY()
	TObjectPtr<const UACFCameraEffectPreset> Preset = nullptr;

	UPROPERTY()
	FACFCameraEffectParams Params;

	UPROPERTY()
	TArray<FACFCameraEffectLayerInstance> Layers;

	FVector LastSourceLocation = FVector::ZeroVector;
	bool bHasLastSource = false;
	uint64 Sequence = 0;
	bool bTracksInstigator = false;
	bool bStopping = false;
};

/**
 * Active camera effects of one local player (owned by the director).
 *
 * Pipeline position (see README / Architecture):
 *   rig -> request blend -> Evaluate + ApplyPreCollision (Location) -> Collision -> ApplyPostCollision (Rotation, FOV)
 * Evaluate advances time once per frame (it runs inside the director's frame guard).
 */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraEffectStack
{
	GENERATED_BODY()

	/** Invalid handle (and a warning) for a null / empty preset. */
	FACFCameraEffectHandle Play(const UACFCameraEffectPreset* Preset, const FACFCameraEffectParams& Params);

	/** Release (or cut) one effect. False for unknown / already finished handles. */
	bool Stop(FACFCameraEffectHandle Handle, bool bImmediate);
	void StopAll(bool bImmediate);

	bool IsActive(FACFCameraEffectHandle Handle) const;
	int32 Num() const { return Instances.Num(); }
	void GetInfos(TArray<FACFCameraEffectInfo>& Out) const;

	/** Advances every layer and computes its output for Camera (the pose the effects are applied to). */
	void Evaluate(const FACFCameraEvaluationContext& Context, const FACFCameraPose& Camera);

	/** Location channel (before Collision). Camera is the same pose given to Evaluate. */
	void ApplyPreCollision(FACFCameraPose& InOutPose, const FACFCameraPose& Camera) const;

	/** Rotation + FOV channels (after Collision). The FOV is clamped to [FovMin, FovMax] when an FOV layer is active. */
	void ApplyPostCollision(FACFCameraPose& InOutPose, float FovMin, float FovMax) const;

private:
	void EndLayer(FACFCameraEffectLayerInstance& Layer);

	UPROPERTY()
	TArray<FACFCameraEffectInstance> Instances;

	uint64 NextSequence = 0;
};
