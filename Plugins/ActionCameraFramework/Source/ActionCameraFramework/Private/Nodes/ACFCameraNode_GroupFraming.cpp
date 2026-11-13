#include "Nodes/ACFCameraNode_GroupFraming.h"
#include "Nodes/ACFCameraFraming.h"
#include "Nodes/ACFCameraNodeUtils.h"

bool UACFCameraNode_GroupFraming::ApplyWeights(FACFCameraFramingSubjects& InOutSubjects, FVector& OutCenter)
{
	float MaxWeight = 0.f;
	if (!ACFTargetGroup::ComputeCenter(InOutSubjects, OutCenter, MaxWeight))
	{
		return false;
	}
	for (FACFCameraFramingSubject& Subject : InOutSubjects)
	{
		const double Influence = FMath::Clamp(static_cast<double>(Subject.Weight / MaxWeight), 0.0, 1.0);
		Subject.Location = FMath::Lerp(OutCenter, Subject.Location, Influence);
		Subject.Radius = static_cast<float>(Subject.Radius * Influence);
	}
	return true;
}

double UACFCameraNode_GroupFraming::ComputeRequiredDolly(const FACFCameraFramingSubjects& Subjects, const FVector& CameraLocation, const FRotator& CameraRotation,
	float FieldOfView, float AspectRatio, const FVector2D& FramingSize, float NearClearance, float HorizontalFovScale)
{
	const ACFFraming::FLens Lens = ACFFraming::MakeLens(FieldOfView, AspectRatio, HorizontalFovScale);
	// Tangent of the framing half-angles: a point (x, y) in camera space is inside when |y| <= F * x.
	const double Fh = Lens.TanHalfH * FMath::Clamp(FramingSize.X, 0.05, 1.0);
	const double Fv = Lens.TanHalfV * FMath::Clamp(FramingSize.Y, 0.05, 1.0);
	const double Sh = FMath::Sqrt(1.0 + Fh * Fh);
	const double Sv = FMath::Sqrt(1.0 + Fv * Fv);
	double Required = -UE_BIG_NUMBER;
	for (const FACFCameraFramingSubject& Subject : Subjects)
	{
		// Moving the camera back by D adds D to the depth x. A sphere is inside a side plane of the framing
		// pyramid when its centre is at least r from it: F * (x + D) - |y| >= r * sqrt(1 + F^2).
		const FVector Local = CameraRotation.UnrotateVector(Subject.Location - CameraLocation);
		const double R = Subject.Radius;
		Required = FMath::Max(Required, (FMath::Abs(Local.Y) + R * Sh) / Fh - Local.X);
		Required = FMath::Max(Required, (FMath::Abs(Local.Z) + R * Sv) / Fv - Local.X);
		Required = FMath::Max(Required, R + NearClearance - Local.X);
	}
	return Subjects.IsEmpty() ? 0.0 : Required;
}

double UACFCameraNode_GroupFraming::ComputeRequiredFov(const FACFCameraFramingSubjects& Subjects, const FVector& CameraLocation, const FRotator& CameraRotation,
	float AspectRatio, const FVector2D& FramingSize, float NearClearance, float MaxFov, float HorizontalFovScale)
{
	const double Aspect = FMath::Max(static_cast<double>(AspectRatio), 0.1);
	const double MaxTan = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(MaxFov), 1.0, 170.0)) * 0.5);
	const double SizeX = FMath::Clamp(FramingSize.X, 0.05, 1.0);
	const double SizeY = FMath::Clamp(FramingSize.Y, 0.05, 1.0);
	// Half-angle (in one camera plane) of the cone tangent to the sphere's cross-section.
	auto HalfAngleTan = [](double Lateral, double Depth, double Radius, double& OutTan)
	{
		const double Dist = FMath::Sqrt(Lateral * Lateral + Depth * Depth);
		const double Angle = FMath::Atan2(FMath::Abs(Lateral), Depth) + FMath::Asin(FMath::Clamp(Radius / FMath::Max(Dist, 1e-3), 0.0, 1.0));
		if (Angle >= FMath::DegreesToRadians(89.0))
		{
			return false;
		}
		OutTan = FMath::Tan(Angle);
		return true;
	};
	double NeededTanH = 0.0;
	for (const FACFCameraFramingSubject& Subject : Subjects)
	{
		const FVector Local = CameraRotation.UnrotateVector(Subject.Location - CameraLocation);
		double TanH = 0.0;
		double TanV = 0.0;
		if (Local.X <= Subject.Radius + NearClearance || !HalfAngleTan(Local.Y, Local.X, Subject.Radius, TanH) || !HalfAngleTan(Local.Z, Local.X, Subject.Radius, TanV))
		{
			NeededTanH = MaxTan; // behind / inside the near clearance: no FOV can frame it
			break;
		}
		// The vertical half-angle tangent is the horizontal one divided by the aspect ratio.
		NeededTanH = FMath::Max(NeededTanH, FMath::Max(TanH / SizeX, TanV / SizeY * Aspect));
	}
	// NeededTanH is in the rendered view; the pose FOV that renders it is smaller / larger by HorizontalFovScale.
	const double Scale = HorizontalFovScale > 0.f && FMath::IsFinite(HorizontalFovScale) ? static_cast<double>(HorizontalFovScale) : 1.0;
	const double PoseTan = NeededTanH >= MaxTan || Scale == 1.0 ? NeededTanH : NeededTanH / Scale;
	return FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Min(PoseTan, MaxTan)));
}

