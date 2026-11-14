#pragma once

#include "CoreMinimal.h"

/**
 * Phase 7C-1A: which Camera Zone a player is in (README 32 章). Plain data and pure functions, so the rules are tested
 * without a World. The zone subsystem builds one candidate per registered zone for each player and frame.
 */
struct ACTIONCAMERAFRAMEWORKLEVEL_API FACFCameraZoneCandidate
{
	/** Zone Priority: higher wins. */
	int32 Priority = 0;
	/** World volume of the shape (cm^3): among equal priorities, the smaller (more specific) zone wins. */
	double Volume = 0.0;
	/** Final tie-break (the component's path name): unique, and the same in every run of the same map. */
	FString StableKey;
	/** The point (the pawn) is inside the shape. */
	bool bInside = false;
	/** The point is inside the shape grown by Exit Margin (only used for the current zone). */
	bool bInsideWithMargin = false;
	/** The zone the player had selected before this update. */
	bool bCurrent = false;
	/** Zone Enabled. A disabled zone is never selected (and is left at once, without the margin). */
	bool bEnabled = true;
};

namespace ACFCameraZoneResolver
{
	/** Relative tolerance under which two volumes count as equal (the next rule decides). */
	inline constexpr double VolumeTolerance = 1.0e-6;

	/**
	 * Can the candidate be selected: enabled, and inside its shape, or (spatial hysteresis) the current zone still inside
	 * its shape grown by Exit Margin. Entering never uses the margin.
	 */
	ACTIONCAMERAFRAMEWORKLEVEL_API bool IsEligible(const FACFCameraZoneCandidate& Candidate);

	/**
	 * Does A win over B (both eligible). The order: higher Priority; then the current zone (a tie never switches away from
	 * the zone the player is in); then the smaller volume; then the smaller StableKey. Strict: Beats(A, A) is false.
	 */
	ACTIONCAMERAFRAMEWORKLEVEL_API bool Beats(const FACFCameraZoneCandidate& A, const FACFCameraZoneCandidate& B);

	/** Index of the selected candidate, INDEX_NONE if none is eligible. Independent of the order of Candidates. */
	ACTIONCAMERAFRAMEWORKLEVEL_API int32 Resolve(TConstArrayView<FACFCameraZoneCandidate> Candidates);
}

/** Phase 7C-1A: point-in-shape tests of the zone shapes, in world units (transform, rotation and scale included). */
namespace ACFCameraZoneGeometry
{
	/**
	 * Box of BoxExtent (half size, unscaled) under Transform, grown by Margin cm on every side in world units. Scale
	 * multiplies the extent per axis (negative scale counts as positive); a zero scale on an axis contains nothing.
	 */
	ACTIONCAMERAFRAMEWORKLEVEL_API bool BoxContains(const FTransform& Transform, const FVector& BoxExtent, const FVector& Point, double Margin);
	ACTIONCAMERAFRAMEWORKLEVEL_API double BoxVolume(const FTransform& Transform, const FVector& BoxExtent);

	/** Sphere of ScaledRadius (world units, as USphereComponent::GetScaledSphereRadius) around Center, grown by Margin. */
	ACTIONCAMERAFRAMEWORKLEVEL_API bool SphereContains(const FVector& Center, double ScaledRadius, const FVector& Point, double Margin);
	ACTIONCAMERAFRAMEWORKLEVEL_API double SphereVolume(double ScaledRadius);
}
