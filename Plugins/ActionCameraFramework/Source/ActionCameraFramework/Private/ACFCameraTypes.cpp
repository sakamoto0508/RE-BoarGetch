#include "ACFCameraTypes.h"
#include "Curves/CurveFloat.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

bool FACFCameraTarget::Resolve(FTransform& OutTransform, const UObject*& OutObject) const
{
	if (const USceneComponent* SceneComponent = Component.Get())
	{
		OutTransform = SocketName.IsNone() ? SceneComponent->GetComponentTransform() : SceneComponent->GetSocketTransform(SocketName);
		OutObject = SceneComponent;
		return true;
	}
	if (const AActor* TargetActor = Actor.Get())
	{
		const USceneComponent* Root = TargetActor->GetRootComponent();
		if (Root && !SocketName.IsNone())
		{
			OutTransform = Root->GetSocketTransform(SocketName);
		}
		else
		{
			OutTransform = TargetActor->GetActorTransform();
		}
		OutObject = TargetActor;
		return true;
	}
	OutObject = nullptr;
	return false;
}

DEFINE_LOG_CATEGORY(LogActionCamera);

FACFCameraPose FACFCameraPose::Blend(const FACFCameraPose& A, const FACFCameraPose& B, float Alpha)
{
	if (Alpha <= 0.f)
	{
		return A;
	}
	if (Alpha >= 1.f)
	{
		return B;
	}

	FACFCameraPose Result;
	Result.Location = FMath::Lerp(A.Location, B.Location, Alpha);
	Result.Rotation = FQuat::Slerp(A.Rotation.Quaternion(), B.Rotation.Quaternion(), Alpha).Rotator();
	Result.FieldOfView = FMath::Lerp(A.FieldOfView, B.FieldOfView, Alpha);
	return Result;
}

float FACFCameraBlendSettings::EvaluateAlpha(float NormalizedTime) const
{
	if (IsCut())
	{
		return 1.f;
	}

	const float T = FMath::Clamp(NormalizedTime, 0.f, 1.f);
	const float Exp = FMath::Max(Exponent, 0.1f);
	switch (Curve)
	{
	case EACFCameraBlendCurve::Linear:
		return T;
	case EACFCameraBlendCurve::EaseIn:
		return FMath::InterpEaseIn(0.f, 1.f, T, Exp);
	case EACFCameraBlendCurve::EaseOut:
		return FMath::InterpEaseOut(0.f, 1.f, T, Exp);
	case EACFCameraBlendCurve::EaseInOut:
		return FMath::InterpEaseInOut(0.f, 1.f, T, Exp);
	case EACFCameraBlendCurve::Custom:
		return CustomCurve ? FMath::Clamp(CustomCurve->GetFloatValue(T), 0.f, 1.f) : T;
	default:
		return 1.f;
	}
}

FACFCameraBlendSettings FACFCameraBlendSettings::MakeCut()
{
	FACFCameraBlendSettings Cut;
	Cut.Curve = EACFCameraBlendCurve::Cut;
	Cut.Duration = 0.f;
	return Cut;
}
