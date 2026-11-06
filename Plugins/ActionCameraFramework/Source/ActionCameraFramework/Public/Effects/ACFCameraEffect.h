#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/Object.h"
#include "Effects/ACFCameraEffectTypes.h"
#include "ACFCameraEffect.generated.h"

struct FInstancedStruct;

/** Inputs of one effect layer evaluation. */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraEffectContext
{
	FACFCameraEffectContext(const FACFCameraEvaluationContext& InEvaluation, const FACFCameraEffectParams& InParams, const FACFCameraPose& InCamera)
		: Evaluation(InEvaluation), Params(InParams), CameraPose(InCamera)
	{
	}

	const FACFCameraEvaluationContext& Evaluation;
	const FACFCameraEffectParams& Params;
	/** Camera the effects are applied to (after request blending, before any effect). */
	const FACFCameraPose& CameraPose;
	/** Seconds since this layer started (already advanced for this frame). */
	float Time = 0.f;
	/** Deterministic per play (Params.RandomSeed or the handle id). */
	int32 Seed = 0;
	/** Source location for attenuation / directional effects (valid if bHasSource). */
	bool bHasSource = false;
	FVector SourceLocation = FVector::ZeroVector;
	/** Actor driving motion-based effects (Speed FOV / Roll): Params.TargetActor or the player's pawn. */
	const AActor* MotionTarget = nullptr;

	float GetDeltaTime() const { return Evaluation.DeltaTime; }
};

/**
 * One layer of a camera effect (Punch, Recoil, Impulse, Shake, Speed FOV, Roll, ...).
 *
 * Like camera nodes, effects are instanced sub-objects of a shared preset and must never change at
 * runtime; per-play state lives in GetRuntimeStateType() (allocated once per play, freed when the
 * effect ends). The stack applies the envelope, weight, scale, attenuation and intensity curve to
 * the raw output and composes all active layers per channel (see EACFCameraEffectBlendMode).
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, BlueprintType)
class ACTIONCAMERAFRAMEWORK_API UACFCameraEffect : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	FACFCameraEffectEnvelope Envelope;

	/** Overrides are applied in ascending priority (the highest ends on top). Additive layers ignore it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect", meta = (ClampMin = "0.0"))
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	EACFCameraEffectBlendMode BlendMode = EACFCameraEffectBlendMode::Additive;

	/** Optional strength over time (seconds since start -> multiplier). Multiplies the envelope. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	TObjectPtr<UCurveFloat> IntensityCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	FACFCameraEffectAttenuation DistanceAttenuation;

	virtual const UScriptStruct* GetRuntimeStateType() const { return nullptr; }

	/** Called on the first evaluation of the play. */
	virtual void OnStart(const FACFCameraEffectContext& Context, FInstancedStruct* State) const {}

	/** Called when the effect is asked to stop (bImmediate = no release). */
	virtual void OnStop(FInstancedStruct* State, bool bImmediate) const {}

	/** Called once when the layer is removed (free resources here). */
	virtual void OnEnd(FInstancedStruct* State) const {}

	/** True if the layer's own lifetime (e.g. a camera shake's blend out) decides when it ends instead of the envelope release. */
	virtual bool HandlesOwnRelease() const { return false; }

	/** Extra end condition (a shake that finished). */
	virtual bool IsFinished(const FInstancedStruct* State) const { return false; }

	/** Raw output (weight 1). Must not advance state when DeltaTime <= 0. */
	virtual void Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const {}
};

/**
 * Reusable camera effect (Cinemachine impulse / noise profile equivalent): one or more layers played
 * together and stopped together through one handle, e.g. "heavy landing" = Impulse + Shake + FOV punch.
 */
UCLASS(BlueprintType)
class ACTIONCAMERAFRAMEWORK_API UACFCameraEffectPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Effect")
	TArray<TObjectPtr<UACFCameraEffect>> Layers;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
