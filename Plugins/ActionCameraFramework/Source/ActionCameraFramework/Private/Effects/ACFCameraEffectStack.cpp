#include "Effects/ACFCameraEffectStack.h"
#include "Effects/ACFCameraEffect.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

// -------------------------------------------------------------------------------------------------
// Envelope / attenuation

float FACFCameraEffectEnvelope::ShapeRamp(float T) const
{
	T = FMath::Clamp(T, 0.f, 1.f);
	return Shape == EACFCameraEnvelopeShape::Smooth ? T * T * (3.f - 2.f * T) : T;
}

float FACFCameraEffectEnvelope::Evaluate(float Time) const
{
	if (Attack > 0.f && Time < Attack)
	{
		return ShapeRamp(Time / Attack);
	}
	if (IsInfinite() || Time < Attack + Sustain)
	{
		return 1.f;
	}
	if (Release <= 0.f)
	{
		return 0.f;
	}
	return 1.f - ShapeRamp((Time - Attack - Sustain) / Release);
}

float FACFCameraEffectEnvelope::EvaluateRelease(float Time, float ReleaseStart, float ReleaseFrom) const
{
	if (Release <= 0.f)
	{
		return 0.f;
	}
	return ReleaseFrom * (1.f - ShapeRamp((Time - ReleaseStart) / Release));
}

float FACFCameraEffectAttenuation::Evaluate(float Distance) const
{
	if (!bEnabled || Distance <= InnerRadius)
	{
		return 1.f;
	}
	if (Distance >= OuterRadius || OuterRadius <= InnerRadius)
	{
		return 0.f;
	}
	const float Normalized = (Distance - InnerRadius) / (OuterRadius - InnerRadius);
	return Falloff ? FMath::Clamp(Falloff->GetFloatValue(Normalized), 0.f, 1.f) : 1.f - Normalized;
}

// -------------------------------------------------------------------------------------------------
// Stack

namespace ACFEffects
{
	static int32 GNextEffectId = 0;
}

FACFCameraEffectHandle FACFCameraEffectStack::Play(const UACFCameraEffectPreset* Preset, const FACFCameraEffectParams& Params)
{
	if (!Preset || Preset->Layers.IsEmpty())
	{
		UE_LOG(LogActionCamera, Warning, TEXT("PlayCameraEffect rejected: preset %s is null or has no layers."), *GetNameSafe(Preset));
		return FACFCameraEffectHandle();
	}
	if (ACFEffects::GNextEffectId == MAX_int32)
	{
		ACFEffects::GNextEffectId = 0;
	}

	FACFCameraEffectInstance& Instance = Instances.AddDefaulted_GetRef();
	Instance.Handle = FACFCameraEffectHandle(++ACFEffects::GNextEffectId);
	Instance.Preset = Preset;
	Instance.Params = Params;
	Instance.Sequence = NextSequence++;
	Instance.bTracksInstigator = !Params.Instigator.IsExplicitlyNull();
	Instance.Layers.Reserve(Preset->Layers.Num());
	for (const UACFCameraEffect* Effect : Preset->Layers)
	{
		if (Effect && Effect->bEnabled)
		{
			FACFCameraEffectLayerInstance& Layer = Instance.Layers.AddDefaulted_GetRef();
			Layer.Effect = Effect;
			if (const UScriptStruct* StateType = Effect->GetRuntimeStateType())
			{
				Layer.State.InitializeAs(StateType);
			}
		}
	}
	return Instance.Handle;
}

void FACFCameraEffectStack::EndLayer(FACFCameraEffectLayerInstance& Layer)
{
	if (Layer.Effect && Layer.bStarted)
	{
		Layer.Effect->OnEnd(Layer.State.IsValid() ? &Layer.State : nullptr);
	}
	Layer.State.Reset(); // free per-play state right away (shake instance becomes unreferenced)
	Layer.bFinished = true;
}

