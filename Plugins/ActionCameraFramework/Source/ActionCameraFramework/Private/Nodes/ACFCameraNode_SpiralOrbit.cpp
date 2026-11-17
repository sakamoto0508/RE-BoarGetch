#include "Nodes/ACFCameraNode_SpiralOrbit.h"
#include "Nodes/ACFCameraNodeUtils.h"
#include "Nodes/ACFCameraTargetTracking.h"
#include "Components/ActorComponent.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"

namespace ACFSpiralOrbit
{
	static const AActor* ActorOf(const UObject* Object)
	{
		if (const AActor* Actor = Cast<AActor>(Object))
		{
			return Actor;
		}
		const UActorComponent* Component = Cast<UActorComponent>(Object);
		return Component ? Component->GetOwner() : nullptr;
	}

	static float EaseInOut(float X, float Exponent)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X < 0.5f ? 0.5f * FMath::Pow(2.f * X, Exponent) : 1.f - 0.5f * FMath::Pow(2.f * (1.f - X), Exponent);
	}

	static float Curve(const UCurveFloat* Curve, float X, float Fallback)
	{
		return Curve ? Curve->GetFloatValue(X) : Fallback;
	}
}

void UACFCameraNode_SpiralOrbit::OnActivated(const FACFCameraNodeContext& Context, FInstancedStruct* RuntimeState) const
{
	// Every activation is a new spiral (a sequence shot starts a fresh request).
	ResetInterpolation(RuntimeState);
}

void UACFCameraNode_SpiralOrbit::ResetInterpolation(FInstancedStruct* RuntimeState) const
{
	if (FACFCameraSpiralOrbitState* State = ACFNodes::GetState<FACFCameraSpiralOrbitState>(RuntimeState))
	{
		State->bStarted = false;
		State->Elapsed = 0.f;
	}
}

FACFCameraSpiralSample UACFCameraNode_SpiralOrbit::SampleSpiral(const FVector& Focus, float StartYaw, float Time) const
{
	FACFCameraSpiralSample S;
	S.Progress = FMath::Clamp(Time / FMath::Max(0.05f, Duration), 0.f, 1.f);
	const float AngleAlpha = ACFSpiralOrbit::Curve(AngleCurve, S.Progress, ACFSpiralOrbit::EaseInOut(S.Progress, AngleEaseExponent));
	const float HeightAlpha = ACFSpiralOrbit::Curve(HeightCurve, S.Progress, FMath::SmoothStep(0.f, 1.f, S.Progress));
	const float RadiusAlpha = ACFSpiralOrbit::Curve(RadiusCurve, S.Progress, S.Progress);
	S.AngleTravelled = RotationDegrees * AngleAlpha;
	S.Yaw = FRotator::NormalizeAxis(StartYaw + (Direction < 0 ? -1.f : 1.f) * S.AngleTravelled);
	S.Radius = FMath::Max(10.f, FMath::Lerp(RadiusStart, RadiusEnd, RadiusAlpha));
	S.Height = FMath::Lerp(HeightStart, HeightEnd, HeightAlpha);
	const float Rad = FMath::DegreesToRadians(S.Yaw);
	S.Location = Focus + FVector(FMath::Cos(Rad) * S.Radius, FMath::Sin(Rad) * S.Radius, S.Height);
	S.LookAt = Focus + LookAtOffset;
	return S;
}

void UACFCameraNode_SpiralOrbit::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraSpiralOrbitState* State = ACFNodes::GetState<FACFCameraSpiralOrbitState>(RuntimeState);
	if (!State)
	{
		return;
	}

	FTransform Look, Follow;
	const UObject* LookObject = nullptr;
	const UObject* FollowObject = nullptr;
	const bool bLook = Context.Request.LookAtTarget.Resolve(Look, LookObject);
	const bool bFollow = Context.Request.FollowTarget.Resolve(Follow, FollowObject);
	const float Dt = Context.GetDeltaTime();
	if (bLook || bFollow)
	{
		const FVector Focus = (bLook ? Look : Follow).GetLocation() + FocusOffset;
		State->Focus = State->bStarted && FocusDampingTime > 0.f && Dt > 0.f
			? FMath::Lerp(State->Focus, Focus, static_cast<double>(ACFCameraMath::ExpAlpha(FocusDampingTime, Dt)))
			: Focus;
		State->FocusActor = ACFSpiralOrbit::ActorOf(bLook ? LookObject : FollowObject);
	}
	else if (!State->bStarted)
	{
		return; // Nothing to orbit yet.
	}
	// else: focus lost -> keep the last focus point and keep orbiting it.

	if (!State->bStarted)
	{
		float Yaw = StartAngleOffset;
		if (StartMode == EACFCameraSpiralStart::ControlRotation && Context.Evaluation.bHasControlRotation)
		{
			// The gameplay camera sits behind the control rotation: start the orbit from that side so the cut is soft.
			Yaw += Context.Evaluation.ControlRotation.Yaw + 180.f;
		}
		else if (StartMode == EACFCameraSpiralStart::FollowTarget && bFollow)
		{
			const FVector ToFollow = Follow.GetLocation() - State->Focus;
			Yaw += FVector(ToFollow.X, ToFollow.Y, 0.0).IsNearlyZero() ? 0.f : static_cast<float>(ToFollow.Rotation().Yaw);
		}
		State->StartYaw = Yaw;
		State->Elapsed = 0.f;
		State->bStarted = true;
	}
	else if (Dt > 0.f)
	{
		State->Elapsed += Dt;
	}

	State->Last = SampleSpiral(State->Focus, State->StartYaw, State->Elapsed);
	InOutPose.Location = State->Last.Location;
	const FVector ToLook = State->Last.LookAt - InOutPose.Location;
	if (!ToLook.IsNearlyZero())
	{
		InOutPose.Rotation = ToLook.Rotation();
		InOutPose.Rotation.Roll = 0.0;
	}
	if (bOverrideFieldOfView)
	{
		InOutPose.FieldOfView = FieldOfView;
	}

	Context.Scratch.bHasPivot = true;
	Context.Scratch.Pivot = State->Last.LookAt;
	Context.Scratch.PivotActor = State->FocusActor.Get();
#if !UE_BUILD_SHIPPING
	Context.Scratch.Debug.bHasFollowTarget = true;
	Context.Scratch.Debug.FollowTarget = State->Focus;
	Context.Scratch.Debug.bHasOrbit = true;
	Context.Scratch.Debug.OrbitYaw = State->Last.Yaw;
	Context.Scratch.Debug.OrbitPitch = static_cast<float>(InOutPose.Rotation.Pitch);
	Context.Scratch.Debug.OrbitDistance = State->Last.Radius;
#endif
}
