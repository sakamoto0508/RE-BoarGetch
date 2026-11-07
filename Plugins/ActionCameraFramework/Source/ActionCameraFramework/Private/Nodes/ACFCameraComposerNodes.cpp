#include "Nodes/ACFCameraNode_Composers.h"
#include "Nodes/ACFCameraNodeUtils.h"

namespace ACFComposer
{
#if !UE_BUILD_SHIPPING
	static void PublishDebug(const FACFCameraNodeContext& Context, const FACFCameraScreenComposition& Composition, const FVector2D& OnScreen)
	{
		FACFCameraRigDebug& Debug = Context.Scratch.Debug;
		Debug.bHasComposition = true;
		Debug.ScreenPosition = Composition.ScreenPosition;
		Debug.DeadZone = Composition.DeadZoneSize;
		Debug.SoftZone = Composition.SoftZoneSize;
		Debug.TargetOnScreen = OnScreen;
	}
#endif
}

// ---------------------------------------------------------------------------------------------
// Rotation Composer

void UACFCameraNode_RotationComposer::ResetInterpolation(FInstancedStruct* RuntimeState) const
{
	if (FACFCameraRotationComposerState* State = ACFNodes::GetState<FACFCameraRotationComposerState>(RuntimeState))
	{
		State->Tracking.ResetInterpolation();
		State->bInitialized = false;
	}
}

void UACFCameraNode_RotationComposer::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraRotationComposerState* State = ACFNodes::GetState<FACFCameraRotationComposerState>(RuntimeState);
	FACFCameraTrackedTarget Target;
	const float Dt = Context.GetDeltaTime();
	if (!State || !ACFNodes::TrackAimSource(State->Tracking, Context, TargetSource, TargetMotion, Target))
	{
		return;
	}
	const FVector Point = ACFNodes::ApplyOffset(Target.Predicted, Target.Transform.GetRotation(), TargetOffset, OffsetSpace);
	const ACFFraming::FLens Lens = ACFFraming::MakeLens(InOutPose.FieldOfView, Context.Evaluation.AspectRatio);
	const FRotator Base(InOutPose.Rotation.Pitch, InOutPose.Rotation.Yaw, 0.f);
	const FVector Camera = InOutPose.Location;

	FRotator Rotation;
	FVector2D OnScreen;
	double Depth = 0.0;
	if (!State->bInitialized || Target.bTeleported)
	{
		// Activation / teleport: frame exactly at the desired screen position.
		Rotation = ACFFraming::AimAt(Base, Camera, Lens, Point, Composition.ScreenPosition);
		State->bInitialized = true;
	}
	else
	{
		Rotation = FRotator(FMath::Clamp(Base.Pitch + State->Correction.Y, -89.0, 89.0), Base.Yaw + State->Correction.X, 0.f);
		if (!ACFFraming::Project(Camera, Rotation, Lens, Point, OnScreen, Depth))
		{
			// Target behind the camera (e.g. orbit swung past it): re-aim at once.
			Rotation = ACFFraming::AimAt(Base, Camera, Lens, Point, Composition.ScreenPosition);
		}
		else
		{
			const FVector2D Desired(
				ACFFraming::ComposeAxis(OnScreen.X, Composition.ScreenPosition.X, Composition.DeadZoneSize.X, Composition.SoftZoneSize.X, ACFCameraMath::ExpAlpha(Damping.X, Dt)),
				ACFFraming::ComposeAxis(OnScreen.Y, Composition.ScreenPosition.Y, Composition.DeadZoneSize.Y, Composition.SoftZoneSize.Y, ACFCameraMath::ExpAlpha(Damping.Y, Dt)));
			if (!Desired.Equals(OnScreen, 1e-6))
			{
				Rotation = ACFFraming::AimAt(Rotation, Camera, Lens, Point, Desired);
			}
		}
	}
	State->Correction = FVector2D(FRotator::NormalizeAxis(Rotation.Yaw - Base.Yaw), Rotation.Pitch - Base.Pitch);
	InOutPose.Rotation = FRotator(Rotation.Pitch, Rotation.Yaw, 0.f);

#if !UE_BUILD_SHIPPING
	ACFFraming::Project(Camera, InOutPose.Rotation, Lens, Point, OnScreen, Depth);
	ACFComposer::PublishDebug(Context, Composition, OnScreen);
	Context.Scratch.Debug.bHasLookAtTarget = true;
	Context.Scratch.Debug.LookAtTarget = Point;
	Context.Scratch.Debug.bHasPrediction |= TargetMotion.Lookahead.Time > 0.f;
	if (TargetMotion.Lookahead.Time > 0.f)
	{
		Context.Scratch.Debug.PredictedTarget = Target.Predicted;
	}