void UACFCameraNode_GroupFraming::ResetInterpolation(FInstancedStruct* RuntimeState) const
{
	if (FACFCameraGroupFramingState* State = ACFNodes::GetState<FACFCameraGroupFramingState>(RuntimeState))
	{
		State->bInitialized = false;
	}
}

void UACFCameraNode_GroupFraming::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraGroupFramingState* State = ACFNodes::GetState<FACFCameraGroupFramingState>(RuntimeState);
	if (!State)
	{
		return;
	}
	FACFCameraFramingSubjects Subjects;
	ACFTargetGroup::GatherSubjects(Context.Request, bFrameTargetGroup, FollowTargetSubject, LookAtTargetSubject, Subjects,
		Context.Scratch.bHasLockOnTarget ? &Context.Scratch.LockOnTargetLocation : nullptr);
	FVector Center = FVector::ZeroVector;
	const bool bHasSubjects = ApplyWeights(Subjects, Center);
	if (bHasSubjects)
	{
		// For the constraint solver (Collision node): re-checked after deocclusion / collision moved the camera.
		Context.Scratch.bHasFramingSubjects = true;
		Context.Scratch.FramingSubjects = Subjects;
		Context.Scratch.FramingSize = FramingSize;
	}

	const float Dt = Context.GetDeltaTime();
	const float Aspect = Context.Evaluation.AspectRatio;
	const float InFov = InOutPose.FieldOfView;
	const FVector Forward = InOutPose.Rotation.Vector();
	const float MinFov = FMath::Min(MinFieldOfView, MaxFieldOfView);
	const float MaxFov = FMath::Max(MinFieldOfView, MaxFieldOfView);

	// 1. Goal along the view axis (relative to the incoming camera, so it never accumulates).
	double GoalDolly = 0.0;
	if (bHasSubjects && Mode != EACFCameraGroupFramingMode::FieldOfView)
	{
		const double Required = ComputeRequiredDolly(Subjects, InOutPose.Location, InOutPose.Rotation, InFov, Aspect, FramingSize, NearClearance, Context.Evaluation.HorizontalFovScale);
		const double MinDolly = Policy == EACFCameraGroupFramingPolicy::KeepInside ? 0.0 : -static_cast<double>(MaxDollyIn);
		GoalDolly = FMath::Clamp(Required, MinDolly, static_cast<double>(MaxDollyOut));
	}

	// 2. Goal FOV at the goal position (FOV only covers what moving could not, without fighting the dolly damping).
	double GoalFovDelta = 0.0;
	if (bHasSubjects && Mode != EACFCameraGroupFramingMode::Distance)
	{
		const FVector GoalLocation = InOutPose.Location - Forward * GoalDolly;
		const double Required = ComputeRequiredFov(Subjects, GoalLocation, InOutPose.Rotation, Aspect, FramingSize, NearClearance, MaxFov, Context.Evaluation.HorizontalFovScale);
		const double GoalFov = Policy == EACFCameraGroupFramingPolicy::KeepInside
			? FMath::Max(static_cast<double>(InFov), FMath::Min(Required, static_cast<double>(MaxFov)))
			: FMath::Clamp(Required, static_cast<double>(MinFov), static_cast<double>(MaxFov));
		GoalFovDelta = GoalFov - InFov;
	}

	// 3. Damping: widen / pull back fast, narrow / move in slowly. Zero-delta evaluations never advance.
	auto Damp = [this, Dt](float Current, double Goal)
	{
		const float Time = Goal > Current ? WidenDampingTime : NarrowDampingTime;
		return static_cast<float>(Current + (Goal - Current) * ACFCameraMath::ExpAlpha(Time, Dt));
	};
	if (!State->bInitialized)
	{
		State->Dolly = static_cast<float>(GoalDolly);
		State->FovDelta = static_cast<float>(GoalFovDelta);
		State->bInitialized = true;
	}
	else
	{
		State->Dolly = Damp(State->Dolly, GoalDolly);
		State->FovDelta = Damp(State->FovDelta, GoalFovDelta);
	}
	InOutPose.Location -= Forward * State->Dolly;
	InOutPose.FieldOfView = FMath::Clamp(InFov + State->FovDelta, 1.f, 170.f);

#if !UE_BUILD_SHIPPING
	FACFCameraRigDebug& Debug = Context.Scratch.Debug;
	Debug.bHasGroup = true;
	Debug.GroupCenter = bHasSubjects ? Center : InOutPose.Location + Forward * 100.0;
	Debug.GroupDolly = State->Dolly;
	Debug.GroupFovDelta = State->FovDelta;
	Debug.GroupFramingSize = FramingSize;
	Debug.NumGroupSubjects = FMath::Min(Subjects.Num(), FACFCameraRigDebug::MaxGroupSubjects);
	for (int32 Index = 0; Index < Debug.NumGroupSubjects; ++Index)
	{
		Debug.GroupSubjects[Index] = FVector4(Subjects[Index].Location, Subjects[Index].Radius);
	}
#endif
}
