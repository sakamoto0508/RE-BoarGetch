#pragma once

#include "CoreMinimal.h"
#include "ACFCameraSequence.h"
#include "Effects/ACFCameraEffectTypes.h"
#include "ACFCameraSequencePlayer.generated.h"

class UACFCameraDirector;
struct FACFCameraEvaluationContext;

/**
 * Runtime state of one sequence playback (one handle). Owned by the director of one local player.
 *
 * Ownership: the playback owns AT MOST ONE camera request (its current shot) and the effects its shots started.
 * A shot switch adds the next shot's request first and then removes the previous one (which the director keeps
 * alive only as the live source of the blend), and releases the previous shot's Shot-lifetime effects. Ending the
 * playback removes its request (with the return / cancel blend) and stops every effect it still owns.
 */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraSequencePlayback
{
	GENERATED_BODY()

	struct FOwnedEffect
	{
		FACFCameraEffectHandle Handle;
		EACFCameraShotEffectLifetime Lifetime = EACFCameraShotEffectLifetime::Shot;
	};

	FACFCameraSequenceHandle Handle;

	UPROPERTY()
	TObjectPtr<const UACFCameraSequence> Sequence = nullptr;

	UPROPERTY()
	FACFCameraSequenceParams Params;

	/** Current shot (started), INDEX_NONE before the first camera update. */
	int32 ShotIndex = INDEX_NONE;
	float ShotTime = 0.f;
	float SequenceTime = 0.f;
	float PlayRate = 1.f;
	int32 HoldCount = 0;
	bool bStarted = false;
	bool bUsingFallback = false;
	bool bPendingAdvance = false;
	int32 PendingJump = INDEX_NONE;
	FACFCameraRequestHandle Request;
	bool bTracksAttacker = false;
	bool bTracksTarget = false;
	bool bTracksInstigator = false;
	TArray<FOwnedEffect> Effects;
};

/** Result of evaluating a camera once without time (shot feasibility probe). */
struct FACFCameraProbeResult
{
	/** Pose before / after the PostBlend stage (Collision). */
	FACFCameraPose DesiredPose;
	FACFCameraPose Pose;
	bool bHasConstraint = false;
	FVector Pivot = FVector::ZeroVector;
	ECollisionChannel Channel = ECC_Camera;
	TArray<const AActor*, TInlineAllocator<4>> IgnoredActors;
};

/**
 * Plays camera sequences for one local player on top of the director's camera requests and effects (no separate
 * camera manager). Ticked by UACFCameraDirector::Evaluate inside its once-per-frame guard, before the active request
 * is chosen, so a shot switch is visible in the same frame and time advances exactly once per frame.
 *
 * Time: sequence time = camera DeltaTime (game time: global time dilation included, nothing while the world is
 * paused) x PlayRate x attacker Custom Time Dilation (optional). It is NOT read from a montage: shots that must match
 * the animation should end on events (anim notifies), with Duration as a timeout. See README "Finisher / Cinematic".
 */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraSequencePlayer
{
	GENERATED_BODY()

	FACFCameraSequenceHandle Play(UACFCameraDirector& Director, const UACFCameraSequence* Sequence, const FACFCameraSequenceParams& Params);

	/**
	 * Applies a command. Advance / Jump are latched and applied by the next camera update (several Advance events in
	 * one frame advance once; the last Jump wins). Hold / Release / Finish / Cancel apply immediately.
	 */
	bool SendCommand(UACFCameraDirector& Director, FACFCameraSequenceHandle Handle, EACFCameraSequenceCommand Command, FName ShotName = NAME_None);

	/** Jump by index (INDEX_NONE / out of range = rejected). */
	bool JumpToShot(FACFCameraSequenceHandle Handle, int32 ShotIndex);

	/** Sends Command to every playback whose attacker or target is Actor (null = all). Returns how many received it. */
	int32 SendCommandToActor(UACFCameraDirector& Director, const AActor* Actor, EACFCameraSequenceCommand Command, FName ShotName = NAME_None);

	bool SetPlayRate(FACFCameraSequenceHandle Handle, float PlayRate);
	bool IsPlaying(FACFCameraSequenceHandle Handle) const { return Find(Handle) != nullptr; }
	bool GetState(const UACFCameraDirector& Director, FACFCameraSequenceHandle Handle, FACFCameraSequenceState& OutState) const;
	int32 Num() const { return Playbacks.Num(); }
	void GetHandles(TArray<FACFCameraSequenceHandle>& Out) const
	{
		Out.Reset();
		for (const FACFCameraSequencePlayback& Playback : Playbacks)
		{
			Out.Add(Playback.Handle);
		}
	}
	const FACFCameraSequencePlayback* FindPlayback(FACFCameraSequenceHandle Handle) const { return Find(Handle); }

	/** Ends every playback (reason Cancelled). */
	void StopAll(UACFCameraDirector& Director, bool bImmediate);

	/** Once per frame, from the director's Evaluate (before the active request is chosen). */
	void Tick(UACFCameraDirector& Director, const FACFCameraEvaluationContext& Context);

	/** Notifications since the last call (the subsystem broadcasts them outside the camera update). */
	void ConsumeEvents(TArray<FACFCameraSequenceEvent>& Out);
	bool HasEvents() const { return Events.Num() > 0; }

private:
	FACFCameraSequencePlayback* Find(FACFCameraSequenceHandle Handle);
	const FACFCameraSequencePlayback* Find(FACFCameraSequenceHandle Handle) const;
	int32 FindIndex(FACFCameraSequenceHandle Handle) const;

	void TickPlayback(UACFCameraDirector& Director, int32 Index, const FACFCameraEvaluationContext& Context);
	/** Starts shot Index, passing through shots fully covered by Carry / skipped as infeasible. False if the playback ended. */
	bool BeginShotChain(UACFCameraDirector& Director, int32 PlaybackIndex, int32 ShotIndex, float Carry, const FACFCameraEvaluationContext& Context);
	bool StartShot(UACFCameraDirector& Director, FACFCameraSequencePlayback& Playback, int32 ShotIndex, float Carry, const FACFCameraEvaluationContext& Context, bool& bOutInfeasible);
	void EndCurrentShot(UACFCameraDirector& Director, FACFCameraSequencePlayback& Playback, EACFCameraSequenceEndReason Reason);
	void EndPlayback(UACFCameraDirector& Director, int32 Index, EACFCameraSequenceEndReason Reason, const FACFCameraBlendSettings* ReturnBlend, bool bCutEffects, bool bShotEnded = false);
	FACFCameraRequestParams MakeShotParams(const FACFCameraSequencePlayback& Playback, const FACFCameraShot& Shot, int32 ShotIndex) const;
	bool IsShotFeasible(UACFCameraDirector& Director, const FACFCameraSequencePlayback& Playback, const FACFCameraShot& Shot, const FACFCameraRequestParams& ShotParams, const FACFCameraEvaluationContext& Context) const;
	void PushEvent(const FACFCameraSequencePlayback& Playback, EACFCameraSequenceEventType Type, int32 ShotIndex, EACFCameraSequenceEndReason Reason, bool bFallback = false);

	UPROPERTY()
	TArray<FACFCameraSequencePlayback> Playbacks;

	TArray<FACFCameraSequenceEvent> Events;
	bool bTicking = false;
};
