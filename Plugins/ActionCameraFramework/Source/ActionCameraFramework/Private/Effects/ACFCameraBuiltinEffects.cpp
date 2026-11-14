#include "Effects/ACFCameraBuiltinEffects.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Math/RandomStream.h"
#include "Nodes/ACFCameraTargetTracking.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace ACFEffectsImpl
{
	template <typename T>
	T* GetState(FInstancedStruct* State)
	{
		return State ? State->GetMutablePtr<T>() : nullptr;
	}

	static void AddChannel(FACFCameraEffectOutput& Out, EACFCameraEffectChannel Channel, bool bCondition)
	{
		if (bCondition)
		{
			Out.Channels |= Channel;
		}
	}

	/** True when a FOV value changes anything for the given blend mode. */
	static bool HasFov(float Fov, EACFCameraEffectBlendMode Mode)
	{
		return Mode == EACFCameraEffectBlendMode::Multiply ? !FMath::IsNearlyEqual(Fov, 1.f) : (Mode == EACFCameraEffectBlendMode::Override || !FMath::IsNearlyZero(Fov));
	}
}

// -------------------------------------------------------------------------------------------------
// Preset validation

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "ACFCameraEffectPreset"
EDataValidationResult UACFCameraEffectPreset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Layers.IsEmpty())
	{
		Context.AddWarning(LOCTEXT("NoLayers", "Effect preset has no layers; playing it does nothing."));
	}
	for (const UACFCameraEffect* Layer : Layers)
	{
		if (!Layer)
		{
			Context.AddWarning(LOCTEXT("EmptyLayer", "Empty layer slot."));
			continue;
		}
		if (Layer->IsA<UACFCameraEffect_Shake>())
		{
			if (!CastChecked<UACFCameraEffect_Shake>(Layer)->ShakeClass)
			{
				Context.AddWarning(LOCTEXT("NoShakeClass", "Camera Shake layer without a Shake Class."));
			}
			if (Layer->BlendMode != EACFCameraEffectBlendMode::Additive)
			{
				Context.AddWarning(LOCTEXT("ShakeAdditive", "Camera Shake layers are always additive; Blend Mode is ignored."));
			}
		}
		else if (Layer->BlendMode == EACFCameraEffectBlendMode::Multiply && !Layer->IsA<UACFCameraEffect_Punch>())
		{
			Context.AddWarning(LOCTEXT("Multiply", "Multiply only affects the FOV channel."));
		}
	}
	return Result;
}
#undef LOCTEXT_NAMESPACE
#endif

// -------------------------------------------------------------------------------------------------
// Punch

UACFCameraEffect_Punch::UACFCameraEffect_Punch()
{
	Envelope.Attack = 0.03f;
	Envelope.Sustain = 0.f;
	Envelope.Release = 0.25f;
}

void UACFCameraEffect_Punch::Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const
{
	using namespace ACFEffectsImpl;
	Out.Location = Location;
	Out.LocationSpace = Space;
	Out.Rotation = Rotation;
	Out.FOV = FOV;
	// A channel is written only when it has a value (an FOV-only punch must not override location / rotation).
	AddChannel(Out, EACFCameraEffectChannel::Location, !Location.IsNearlyZero());
	AddChannel(Out, EACFCameraEffectChannel::Rotation, !Rotation.IsNearlyZero());
	AddChannel(Out, EACFCameraEffectChannel::FOV, HasFov(FOV, BlendMode));
	if (BlendMode == EACFCameraEffectBlendMode::Multiply)
	{
		Out.Channels &= EACFCameraEffectChannel::FOV;
	}
}

// -------------------------------------------------------------------------------------------------
// Recoil

UACFCameraEffect_Recoil::UACFCameraEffect_Recoil()
{
	Envelope.Attack = 0.03f;
	Envelope.Sustain = 0.f;
	Envelope.Release = 0.3f;
}