#endif
}

// ---------------------------------------------------------------------------------------------
// Position Composer

void UACFCameraNode_PositionComposer::ResetInterpolation(FInstancedStruct* RuntimeState) const
{
	if (FACFCameraPositionComposerState* State = ACFNodes::GetState<FACFCameraPositionComposerState>(RuntimeState))
	{
		State->Tracking.ResetInterpolation();
		State->bInitialized = false;
	}
}

void UACFCameraNode_PositionComposer::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	FACFCameraPositionComposerState* State = ACFNodes::GetState<FACFCameraPositionComposerState>(RuntimeState);
	FACFCameraTrackedTarget Target;
	const float Dt = Context.GetDeltaTime();
	if (!State || !State->Tracking.Track(Context.Request.FollowTarget, TargetMotion, Dt, Target))
	{
		return;
	}
	const FVector Point = ACFNodes::ApplyOffset(Target.Predicted, Target.Transform.GetRotation(), TargetOffset, OffsetSpace);
	const ACFFraming::FLens Lens = ACFFraming::MakeLens(InOutPose.FieldOfView, Context.Evaluation.AspectRatio);
	const FRotator Rotation = InOutPose.Rotation;
	const FVector Forward = Rotation.RotateVector(FVector::ForwardVector);
	const FVector Right = Rotation.RotateVector(FVector::RightVector);
	const FVector Up = Rotation.RotateVector(FVector::UpVector);
	const FVector2D Desired = Composition.ScreenPosition;
	const double Distance = CameraDistance;

	// Camera location that puts Point exactly at the desired screen position at CameraDistance.
	auto Ideal = [&]()
	{
		return Point - Forward * Distance
			- Right * (2.0 * Desired.X * Distance * Lens.TanHalfH)
			- Up * (2.0 * Desired.Y * Distance * Lens.TanHalfV);
	};

	FVector Camera;
	const FVector Local = Rotation.UnrotateVector(Point - State->CameraLocation);
	if (!State->bInitialized || Target.bTeleported || Local.X <= 1.0)
	{
		Camera = Ideal();
		State->bInitialized = true;
	}
	else
	{
		Camera = State->CameraLocation;
		const double Depth = Local.X;
		const double NewDepth = Depth + (Distance - Depth) * ACFCameraMath::ExpAlpha(Damping.X, Dt);
		Camera += Forward * (Depth - NewDepth);

		const FVector2D Screen(0.5 * (Local.Y / NewDepth) / Lens.TanHalfH, 0.5 * (Local.Z / NewDepth) / Lens.TanHalfV);
		const double NewX = ACFFraming::ComposeAxis(Screen.X, Desired.X, Composition.DeadZoneSize.X, Composition.SoftZoneSize.X, ACFCameraMath::ExpAlpha(Damping.Y, Dt));
		const double NewY = ACFFraming::ComposeAxis(Screen.Y, Desired.Y, Composition.DeadZoneSize.Y, Composition.SoftZoneSize.Y, ACFCameraMath::ExpAlpha(Damping.Z, Dt));
		// Moving the camera right moves the target left on screen.
		Camera += Right * ((Screen.X - NewX) * 2.0 * NewDepth * Lens.TanHalfH);
		Camera += Up * ((Screen.Y - NewY) * 2.0 * NewDepth * Lens.TanHalfV);
	}
	State->CameraLocation = Camera;
	InOutPose.Location = Camera;

	Context.Scratch.bHasPivot = true;
	Context.Scratch.Pivot = Target.Location;
	Context.Scratch.PivotActor = Target.Actor;
#if !UE_BUILD_SHIPPING
	FVector2D OnScreen;
	double Depth = 0.0;
	ACFFraming::Project(Camera, Rotation, Lens, Point, OnScreen, Depth);
	ACFComposer::PublishDebug(Context, Composition, OnScreen);
	Context.Scratch.Debug.bHasFollowTarget = true;
	Context.Scratch.Debug.FollowTarget = Target.Location;
	Context.Scratch.Debug.bHasPrediction |= TargetMotion.Lookahead.Time > 0.f;
	if (TargetMotion.Lookahead.Time > 0.f)
	{
		Context.Scratch.Debug.PredictedTarget = Target.Predicted;
	}
#endif
}