bool FACFCameraEffectStack::Stop(FACFCameraEffectHandle Handle, bool bImmediate)
{
	const int32 Index = Instances.IndexOfByPredicate([Handle](const FACFCameraEffectInstance& I) { return I.Handle == Handle; });
	if (!Handle.IsValid() || Index == INDEX_NONE)
	{
		return false;
	}
	FACFCameraEffectInstance& Instance = Instances[Index];
	if (bImmediate)
	{
		for (FACFCameraEffectLayerInstance& Layer : Instance.Layers)
		{
			EndLayer(Layer);
		}
		Instances.RemoveAt(Index);
		return true;
	}
	Instance.bStopping = true;
	for (FACFCameraEffectLayerInstance& Layer : Instance.Layers)
	{
		if (Layer.bFinished || Layer.bReleasing || !Layer.Effect)
		{
			continue;
		}
		if (!Layer.bStarted)
		{
			EndLayer(Layer); // played and stopped before its first frame
			continue;
		}
		Layer.bReleasing = true;
		Layer.ReleaseStart = Layer.Time;
		Layer.ReleaseFrom = Layer.Effect->Envelope.Evaluate(Layer.Time);
		Layer.Effect->OnStop(Layer.State.IsValid() ? &Layer.State : nullptr, false);
	}
	return true;
}

void FACFCameraEffectStack::StopAll(bool bImmediate)
{
	for (int32 Index = Instances.Num() - 1; Index >= 0; --Index)
	{
		Stop(Instances[Index].Handle, bImmediate);
	}
}

bool FACFCameraEffectStack::IsActive(FACFCameraEffectHandle Handle) const
{
	return Handle.IsValid() && Instances.ContainsByPredicate([Handle](const FACFCameraEffectInstance& I) { return I.Handle == Handle; });
}

void FACFCameraEffectStack::GetInfos(TArray<FACFCameraEffectInfo>& Out) const
{
	Out.Reset(Instances.Num());
	for (const FACFCameraEffectInstance& Instance : Instances)
	{
		FACFCameraEffectInfo& Info = Out.AddDefaulted_GetRef();
		Info.Handle = Instance.Handle;
		Info.PresetName = Instance.Preset ? Instance.Preset->GetFName() : NAME_None;
		Info.bStopping = Instance.bStopping;
		for (const FACFCameraEffectLayerInstance& Layer : Instance.Layers)
		{
			if (!Layer.bFinished)
			{
				++Info.ActiveLayers;
				Info.Time = FMath::Max(Info.Time, Layer.Time);
				Info.Weight = FMath::Max(Info.Weight, Layer.FinalWeight);
			}
		}
	}
}