void UACFCameraEffect_Recoil::OnStart(const FACFCameraEffectContext& Context, FInstancedStruct* State) const
{
	if (FACFCameraRecoilState* Recoil = ACFEffectsImpl::GetState<FACFCameraRecoilState>(State))
	{
		FRandomStream Random(Context.Seed);
		Recoil->Yaw = Random.FRandRange(-YawSpread, YawSpread);
		Recoil->Roll = Random.FRandRange(-RollSpread, RollSpread);
	}
}

void UACFCameraEffect_Recoil::Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const
{
	const FACFCameraRecoilState* Recoil = ACFEffectsImpl::GetState<FACFCameraRecoilState>(State);
	Out.Location = FVector(-KickBack, 0.f, KickUp);
	Out.LocationSpace = EACFCameraEffectSpace::Camera;
	Out.Rotation = FRotator(PitchKick, Recoil ? Recoil->Yaw : 0.f, Recoil ? Recoil->Roll : 0.f);
	Out.Channels = EACFCameraEffectChannel::Location | EACFCameraEffectChannel::Rotation;
}

// -------------------------------------------------------------------------------------------------
// Impulse

UACFCameraEffect_Impulse::UACFCameraEffect_Impulse()
{
	Envelope.Attack = 0.f;
	Envelope.Sustain = 0.f;
	Envelope.Release = 0.6f;
	DistanceAttenuation.bEnabled = true;
}

void UACFCameraEffect_Impulse::OnStart(const FACFCameraEffectContext& Context, FInstancedStruct* State) const
{
	FACFCameraImpulseState* Impulse = ACFEffectsImpl::GetState<FACFCameraImpulseState>(State);
	if (!Impulse)
	{
		return;
	}
	const FVector Fixed = Space == EACFCameraEffectSpace::Camera ? Context.CameraPose.Rotation.RotateVector(FixedDirection) : FixedDirection;
	FVector Direction = FVector::ZeroVector;
	switch (DirectionMode)
	{
	case EACFCameraImpulseDirection::AwayFromSource:
		if (Context.bHasSource)
		{
			Direction = (Context.CameraPose.Location - Context.SourceLocation).GetSafeNormal();
		}
		if (Direction.IsNearlyZero())
		{
			Direction = !Context.Params.Direction.IsNearlyZero() ? Context.Params.Direction : FVector::UpVector;
		}
		break;
	case EACFCameraImpulseDirection::ParamsDirection:
		Direction = !Context.Params.Direction.IsNearlyZero() ? Context.Params.Direction : Fixed;
		break;
	default:
		Direction = Fixed;
		break;
	}
	Impulse->Direction = Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector); // latched in world space
}

void UACFCameraEffect_Impulse::Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const
{
	const FACFCameraImpulseState* Impulse = ACFEffectsImpl::GetState<FACFCameraImpulseState>(State);
	const float Wave = Frequency > 0.f ? FMath::Sin(2.f * UE_PI * Frequency * Context.Time) : 1.f;
	Out.Location = (Impulse ? Impulse->Direction : FVector::UpVector) * (Amplitude * Wave);
	Out.LocationSpace = EACFCameraEffectSpace::World;
	Out.Rotation = RotationAmplitude * Wave;
	Out.Channels = EACFCameraEffectChannel::Location;
	ACFEffectsImpl::AddChannel(Out, EACFCameraEffectChannel::Rotation, !RotationAmplitude.IsNearlyZero());
}

// -------------------------------------------------------------------------------------------------
// Camera Shake (UCameraShakeBase evaluated by ACF; never handed to the PlayerCameraManager)

UACFCameraEffect_Shake::UACFCameraEffect_Shake()
{
	Envelope.Attack = 0.f;
	Envelope.Sustain = -1.f; // lifetime = the shake's own duration / blend out
	Envelope.Release = 0.f;
}

void UACFCameraEffect_Shake::OnStart(const FACFCameraEffectContext& Context, FInstancedStruct* State) const
{
	FACFCameraShakeLayerState* Shake = ACFEffectsImpl::GetState<FACFCameraShakeLayerState>(State);
	if (!Shake || !ShakeClass)
	{
		return;
	}
	Shake->Shake = NewObject<UCameraShakeBase>(GetTransientPackage(), ShakeClass);
	FCameraShakeBaseStartParams Params;
	// The camera manager is only used to forward the shake's post-process blend (UE 5.8 CameraShakeBase.cpp);
	// it may be null (unit tests). The shake is NOT registered with the PCM's shake modifier.
	Params.CameraManager = Context.Evaluation.PlayerController ? Context.Evaluation.PlayerController->PlayerCameraManager.Get() : nullptr;
	Params.Scale = ShakeScale;
	Params.PlaySpace = PlaySpace;
	Shake->Shake->StartShake(Params);
}

