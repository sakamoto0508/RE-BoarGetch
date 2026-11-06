#include "Nodes/ACFCameraFraming.h"

ACFFraming::FLens ACFFraming::MakeLens(float HorizontalFovDegrees, float AspectRatio)
{
	FLens Lens;
	Lens.TanHalfH = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(HorizontalFovDegrees), 1.0, 170.0)) * 0.5);
	Lens.TanHalfV = Lens.TanHalfH / FMath::Max(static_cast<double>(AspectRatio), 0.1);
	return Lens;
}

bool ACFFraming::Project(const FVector& CameraLocation, const FRotator& CameraRotation, const FLens& Lens, const FVector& WorldPoint, FVector2D& OutScreen, double& OutDepth)
{
	const FVector Local = CameraRotation.UnrotateVector(WorldPoint - CameraLocation);
	OutDepth = Local.X;
	if (Local.X <= 1.0)
	{
		return false;
	}
	OutScreen.X = 0.5 * (Local.Y / Local.X) / Lens.TanHalfH;
	OutScreen.Y = 0.5 * (Local.Z / Local.X) / Lens.TanHalfV;
	return true;
}

double ACFFraming::ComposeAxis(double Screen, double Desired, double DeadSize, double SoftSize, float Alpha)
{
	const double DeadHalf = FMath::Max(DeadSize, 0.0) * 0.5;
	const double SoftHalf = FMath::Max(SoftSize * 0.5, DeadHalf);
	const double DeadEdge = FMath::Clamp(Screen, Desired - DeadHalf, Desired + DeadHalf);
	const double Damped = Screen + (DeadEdge - Screen) * FMath::Clamp(static_cast<double>(Alpha), 0.0, 1.0);
	return FMath::Clamp(Damped, Desired - SoftHalf, Desired + SoftHalf);
}

FRotator ACFFraming::AimAt(const FRotator& Rotation, const FVector& CameraLocation, const FLens& Lens, const FVector& WorldPoint, const FVector2D& DesiredScreen)
{
	if (FVector::DistSquared(CameraLocation, WorldPoint) < 1.0)
	{
		return Rotation;
	}
	// Direction (camera space) that projects to DesiredScreen.
	const double DesiredY = 2.0 * DesiredScreen.X * Lens.TanHalfH;
	const double DesiredZ = 2.0 * DesiredScreen.Y * Lens.TanHalfV;
	const double DesiredYaw = FMath::RadiansToDegrees(FMath::Atan(DesiredY));
	const double DesiredPitch = FMath::RadiansToDegrees(FMath::Atan2(DesiredZ, FMath::Sqrt(1.0 + DesiredY * DesiredY)));

	FRotator Result(Rotation.Pitch, Rotation.Yaw, 0.f);
	if (!FMath::IsFinite(Result.Pitch) || !FMath::IsFinite(Result.Yaw))
	{
		Result = (WorldPoint - CameraLocation).Rotation();
	}
	for (int32 Iteration = 0; Iteration < 6; ++Iteration)
	{
		const FVector Local = Result.UnrotateVector(WorldPoint - CameraLocation);
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
		const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(Local.Z, FMath::Sqrt(Local.X * Local.X + Local.Y * Local.Y)));
		const double DeltaYaw = FRotator::NormalizeAxis(Yaw - DesiredYaw);
		const double DeltaPitch = Pitch - DesiredPitch;
		Result.Yaw = FRotator::NormalizeAxis(Result.Yaw + DeltaYaw);
		Result.Pitch = FMath::Clamp(Result.Pitch + DeltaPitch, -89.0, 89.0);
		if (FMath::Abs(DeltaYaw) < 1e-4 && FMath::Abs(DeltaPitch) < 1e-4)
		{
			break;
		}
	}
	return Result;
}