void FACFCameraEffectStack::Evaluate(const FACFCameraEvaluationContext& Context, const FACFCameraPose& Camera)
{
	const float Dt = FMath::Max(Context.DeltaTime, 0.f);
	const AActor* Pawn = Context.PlayerController ? Context.PlayerController->GetPawn() : nullptr;

	for (int32 InstanceIndex = Instances.Num() - 1; InstanceIndex >= 0; --InstanceIndex)
	{
		FACFCameraEffectInstance& Instance = Instances[InstanceIndex];
		if (Instance.bTracksInstigator && !Instance.Params.Instigator.IsValid())
		{
			for (FACFCameraEffectLayerInstance& Layer : Instance.Layers)
			{
				EndLayer(Layer);
			}
			Instances.RemoveAt(InstanceIndex);
			continue;
		}

		if (const AActor* Source = Instance.Params.SourceActor.Get())
		{
			Instance.LastSourceLocation = Source->GetActorLocation();
			Instance.bHasLastSource = true;
		}
		else if (Instance.Params.bUseSourceLocation && !Instance.bHasLastSource)
		{
			Instance.LastSourceLocation = Instance.Params.SourceLocation;
			Instance.bHasLastSource = true;
		}

		FACFCameraEffectContext EffectContext(Context, Instance.Params, Camera);
		EffectContext.bHasSource = Instance.bHasLastSource;
		EffectContext.SourceLocation = Instance.LastSourceLocation;
		EffectContext.MotionTarget = Instance.Params.TargetActor.IsValid() ? Instance.Params.TargetActor.Get() : (Instance.Params.TargetActor.IsExplicitlyNull() ? Pawn : nullptr);
		const int32 BaseSeed = Instance.Params.RandomSeed != 0 ? Instance.Params.RandomSeed : Instance.Handle.GetId() * 7919 + 17;

		for (int32 LayerIndex = 0; LayerIndex < Instance.Layers.Num(); ++LayerIndex)
		{
			FACFCameraEffectLayerInstance& Layer = Instance.Layers[LayerIndex];
			const UACFCameraEffect* Effect = Layer.Effect;
			if (Layer.bFinished)
			{
				continue;
			}
			if (!Effect)
			{
				EndLayer(Layer);
				continue;
			}
			FInstancedStruct* State = Layer.State.IsValid() ? &Layer.State : nullptr;
			EffectContext.Seed = BaseSeed + LayerIndex;
			if (!Layer.bStarted)
			{
				EffectContext.Time = 0.f;
				Effect->OnStart(EffectContext, State);
				Layer.bStarted = true;
			}
			Layer.Time += Dt;
			EffectContext.Time = Layer.Time;

			const FACFCameraEffectEnvelope& Envelope = Effect->Envelope;
			float Level;
			bool bDone = false;
			if (Layer.bReleasing && !Effect->HandlesOwnRelease())
			{
				Level = Envelope.EvaluateRelease(Layer.Time, Layer.ReleaseStart, Layer.ReleaseFrom);
				bDone = Layer.Time >= Layer.ReleaseStart + Envelope.Release;
			}
			else
			{
				Level = Envelope.Evaluate(Layer.Time);
				bDone = !Envelope.IsInfinite() && !Layer.bReleasing && Layer.Time >= Envelope.GetDuration();
			}
			bDone |= Effect->IsFinished(State);
			if (bDone)
			{
				EndLayer(Layer);
				continue;
			}

			const float Curve = Effect->IntensityCurve ? Effect->IntensityCurve->GetFloatValue(Layer.Time) : 1.f;
			const float Attenuation = (Effect->DistanceAttenuation.bEnabled && EffectContext.bHasSource)
				? Effect->DistanceAttenuation.Evaluate(FVector::Dist(EffectContext.SourceLocation, Camera.Location)) : 1.f;
			Layer.FinalWeight = FMath::Max(Effect->Weight * Instance.Params.Scale * Level * Curve * Attenuation, 0.f);
			Layer.Output.Reset();
			Effect->Evaluate(EffectContext, State, Layer.Output);
		}

		Instance.Layers.RemoveAll([](const FACFCameraEffectLayerInstance& Layer) { return Layer.bFinished; });
		if (Instance.Layers.IsEmpty())
		{
			Instances.RemoveAt(InstanceIndex);
		}
	}
}

namespace ACFEffects
{
	struct FActiveLayer
	{
		const FACFCameraEffectLayerInstance* Layer;
		int32 Priority;
		uint64 Sequence;
	};

	/** Layers writing Channel with a positive weight, overrides sorted by (priority, play order). */
	template <typename TArrayType>
	static void Gather(const TArray<FACFCameraEffectInstance>& Instances, EACFCameraEffectChannel Channel, TArrayType& Out)
	{
		for (const FACFCameraEffectInstance& Instance : Instances)
		{
			for (const FACFCameraEffectLayerInstance& Layer : Instance.Layers)
			{
				if (!Layer.bFinished && Layer.Effect && Layer.FinalWeight > 0.f && EnumHasAnyFlags(Layer.Output.Channels, Channel))
				{
					Out.Add({ &Layer, Layer.Effect->Priority, Instance.Sequence });
				}
			}
		}
		Out.StableSort([](const FActiveLayer& A, const FActiveLayer& B)
		{
			return A.Priority != B.Priority ? A.Priority < B.Priority : A.Sequence < B.Sequence;
		});
	}
}

