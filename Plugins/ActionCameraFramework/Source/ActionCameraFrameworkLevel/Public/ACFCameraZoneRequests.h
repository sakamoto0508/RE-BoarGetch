#pragma once

#include "CoreMinimal.h"
#include "ACFCameraTypes.h"

class AActor;
class UACFCameraDirector;
class UACFCameraSubsystem;
class UACFCameraZoneComponent;

/** What one FACFCameraZoneRequestState::Update did (logs, tests). */
enum class EACFCameraZoneRequestEvent : uint8
{
	None,
	/** No zone before: the zone's camera was requested. */
	Requested,
	/** Zone to zone: the new zone's camera was requested, the old request released (the new Blend In blends). */
	Switched,
	/** Zone to no zone: the request was released (Blend Out if it was on screen). */
	Released,
	/** The request disappeared while the zone stayed selected (a retry is scheduled). */
	Lost,
	/** The lost request was requested again. */
	Retried,
	/** The zone's camera could not be requested (no preset): nothing is retried until the selection changes. */
	Failed,
};

/** Everything FACFCameraZoneRequestState needs from the player (the zone subsystem fills it; tests fill it by hand). */
struct FACFCameraZoneRequestContext
{
	/** The player's director. Required. */
	UACFCameraDirector* Director = nullptr;

	/** The player's camera subsystem: when set, requests go through it (it attaches the output modifier). */
	UACFCameraSubsystem* Camera = nullptr;

	/** Follow target of the request (the pawn). */
	FACFCameraTarget FollowTarget;

	/** The view on screen (the rotation handoff). */
	bool bHasViewRotation = false;
	FRotator ViewRotation = FRotator::ZeroRotator;

	/** Sets the player's control rotation (rotation handoff to a camera driven by the Control Rotation). Optional. */
	TFunction<void(const FRotator&)> SetControlRotation;

	/** Seconds (real time) for the retry delays. */
	double Time = 0.0;

	int32 Priority = 5;
	float RetryDelay = 1.f;
	float RetryMaxDelay = 8.f;
};

/**
 * Phase 7C-1B: the zone camera request of ONE local player (README 32 章). The player's selected zone (from the resolver)
 * and the director's active request are separate: this object owns at most one request handle, of the zone it last
 * acted on (the Active Zone), and brings it in line with the selected zone on every Update.
 *
 * - Zone A -> zone B: B is requested (B's Blend In blends), then A is released with a plain release.
 * - Zone -> no zone, the zone camera on screen: released with its Blend Out, "with return blend" when a request below takes
 *   over (ReleaseCameraWithReturnBlend: the exploration camera), "with blend" when the UE camera does (ReleaseCameraWithBlend).
 * - Zone -> no zone, the zone camera hidden (a Lock-On / Finisher above it): a plain release; the screen does not change.
 * - Rotation handoff (KeepView) only to the zone request itself, or to the request that takes over from it while it is on
 *   screen; never SetAllOrbitRotations, never a request above the zone.
 * - A request that disappears while the zone stays selected is requested again after a delay that doubles with each loss
 *   (never every frame). A zone without preset requests nothing and is not retried.
 */
class ACTIONCAMERAFRAMEWORKLEVEL_API FACFCameraZoneRequestState
{
public:
	/** Lower bound of the retry delay (seconds), whatever the settings say. */
	static const double MinRetryDelay;

	/** Brings the request in line with Selected (null: no zone). */
	EACFCameraZoneRequestEvent Update(const FACFCameraZoneRequestContext& Context, UACFCameraZoneComponent* Selected);

	/** Releases the request without any blend override or rotation change (World end, player gone) and forgets the zone. */
	void Release(UACFCameraDirector* Director);

	UACFCameraZoneComponent* GetActiveZone() const { return ActiveZone.Get(); }
	FACFCameraRequestHandle GetHandle() const { return Handle; }
	/** Losses since the zone was selected (or since the request survived Retry Max Delay). */
	int32 GetNumLost() const { return NumLost; }
	bool IsWaitingForRetry() const { return bWaitingForRetry; }
	double GetNextRetryTime() const { return NextRetryTime; }
	/** The zone's camera could not be requested (no preset). */
	bool HasFailed() const { return bFailed; }

private:
	bool Request(const FACFCameraZoneRequestContext& Context, UACFCameraZoneComponent& Zone);
	void Leave(const FACFCameraZoneRequestContext& Context, FACFCameraRequestHandle Old, const UACFCameraZoneComponent* OldZone);
	void WarnSamePriority(const FACFCameraZoneRequestContext& Context);
	static void HandOffRotation(const FACFCameraZoneRequestContext& Context, FACFCameraRequestHandle Target);

	TWeakObjectPtr<UACFCameraZoneComponent> ActiveZone;
	bool bHasActiveZone = false;
	FACFCameraRequestHandle Handle;
	FACFCameraTarget FollowTarget;
	double RequestTime = 0.0;
	double NextRetryTime = 0.0;
	int32 NumLost = 0;
	bool bWaitingForRetry = false;
	bool bFailed = false;
	bool bWarnedSamePriority = false;
};
