#include "ACFCameraTypes.h"
#include "ACFCameraHealth.h"
#include "Curves/CurveFloat.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

namespace ACFTargetImpl
{
	/** Phase 6B / B-4: the one input boundary for target transforms (every node resolves targets here). */
	static bool AcceptTransform(const FTransform& Transform, const UObject* Object)
	{
		if (ACFCameraValidation::IsValidTransform(Transform))
		{
			return true;
		}
		const TCHAR* What = !ACFCameraValidation::IsFinite(Transform.GetLocation()) ? TEXT("target location")
			: (!ACFCameraValidation::IsValidQuat(Transform.GetRotation()) ? TEXT("target rotation") : TEXT("target scale"));
		FACFCameraHealth::ReportActive(EACFCameraHealthIssue::InvalidTarget, Object ? Object->GetFName() : NAME_None, What, TEXT("target not resolved this frame (last valid transform held)"));
		return false;
	}
}

bool FACFCameraTarget::Resolve(FTransform& OutTransform, const UObject*& OutObject) const
{
	if (const USceneComponent* SceneComponent = Component.Get())
	{
		OutTransform = SocketName.IsNone() ? SceneComponent->GetComponentTransform() : SceneComponent->GetSocketTransform(SocketName);
		OutObject = SceneComponent;
		return ACFTargetImpl::AcceptTransform(OutTransform, SceneComponent);
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
		return ACFTargetImpl::AcceptTransform(OutTransform, TargetActor);
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

// -------------------------------------------------------------------------------------------------
// Target group

void FACFCameraTargetGroup::AddOrUpdate(const FACFCameraTargetGroupMember& Member)
{
	if (!Member.Target.IsSet())
	{
		return;
	}
	if (FACFCameraTargetGroupMember* Existing = Members.FindByPredicate([&Member](const FACFCameraTargetGroupMember& M) { return M.RefersTo(Member.Target); }))
	{
		*Existing = Member;
		return;
	}
	Members.Add(Member);
}

bool FACFCameraTargetGroup::Remove(const FACFCameraTarget& Target)
{
	return Members.RemoveAll([&Target](const FACFCameraTargetGroupMember& M) { return M.RefersTo(Target); }) > 0;
}

int32 FACFCameraTargetGroup::RemoveLostMembers()
{
	return Members.RemoveAll([](const FACFCameraTargetGroupMember& M)
	{
		return !M.Target.IsAlive(); // alive but with an invalid transform this frame is not "lost" (B-4)
	});
}