void FACFCameraEffectStack::ApplyPreCollision(FACFCameraPose& InOutPose, const FACFCameraPose& Camera) const
{
	TArray<ACFEffects::FActiveLayer, TInlineAllocator<16>> Layers;
	ACFEffects::Gather(Instances, EACFCameraEffectChannel::Location, Layers);
	FVector Additive = FVector::ZeroVector;
	for (const ACFEffects::FActiveLayer& Entry : Layers)
	{
		const FACFCameraEffectLayerInstance& Layer = *Entry.Layer;
		const float W = Layer.FinalWeight;
		switch (Layer.Effect->BlendMode)
		{
		case EACFCameraEffectBlendMode::Override:
			InOutPose.Location = FMath::Lerp(InOutPose.Location, Layer.Output.Location, static_cast<double>(FMath::Min(W, 1.f)));
			break;
		case EACFCameraEffectBlendMode::Additive:
			Additive += (Layer.Output.LocationSpace == EACFCameraEffectSpace::Camera ? Camera.Rotation.RotateVector(Layer.Output.Location) : Layer.Output.Location) * W;
			break;
		default:
			break; // Multiply has no meaning for Location (the preset validator warns)
		}
	}
	InOutPose.Location += Additive;
}

void FACFCameraEffectStack::ApplyPostCollision(FACFCameraPose& InOutPose, float FovMin, float FovMax) const
{
	TArray<ACFEffects::FActiveLayer, TInlineAllocator<16>> Layers;
	ACFEffects::Gather(Instances, EACFCameraEffectChannel::Rotation, Layers);
	FRotator Additive = FRotator::ZeroRotator;
	for (const ACFEffects::FActiveLayer& Entry : Layers)
	{
		const FACFCameraEffectLayerInstance& Layer = *Entry.Layer;
		const float W = Layer.FinalWeight;
		if (Layer.Effect->BlendMode == EACFCameraEffectBlendMode::Override)
		{
			InOutPose.Rotation = FQuat::Slerp(InOutPose.Rotation.Quaternion(), Layer.Output.Rotation.Quaternion(), FMath::Min(W, 1.f)).Rotator();
		}
		else if (Layer.Effect->BlendMode == EACFCameraEffectBlendMode::Additive)
		{
			Additive += Layer.Output.Rotation * W;
		}
	}
	InOutPose.Rotation = (InOutPose.Rotation + Additive).GetNormalized();

	Layers.Reset();
	ACFEffects::Gather(Instances, EACFCameraEffectChannel::FOV, Layers);
	if (Layers.IsEmpty())
	{
		return;
	}
	const float BaseFov = InOutPose.FieldOfView;
	float Fov = BaseFov;
	// Override (ascending priority) -> Multiply -> Additive.
	for (const ACFEffects::FActiveLayer& Entry : Layers)
	{
		if (Entry.Layer->Effect->BlendMode == EACFCameraEffectBlendMode::Override)
		{
			Fov = FMath::Lerp(Fov, Entry.Layer->Output.FOV, FMath::Min(Entry.Layer->FinalWeight, 1.f));
		}
	}
	for (const ACFEffects::FActiveLayer& Entry : Layers)
	{
		if (Entry.Layer->Effect->BlendMode == EACFCameraEffectBlendMode::Multiply)
		{
			Fov *= FMath::Lerp(1.f, Entry.Layer->Output.FOV, Entry.Layer->FinalWeight);
		}
	}
	for (const ACFEffects::FActiveLayer& Entry : Layers)
	{
		if (Entry.Layer->Effect->BlendMode == EACFCameraEffectBlendMode::Additive)
		{
			Fov += Entry.Layer->Output.FOV * Entry.Layer->FinalWeight;
		}
	}
	// Effects may not push the FOV outside the limits, but never change a base FOV that already is.
	InOutPose.FieldOfView = FMath::Clamp(Fov, FMath::Min(FovMin, BaseFov), FMath::Max(FovMax, BaseFov));
}
