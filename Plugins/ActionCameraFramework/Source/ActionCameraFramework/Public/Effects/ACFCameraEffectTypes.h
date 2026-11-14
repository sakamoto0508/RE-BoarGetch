#pragma once

#include "CoreMinimal.h"
#include "ACFCameraTypes.h"
#include "ACFCameraEffectTypes.generated.h"

class AActor;
class UCurveFloat;

/** Channels an effect writes. Location is applied before Collision, Rotation / FOV after it. */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EACFCameraEffectChannel : uint8
{
	None = 0 UMETA(Hidden),
	Location = 1 << 0,
	Rotation = 1 << 1,
	FOV = 1 << 2,
};
ENUM_CLASS_FLAGS(EACFCameraEffectChannel);

/**
 * How an effect combines with the camera on each channel it writes. Per channel the order is
 * Override (ascending Priority, each lerped by its weight) -> Multiply (FOV only) -> Additive.
 */
UENUM(BlueprintType)
enum class EACFCameraEffectBlendMode : uint8
{
	/** Offset added to the camera (shakes, punches, recoil, roll, speed FOV in degrees). Order independent. */
	Additive,
	/** Absolute value the channel moves towards by the effect weight (e.g. a fixed dash FOV). Highest priority wins. */
	Override,
	/** FOV scale factor (1 = unchanged). Other channels ignore Multiply. */
	Multiply,
};

/** Space of location / rotation offsets. */
UENUM(BlueprintType)
enum class EACFCameraEffectSpace : uint8
{
	/** Relative to the camera (X = forward, Y = right, Z = up). */
	Camera,
	World,
};

UENUM(BlueprintType)
enum class EACFCameraEnvelopeShape : uint8
{
	Linear,
	/** Smoothstep ramps (no velocity pop at the start / end of a ramp). */
	Smooth,
};

/**
 * Attack -> Sustain -> Release intensity envelope (0..1).
 * Sustain < 0 holds until the effect is stopped (or, for shakes, until the shake finishes).
 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraEffectEnvelope
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope", meta = (ClampMin = "0.0", Units = s))
	float Attack = 0.02f;

	/** Seconds at full intensity. Negative = until stopped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope", meta = (Units = s))
	float Sustain = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope", meta = (ClampMin = "0.0", Units = s))
	float Release = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope")
	EACFCameraEnvelopeShape Shape = EACFCameraEnvelopeShape::Smooth;

	bool IsInfinite() const { return Sustain < 0.f; }
	/** Total length, or a negative value when infinite. */
	float GetDuration() const { return IsInfinite() ? -1.f : Attack + Sustain + Release; }

	/** Intensity at Time when not stopped. */
	float Evaluate(float Time) const;
	/** Intensity during a release that started at ReleaseStart from level ReleaseFrom. */
	float EvaluateRelease(float Time, float ReleaseStart, float ReleaseFrom) const;
	float ShapeRamp(float T) const;
};

/** Distance attenuation from a source (explosion, landing point). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraEffectAttenuation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attenuation", meta = (InlineEditConditionToggle))
	bool bEnabled = false;

	/** Full strength up to this distance from the source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attenuation", meta = (EditCondition = "bEnabled", ClampMin = "0.0", Units = cm))
	float InnerRadius = 300.f;

	/** Zero strength from this distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attenuation", meta = (EditCondition = "bEnabled", ClampMin = "0.0", Units = cm))
	float OuterRadius = 2000.f;

	/** Optional falloff: normalised distance (0 = inner, 1 = outer) -> strength. Linear if unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attenuation", meta = (EditCondition = "bEnabled"))
	TObjectPtr<UCurveFloat> Falloff = nullptr;

	float Evaluate(float Distance) const;
};

/** Opaque id of one played effect preset. */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraEffectHandle
{
	GENERATED_BODY()

	FACFCameraEffectHandle() = default;
	explicit FACFCameraEffectHandle(int32 InId) : Id(InId) {}

	bool IsValid() const { return Id != INDEX_NONE; }
	void Invalidate() { Id = INDEX_NONE; }
	int32 GetId() const { return Id; }
	bool operator==(const FACFCameraEffectHandle& Other) const { return Id == Other.Id; }
	bool operator!=(const FACFCameraEffectHandle& Other) const { return Id != Other.Id; }
	FString ToString() const { return IsValid() ? FString::Printf(TEXT("ACFEffect#%d"), Id) : TEXT("ACFEffect#None"); }

private:
	UPROPERTY()
	int32 Id = INDEX_NONE;
};

/** Per-play parameters (the preset stays shared and immutable). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraEffectParams
{
	GENERATED_BODY()

	/** Multiplies every layer's weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect", meta = (ClampMin = "0.0"))
	float Scale = 1.f;

	/** Source for distance attenuation and "from the source" impulses (used when SourceActor is not set). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect", meta = (InlineEditConditionToggle))
	bool bUseSourceLocation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect", meta = (EditCondition = "bUseSourceLocation"))
	FVector SourceLocation = FVector::ZeroVector;

	/** Moving source (weak). Destroyed -> its last known location is kept. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	TWeakObjectPtr<AActor> SourceActor;

	/** Actor whose motion drives Speed FOV / Roll. Empty -> the player's pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	TWeakObjectPtr<AActor> TargetActor;

	/** If set, the effect stops immediately when this object is destroyed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	TWeakObjectPtr<UObject> Instigator;

	/** World direction for impulses without a source (zero = camera up for vertical bumps). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	FVector Direction = FVector::ZeroVector;

	/** Seed for randomised effects (recoil spread). 0 = derived from the handle (still deterministic per play). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	int32 RandomSeed = 0;
};

/** Read-only view of an active effect (Blueprint / debugging). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraEffectInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Effect")
	FACFCameraEffectHandle Handle;

	UPROPERTY(BlueprintReadOnly, Category = "Effect")
	FName PresetName;

	UPROPERTY(BlueprintReadOnly, Category = "Effect")
	float Time = 0.f;

	/** Largest current layer weight (envelope x scale x attenuation x curve). */
	UPROPERTY(BlueprintReadOnly, Category = "Effect")
	float Weight = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Effect")
	int32 ActiveLayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Effect")
	bool bStopping = false;
};

/** What one effect layer produced this frame (before weighting). */
struct FACFCameraEffectOutput
{
	EACFCameraEffectChannel Channels = EACFCameraEffectChannel::None;
	/** Additive: offset (in LocationSpace). Override: absolute world location. */
	FVector Location = FVector::ZeroVector;
	EACFCameraEffectSpace LocationSpace = EACFCameraEffectSpace::Camera;
	/** Additive: pitch / yaw / roll offsets in degrees. Override: absolute rotation. */
	FRotator Rotation = FRotator::ZeroRotator;
	/** Additive: degrees. Override: absolute FOV. Multiply: factor. */
	float FOV = 0.f;

	void Reset() { *this = FACFCameraEffectOutput(); }
};