void UACFCameraEffect_Shake::OnStop(FInstancedStruct* State, bool bImmediate) const
{
	FACFCameraShakeLayerState* Shake = ACFEffectsImpl::GetState<FACFCameraShakeLayerState>(State);
	if (Shake && Shake->Shake && Shake->Shake->IsActive())
	{
		Shake->Shake->StopShake(bImmediate); // non-immediate: the shake's own blend out
	}
}

void UACFCameraEffect_Shake::OnEnd(FInstancedStruct* State) const
{
	FACFCameraShakeLayerState* Shake = ACFEffectsImpl::GetState<FACFCameraShakeLayerState>(State);
	if (Shake && Shake->Shake)
	{
		if (Shake->Shake->IsActive())
		{
			Shake->Shake->StopShake(true);
		}
		Shake->Shake->TeardownShake();
		Shake->Shake = nullptr;
	}
}

bool UACFCameraEffect_Shake::IsFinished(const FInstancedStruct* State) const
{
	const FACFCameraShakeLayerState* Shake = State ? State->GetPtr<FACFCameraShakeLayerState>() : nullptr;
	return !Shake || !Shake->Shake || !Shake->Shake->IsActive() || Shake->Shake->IsFinished();
}

void UACFCameraEffect_Shake::Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const
{
	FACFCameraShakeLayerState* Shake = ACFEffectsImpl::GetState<FACFCameraShakeLayerState>(State);
	if (!Shake || !Shake->Shake || !Shake->Shake->IsActive())
	{
		return;
	}
	// Evaluate on a copy of the camera and keep only the delta, so the location part can be applied before
	// Collision and the rotation / FOV part after it. Alpha 1: the layer weight is applied by the stack.
	FMinimalViewInfo View;
	View.Location = Context.CameraPose.Location;
	View.Rotation = Context.CameraPose.Rotation;
	View.FOV = Context.CameraPose.FieldOfView;
	const FMinimalViewInfo Before = View;
	Shake->Shake->UpdateAndApplyCameraShake(Context.GetDeltaTime(), 1.f, View);

	Out.Location = View.Location - Before.Location;
	Out.LocationSpace = EACFCameraEffectSpace::World;
	Out.Rotation = (View.Rotation - Before.Rotation).GetNormalized();
	Out.FOV = View.FOV - Before.FOV;
	Out.Channels = EACFCameraEffectChannel::Location | EACFCameraEffectChannel::Rotation | EACFCameraEffectChannel::FOV;
}

// -------------------------------------------------------------------------------------------------
// Motion helpers (Speed FOV / Roll)

namespace ACFEffectsImpl
{
	/** Updates position / yaw history; returns false on the first sample (no velocity yet). */
	static bool SampleMotion(FACFCameraMotionState& Motion, const AActor* Target, float Dt, FVector& OutVelocity, float& OutYawRate)
	{
		OutVelocity = FVector::ZeroVector;
		OutYawRate = 0.f;
		if (!Target)
		{
			return false;
		}
		const FVector Location = Target->GetActorLocation();
		const float Yaw = Target->GetActorRotation().Yaw;
		if (!Motion.bInitialized || Motion.Target.Get() != Target)
		{
			Motion.Target = Target;
			Motion.PreviousLocation = Location;
			Motion.PreviousYaw = Yaw;
			Motion.bInitialized = true;
			return false;
		}
		if (Dt <= 0.f)
		{
			return false;
		}
		OutVelocity = (Location - Motion.PreviousLocation) / Dt;
		OutYawRate = FRotator::NormalizeAxis(Yaw - Motion.PreviousYaw) / Dt;
		Motion.PreviousLocation = Location;
		Motion.PreviousYaw = Yaw;
		// A teleport is not motion.
		if (OutVelocity.SizeSquared() > FMath::Square(20000.f))
		{
			OutVelocity = FVector::ZeroVector;
			OutYawRate = 0.f;
		}
		return true;
	}

