#include "Presentation/CaptureCameraPresentationData.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSequence.h"
#include "Effects/ACFCameraEffect.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_SpiralOrbit.h"

UCaptureCameraPresentationData::UCaptureCameraPresentationData()
{
	Orbit = CreateDefaultSubobject<UACFCameraNode_SpiralOrbit>(TEXT("Orbit"));
	Collision = CreateDefaultSubobject<UACFCameraNode_Collision>(TEXT("Collision"));
	BlendIn.Curve = EACFCameraBlendCurve::EaseOut;
	BlendIn.Duration = 0.15f;
	ReturnBlend.Curve = EACFCameraBlendCurve::EaseInOut;
	ReturnBlend.Duration = 0.45f;
}

UACFCameraSequence* UCaptureCameraPresentationData::BuildSequence(UObject* Outer) const
{
	if (!Orbit)
		return nullptr;
	// Nodeと揺れのLayerはこのDataAssetのものを共有する（ACFのNode・Layerは実行時に自分を書き換えない）。
	UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(Outer, NAME_None, RF_Transient);
	Preset->Nodes.Add(Orbit);

	if (Collision)
		Preset->Nodes.Add(Collision);

	Preset->DefaultBlendIn = BlendIn;
	Preset->DefaultBlendOut = ReturnBlend;

	FACFCameraShot Shot;
	Shot.Name = TEXT("CaptureOrbit");
	Shot.Preset = Preset;
	Shot.AdvanceMode = EACFCameraShotAdvanceMode::Duration;
	Shot.Duration = Orbit->Duration + FMath::Max(0.f, HoldAfterOrbit);
	Shot.BlendIn = BlendIn;

	// Follow = Player（Start Mode = Follow Target用）、Look At = 注視点のProxy（Orbitの中心）。
	Shot.Targets.Follow = EACFCameraShotRole::Attacker;
	Shot.Targets.LookAt = EACFCameraShotRole::Target;
	Shot.bEnableDeoccluder = false;

	// 塞がり判定はCapturePresentationComponentが軌道全体で行う（ACFの開始時1点のProbeでは省略しない）。
	Shot.Fallback.bSkipIfInfeasible = false;

	if (!ShakeLayers.IsEmpty() && ShakeScale > 0.f)
	{
		UACFCameraEffectPreset* Shake = NewObject<UACFCameraEffectPreset>(Outer, NAME_None, RF_Transient);

		for (UACFCameraEffect* Layer : ShakeLayers)
			if (Layer)
				Shake->Layers.Add(Layer);

		FACFCameraShotEffect Effect;
		Effect.Preset = Shake;
		Effect.Scale = ShakeScale;
		Effect.Lifetime = EACFCameraShotEffectLifetime::FireAndForget;
		Shot.Effects.Add(Effect);
	}

	UACFCameraSequence* Sequence = NewObject<UACFCameraSequence>(Outer, NAME_None, RF_Transient);
	Sequence->Shots.Add(Shot);
	Sequence->Priority = Priority;
	Sequence->ReturnBlend = ReturnBlend;
	Sequence->CancelBlend = FACFCameraBlendSettings::MakeCut();
	Sequence->InterruptPolicy = EACFCameraSequenceInterruptPolicy::Cancel;
	Sequence->ActorLostPolicy = EACFCameraSequenceActorLostPolicy::Continue;
	Sequence->bUseAttackerTimeDilation = false;
	Sequence->bCollisionIgnoresRoleActors = true;
	return Sequence;
}
