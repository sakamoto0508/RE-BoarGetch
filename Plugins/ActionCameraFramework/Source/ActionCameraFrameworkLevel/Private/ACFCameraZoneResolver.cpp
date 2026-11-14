#include "ACFCameraZoneResolver.h"

bool ACFCameraZoneResolver::IsEligible(const FACFCameraZoneCandidate& Candidate)
{
	return Candidate.bEnabled && (Candidate.bInside || (Candidate.bCurrent && Candidate.bInsideWithMargin));
}

bool ACFCameraZoneResolver::Beats(const FACFCameraZoneCandidate& A, const FACFCameraZoneCandidate& B)
{
	if (A.Priority != B.Priority)
	{
		return A.Priority > B.Priority;
	}
	if (A.bCurrent != B.bCurrent)
	{
		return A.bCurrent;
	}
	const double Largest = FMath::Max(FMath::Abs(A.Volume), FMath::Abs(B.Volume));
	if (FMath::Abs(A.Volume - B.Volume) > Largest * VolumeTolerance)
	{
		return A.Volume < B.Volume;
	}
	return A.StableKey.Compare(B.StableKey, ESearchCase::CaseSensitive) < 0;
}

int32 ACFCameraZoneResolver::Resolve(TConstArrayView<FACFCameraZoneCandidate> Candidates)
{
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (IsEligible(Candidates[Index]) && (Best == INDEX_NONE || Beats(Candidates[Index], Candidates[Best])))
		{
			Best = Index;
		}
	}
	return Best;
}

bool ACFCameraZoneGeometry::BoxContains(const FTransform& Transform, const FVector& BoxExtent, const FVector& Point, double Margin)
{
	const FVector Scale = Transform.GetScale3D().GetAbs();
	if (Scale.X <= UE_SMALL_NUMBER || Scale.Y <= UE_SMALL_NUMBER || Scale.Z <= UE_SMALL_NUMBER || !Transform.IsValid())
	{
		return false; // a flattened box has no inside
	}
	// Rotation and translation only: the extent is scaled instead, so the margin stays in world centimetres on every axis.
	const FVector Local = Transform.InverseTransformPositionNoScale(Point);
	const FVector Half = BoxExtent.GetAbs() * Scale + FVector(FMath::Max(Margin, 0.0));
	return FMath::Abs(Local.X) <= Half.X && FMath::Abs(Local.Y) <= Half.Y && FMath::Abs(Local.Z) <= Half.Z;
}

double ACFCameraZoneGeometry::BoxVolume(const FTransform& Transform, const FVector& BoxExtent)
{
	const FVector Size = BoxExtent.GetAbs() * Transform.GetScale3D().GetAbs() * 2.0;
	return Size.X * Size.Y * Size.Z;
}

bool ACFCameraZoneGeometry::SphereContains(const FVector& Center, double ScaledRadius, const FVector& Point, double Margin)
{
	if (ScaledRadius <= UE_SMALL_NUMBER || !FMath::IsFinite(ScaledRadius))
	{
		return false;
	}
	const double Radius = ScaledRadius + FMath::Max(Margin, 0.0);
	return FVector::DistSquared(Center, Point) <= Radius * Radius;
}

double ACFCameraZoneGeometry::SphereVolume(double ScaledRadius)
{
	const double R = FMath::Max(ScaledRadius, 0.0);
	return 4.0 / 3.0 * UE_DOUBLE_PI * R * R * R;
}
