#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "Effects/ACFCameraEffect.h"
#include "ACFCameraBuiltinEffects.generated.h"

// -------------------------------------------------------------------------------------------------
// Punch

/** Instant camera kick shaped by the envelope: location / rotation / FOV offsets (also a dash FOV punch). */
UCLASS(meta = (DisplayName = "Camera Punch"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraEffect_Punch : public UACFCameraEffect
{
	GENERATED_BODY()

public:
	UACFCameraEffect_Punch();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Punch")
	FVector Location = FVector(-30.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Punch")
	EACFCameraEffectSpace Space = EACFCameraEffectSpace::Camera;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Punch")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Degrees (Additive), absolute FOV (Override) or factor (Multiply). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Punch")
	float FOV = 0.f;

	virtual void Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const override;
};

// -------------------------------------------------------------------------------------------------
// Recoil

USTRUCT()
struct FACFCameraRecoilState
{
	GENERATED_BODY()
	float Yaw = 0.f;
	float Roll = 0.f;
};

/** Attack recoil: kick back + pitch up, with a per-shot random yaw / roll spread. Shots stack. */
UCLASS(meta = (DisplayName = "Camera Recoil"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraEffect_Recoil : public UACFCameraEffect
{
	GENERATED_BODY()

public:
	UACFCameraEffect_Recoil();

	/** Camera moves backwards by this many cm (PreCollision). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (Units = cm))
	float KickBack = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (Units = cm))
	float KickUp = 2.f;

	/** Pitch up in degrees (PostCollision). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (Units = deg))
	float PitchKick = 2.5f;

	/** Random yaw in [-YawSpread, YawSpread] per shot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0.0", Units = deg))
	float YawSpread = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0.0", Units = deg))
	float RollSpread = 1.f;

	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraRecoilState::StaticStruct(); }
	virtual void OnStart(const FACFCameraEffectContext& Context, FInstancedStruct* State) const override;
	virtual void Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const override;
};

// -------------------------------------------------------------------------------------------------
// Impulse

UENUM(BlueprintType)
enum class EACFCameraImpulseDirection : uint8
{
	/** From the source towards the camera (explosions); falls back to Params.Direction / camera up. */
	AwayFromSource,
	/** Params.Direction (world). Falls back to Fixed. */
	ParamsDirection,
	/** FixedDirection in Space. */
	Fixed,
};

USTRUCT()
struct FACFCameraImpulseState
{
	GENERATED_BODY()
	/** World direction latched at start (the impulse does not rotate with the camera). */
	FVector Direction = FVector::UpVector;
};

/**
 * Shock (explosion, landing, hit): a displacement along a direction, optionally oscillating as a damped
 * wave (Frequency > 0), plus an optional rotation kick. Distance attenuation via DistanceAttenuation.
 * This is a single deterministic waveform, not a noise shake: use the Camera Shake layer for noise.
 */
UCLASS(meta = (DisplayName = "Camera Impulse"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraEffect_Impulse : public UACFCameraEffect
{
	GENERATED_BODY()

public:
	UACFCameraEffect_Impulse();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse")
	EACFCameraImpulseDirection DirectionMode = EACFCameraImpulseDirection::AwayFromSource;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse")
	FVector FixedDirection = FVector(0.f, 0.f, -1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse")
	EACFCameraEffectSpace Space = EACFCameraEffectSpace::World;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (Units = cm))
	float Amplitude = 25.f;

	/** Oscillations per second (0 = a single push shaped by the envelope). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (ClampMin = "0.0"))
	float Frequency = 12.f;

	/** Rotation kick (degrees), oscillating with the same wave. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse")
	FRotator RotationAmplitude = FRotator(1.5f, 0.f, 1.f);

	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraImpulseState::StaticStruct(); }
	virtual void OnStart(const FACFCameraEffectContext& Context, FInstancedStruct* State) const override;
	virtual void Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const override;
};

// -------------------------------------------------------------------------------------------------
// Camera Shake (reuses UE's UCameraShakeBase)

USTRUCT()
struct FACFCameraShakeLayerState
{
	GENERATED_BODY()

	/** ACF-owned shake instance. Never registered with the PlayerCameraManager, so UE never applies it too. */
	UPROPERTY()
	TObjectPtr<UCameraShakeBase> Shake = nullptr;
};

/**
 * Plays a standard UCameraShakeBase (any pattern: wave oscillator, Perlin noise, sequence, ...) inside the
 * ACF pipeline: the shake is evaluated on a copy of the camera, its location delta goes to PreCollision
 * (so it is collided) and its rotation / FOV delta to PostCollision. Its own blend in / out and duration
 * are used; the layer envelope defaults to "until the shake finishes". Post-process output of the shake
 * is forwarded to the PlayerCameraManager like UE does.
 */
UCLASS(meta = (DisplayName = "Camera Shake (UE)"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraEffect_Shake : public UACFCameraEffect
{
	GENERATED_BODY()

public:
	UACFCameraEffect_Shake();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake")
	TSubclassOf<UCameraShakeBase> ShakeClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake", meta = (ClampMin = "0.0"))
	float ShakeScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake")
	ECameraShakePlaySpace PlaySpace = ECameraShakePlaySpace::CameraLocal;

	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraShakeLayerState::StaticStruct(); }
	virtual void OnStart(const FACFCameraEffectContext& Context, FInstancedStruct* State) const override;
	virtual void OnStop(FInstancedStruct* State, bool bImmediate) const override;
	virtual void OnEnd(FInstancedStruct* State) const override;
	virtual bool HandlesOwnRelease() const override { return true; }
	virtual bool IsFinished(const FInstancedStruct* State) const override;
	virtual void Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const override;
};

// -------------------------------------------------------------------------------------------------
// Speed-based FOV

USTRUCT()
struct FACFCameraMotionState
{
	GENERATED_BODY()
	FVector PreviousLocation = FVector::ZeroVector;
	float PreviousYaw = 0.f;
	TWeakObjectPtr<const AActor> Target;
	float Value = 0.f;
	bool bInitialized = false;
};

/** Widens the FOV with the target's speed (dash / sprint feel). Usually played once with an infinite sustain. */
UCLASS(meta = (DisplayName = "Speed FOV"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraEffect_SpeedFOV : public UACFCameraEffect
{
	GENERATED_BODY()

public:
	UACFCameraEffect_SpeedFOV();

	/** No change below this speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed FOV", meta = (ClampMin = "0.0"))
	float MinSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed FOV", meta = (ClampMin = "0.0"))
	float MaxSpeed = 1200.f;

	/** Extra degrees at MaxSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed FOV", meta = (Units = deg))
	float FOVAtMaxSpeed = 15.f;

	/** The camera FOV including this effect stays inside [MinFOV, MaxFOV] (relative to the FOV it is applied to). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed FOV", meta = (Units = deg))
	float MinFOV = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed FOV", meta = (Units = deg))
	float MaxFOV = 110.f;

	/** Time constants for widening / narrowing (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed FOV", meta = (ClampMin = "0.0", Units = s))
	float RiseTime = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed FOV", meta = (ClampMin = "0.0", Units = s))
	float FallTime = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed FOV")
	bool bIgnoreVerticalSpeed = true;

	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraMotionState::StaticStruct(); }
	virtual void Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const override;
};

// -------------------------------------------------------------------------------------------------
// Roll

/** Banks the camera into turns and sideways motion of the target (roll offset, smoothed). */
UCLASS(meta = (DisplayName = "Camera Roll"))
class ACTIONCAMERAFRAMEWORK_API UACFCameraEffect_Roll : public UACFCameraEffect
{
	GENERATED_BODY()

public:
	UACFCameraEffect_Roll();

	/** Degrees of roll per (degree / second) of target yaw rate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll")
	float RollPerYawRate = 0.02f;

	/** Degrees of roll per (cm / second) of sideways speed relative to the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll")
	float RollPerLateralSpeed = 0.006f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll", meta = (ClampMin = "0.0", Units = deg))
	float MaxRoll = 6.f;

	/** Time constant towards the target roll (also the return to level). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll", meta = (ClampMin = "0.0", Units = s))
	float ResponseTime = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roll", meta = (ClampMin = "0.0", Units = s))
	float ReturnTime = 0.4f;

	virtual const UScriptStruct* GetRuntimeStateType() const override { return FACFCameraMotionState::StaticStruct(); }
	virtual void Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const override;
};