	static void Approach(float& Value, float Target, float RiseTime, float FallTime, float Dt)
	{
		const float Time = FMath::Abs(Target) > FMath::Abs(Value) ? RiseTime : FallTime;
		Value += (Target - Value) * ACFCameraMath::ExpAlpha(Time, Dt);
	}
}

// -------------------------------------------------------------------------------------------------
// Speed FOV

UACFCameraEffect_SpeedFOV::UACFCameraEffect_SpeedFOV()
{
	Envelope.Attack = 0.2f;
	Envelope.Sustain = -1.f;
	Envelope.Release = 0.3f;
}

void UACFCameraEffect_SpeedFOV::Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const
{
	FACFCameraMotionState* Motion = ACFEffectsImpl::GetState<FACFCameraMotionState>(State);
	if (!Motion)
	{
		return;
	}
	FVector Velocity;
	float YawRate;
	const float Dt = Context.GetDeltaTime();
	if (ACFEffectsImpl::SampleMotion(*Motion, Context.MotionTarget, Dt, Velocity, YawRate) || (!Context.MotionTarget && Dt > 0.f))
	{
		if (bIgnoreVerticalSpeed)
		{
			Velocity.Z = 0.0;
		}
		const float Speed = Velocity.Size();
		const float Alpha = MaxSpeed > MinSpeed ? FMath::Clamp((Speed - MinSpeed) / (MaxSpeed - MinSpeed), 0.f, 1.f) : (Speed >= MinSpeed ? 1.f : 0.f);
		ACFEffectsImpl::Approach(Motion->Value, FOVAtMaxSpeed * Alpha, RiseTime, FallTime, Dt);
	}
	const float BaseFov = Context.CameraPose.FieldOfView;
	Out.FOV = FMath::Clamp(Motion->Value, MinFOV - BaseFov, MaxFOV - BaseFov);
	if (BlendMode == EACFCameraEffectBlendMode::Multiply)
	{
		Out.FOV = BaseFov > 0.f ? (BaseFov + Out.FOV) / BaseFov : 1.f;
	}
	else if (BlendMode == EACFCameraEffectBlendMode::Override)
	{
		Out.FOV = BaseFov + Out.FOV;
	}
	Out.Channels = EACFCameraEffectChannel::FOV;
}

// -------------------------------------------------------------------------------------------------
// Roll

UACFCameraEffect_Roll::UACFCameraEffect_Roll()
{
	Envelope.Attack = 0.2f;
	Envelope.Sustain = -1.f;
	Envelope.Release = 0.3f;
}

void UACFCameraEffect_Roll::Evaluate(const FACFCameraEffectContext& Context, FInstancedStruct* State, FACFCameraEffectOutput& Out) const
{
	FACFCameraMotionState* Motion = ACFEffectsImpl::GetState<FACFCameraMotionState>(State);
	if (!Motion)
	{
		return;
	}
	FVector Velocity;
	float YawRate;
	const float Dt = Context.GetDeltaTime();
	if (ACFEffectsImpl::SampleMotion(*Motion, Context.MotionTarget, Dt, Velocity, YawRate) || (!Context.MotionTarget && Dt > 0.f))
	{
		const FVector CameraRight = FRotator(0.f, Context.CameraPose.Rotation.Yaw, 0.f).RotateVector(FVector::RightVector);
		const float Lateral = Velocity | CameraRight;
		const float Target = FMath::Clamp(YawRate * RollPerYawRate + Lateral * RollPerLateralSpeed, -MaxRoll, MaxRoll);
		ACFEffectsImpl::Approach(Motion->Value, Target, ResponseTime, ReturnTime, Dt);
	}
	Out.Rotation = FRotator(0.f, 0.f, FMath::Clamp(Motion->Value, -MaxRoll, MaxRoll));
	Out.Channels = EACFCameraEffectChannel::Rotation;
}
