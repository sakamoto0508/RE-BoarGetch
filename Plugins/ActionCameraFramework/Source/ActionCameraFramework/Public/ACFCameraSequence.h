#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ACFCameraTypes.h"
#include "ACFCameraSequence.generated.h"

class AActor;
class UACFCameraPreset;
class UACFCameraEffectPreset;

/**
 * Cinematic camera sequences / finisher cameras (Phase 4C).
 *
 * A sequence is a list of shots. Each shot is played as an ordinary camera request (its own preset, priority,
 * blend-in, targets) owned by the sequence playback, so priorities, blends, Collision, the output guard and effects
 * work exactly as for gameplay cameras. Nothing here plays animations or touches gameplay: the game starts / steers /
 * ends a playback through its handle (Blueprint, C++ or the ACF anim notifies).
 */

/** Which actor of the playback a shot uses for a camera target. */
UENUM(BlueprintType)
enum class EACFCameraShotRole : uint8
{
	None,
	/** The actor performing the finisher (usually the player's pawn). */
	Attacker,
	/** The actor receiving it (enemy / boss). */
	Target,
};

/** When a shot ends on its own. Events (Advance / Jump / anim notifies) can always end a shot earlier. */
UENUM(BlueprintType)
enum class EACFCameraShotAdvanceMode : uint8
{
	/** Ends after Duration (sequence time). */
	Duration,
	/**
	 * Waits for an Advance event (anim notify, gameplay). Duration is a safety timeout (0 = wait forever), so a missing
	 * notify (montage interrupted, notify filtered by LOD) can never leave the camera stuck.
	 */
	WaitForEvent,
};

/** What a playback does while a higher priority camera has taken control. */
UENUM(BlueprintType)
enum class EACFCameraSequenceInterruptPolicy : uint8
{
	/** Shot time keeps running (the animation keeps playing too); the shot shows again once the interruption ends. */
	Continue,
	/** Shot time is frozen while not in control. */
	Pause,
	/** The playback ends (reason Interrupted). */
	Cancel,
};

/** What a playback does when its attacker or target is destroyed. */
UENUM(BlueprintType)
enum class EACFCameraSequenceActorLostPolicy : uint8
{
	/** End and return to the previous camera with the Return Blend (default, safe). */
	Finish,
	/** End and return with the Cancel Blend. */
	Cancel,
	/** Keep playing; nodes hold the last known target position (Target Lost Behavior). */
	Continue,
};

/** Why a shot / playback ended (notifications and state). */
UENUM(BlueprintType)
enum class EACFCameraSequenceEndReason : uint8
{
	None,
	/** Shot: its time (Duration / timeout) ran out. Playback: the last shot ended. */
	Completed,
	/** Shot: Advance event. Playback: Finish was called. */
	Advanced,
	/** Shot: a Jump skipped it. */
	Skipped,
	/** Shot: not playable here (fallback probe failed and Skip If Infeasible). */
	Infeasible,
	Cancelled,
	/** A higher priority camera took over (Interrupt Policy = Cancel). */
	Interrupted,
	/** Attacker or target destroyed. */
	ActorLost,
	/** The shot's camera request disappeared (released by somebody else, instigator destroyed, Release All Cameras). */
	RequestLost,
	/** Another playback was started with Stop Other Sequences. */
	Replaced,
};

UENUM(BlueprintType)
enum class EACFCameraSequenceEventType : uint8
{
	ShotStarted,
	ShotEnded,
	SequenceEnded,
};

/** Commands the game / anim notifies send to a playback. */
UENUM(BlueprintType)
enum class EACFCameraSequenceCommand : uint8
{
	/** End the current shot and start the next one (the last shot ends the playback). */
	AdvanceShot,
	/** Jump to the shot named ShotName (skipping the shots in between). */
	JumpToShot,
	/** Freeze shot time (counted: every Hold needs a Release). */
	HoldShot,
	ReleaseShot,
	/** Normal end: return to the previous camera with the Return Blend. */
	Finish,
	/** Abort: return with the Cancel Blend, effects cut immediately. */
	Cancel,
};

/** How long an effect started by a shot lives. It is always stopped when the playback ends. */
UENUM(BlueprintType)
enum class EACFCameraShotEffectLifetime : uint8
{
	/** Released when the shot ends. */
	Shot,
	/** Released when the playback ends. */
	Sequence,
	/** Plays to its own end (one-shot impulses); still cut if the playback is cancelled. */
	FireAndForget,
};

/** One camera effect started when a shot starts (owned by the playback). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraShotEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	TObjectPtr<UACFCameraEffectPreset> Preset = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect", meta = (ClampMin = "0.0"))
	float Scale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	EACFCameraShotEffectLifetime Lifetime = EACFCameraShotEffectLifetime::Shot;
};

/** Which playback actors a shot targets. */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraShotTargets
{
	GENERATED_BODY()

	/** Request Follow Target (Follow / Orbital Follow / Shot Anchor pivot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	EACFCameraShotRole Follow = EACFCameraShotRole::Attacker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	FName FollowSocket;

	/** Request Look At Target (Look At / Lock-On / Shot Anchor line of action). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	EACFCameraShotRole LookAt = EACFCameraShotRole::Target;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	FName LookAtSocket;

	/** Target Group members (Group Framing). Weight 0 = not in the group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets", meta = (ClampMin = "0.0"))
	float AttackerGroupWeight = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets", meta = (ClampMin = "0.0"))
	float TargetGroupWeight = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets", meta = (ClampMin = "0.0", Units = cm))
	float GroupRadius = 80.f;
};

/**
 * Narrow-space fallback, checked once when the shot starts (never mid-shot, so it can never cause a jump inside a
 * shot). The shot's camera is evaluated once without time ("probe"): if Collision has to pull it closer than
 * MinDistanceRatio of its intended distance, or (bRequireLineOfSight) the attacker / target is hidden from it, the
 * shot is "infeasible" and the Fallback Preset is used instead (else the shot is skipped, else it plays anyway:
 * Collision still keeps it out of walls). Costs one extra rig evaluation + up to 3 traces per shot start.
 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraShotFallback
{
	GENERATED_BODY()

	/** Used when the shot is infeasible. Null = no probe unless bSkipIfInfeasible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fallback")
	TObjectPtr<UACFCameraPreset> FallbackPreset = nullptr;

	/** Without a fallback preset: skip an infeasible shot instead of playing it squeezed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fallback")
	bool bSkipIfInfeasible = false;

	/** Collided distance / intended distance (from the Collision pivot) below which the shot is infeasible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fallback", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinDistanceRatio = 0.6f;

	/** Also infeasible if a line trace from the probed camera to the attacker / target hits something else first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fallback")
	bool bRequireLineOfSight = true;

	bool NeedsProbe() const { return FallbackPreset != nullptr || bSkipIfInfeasible; }
};

/** One shot of a camera sequence (designer data, shared and immutable at runtime). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraShot
{
	GENERATED_BODY()

	/** For Jump To Shot, notifications and debugging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	FName Name;

	/** Camera used by the shot: dynamic (follows attacker / target) or fixed (Shot Anchor with Freeze At Shot Start). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	TObjectPtr<UACFCameraPreset> Preset = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	EACFCameraShotAdvanceMode AdvanceMode = EACFCameraShotAdvanceMode::Duration;

	/** Seconds of sequence time (Duration mode) / safety timeout (Wait For Event, 0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot", meta = (ClampMin = "0.0", Units = s))
	float Duration = 1.f;

	/** Transition INTO this shot (Cut or blend from the previous shot / the gameplay camera for the first shot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	FACFCameraBlendSettings BlendIn;

	/** Added to the playback priority (lets one shot sit above / below the others, e.g. under a boss intro). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	int32 PriorityOffset = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	FACFCameraShotTargets Targets;

	/** Collision node's Deoccluder for this shot (if the preset has one). Off keeps a staged composition untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	bool bEnableDeoccluder = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	FACFCameraShotFallback Fallback;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	TArray<FACFCameraShotEffect> Effects;

	FACFCameraShot()
	{
		BlendIn = FACFCameraBlendSettings::MakeCut();
	}
};

/**
 * Data asset: an ordered list of shots plus how the playback starts / ends. Shared and immutable at runtime; every
 * playback keeps its own state (FACFCameraSequencePlayback), so several local players can play it at once.
 */
UCLASS(BlueprintType)
class ACTIONCAMERAFRAMEWORK_API UACFCameraSequence : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequence")
	TArray<FACFCameraShot> Shots;

	/** Request priority of the shots (+ each shot's offset). High so gameplay cameras (lock-on ...) are overridden. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequence")
	int32 Priority = 1000;

	/** Back to the previous camera (lock-on, gameplay camera or UE camera) after the last shot / Finish. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequence")
	FACFCameraBlendSettings ReturnBlend;

	/** Back to the previous camera on Cancel (default: cut). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequence")
	FACFCameraBlendSettings CancelBlend;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequence")
	EACFCameraSequenceInterruptPolicy InterruptPolicy = EACFCameraSequenceInterruptPolicy::Continue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequence")
	EACFCameraSequenceActorLostPolicy ActorLostPolicy = EACFCameraSequenceActorLostPolicy::Finish;

	/**
	 * Multiply sequence time by the attacker's Custom Time Dilation (hit-stop on the attacker also freezes the camera
	 * timing, like its montage).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequence")
	bool bUseAttackerTimeDilation = true;

	/** Attacker / target never push the camera (added to the shots' Collision Ignored Actors). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequence")
	bool bCollisionIgnoresRoleActors = true;

	UACFCameraSequence()
	{
		ReturnBlend.Duration = 0.6f;
		CancelBlend = FACFCameraBlendSettings::MakeCut();
	}

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

/** Opaque id of one sequence playback (process-unique, so a stale handle never controls a newer playback). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraSequenceHandle
{
	GENERATED_BODY()

	FACFCameraSequenceHandle() = default;
	explicit FACFCameraSequenceHandle(int32 InId) : Id(InId) {}

	bool IsValid() const { return Id != INDEX_NONE; }
	void Invalidate() { Id = INDEX_NONE; }
	int32 GetId() const { return Id; }
	bool operator==(const FACFCameraSequenceHandle& Other) const { return Id == Other.Id; }
	bool operator!=(const FACFCameraSequenceHandle& Other) const { return Id != Other.Id; }
	friend uint32 GetTypeHash(const FACFCameraSequenceHandle& Handle) { return ::GetTypeHash(Handle.Id); }
	FString ToString() const { return IsValid() ? FString::Printf(TEXT("ACFSequence#%d"), Id) : TEXT("ACFSequence#None"); }

private:
	UPROPERTY()
	int32 Id = INDEX_NONE;
};

/** Per-play parameters (the sequence asset stays shared). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraSequenceParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence")
	TWeakObjectPtr<AActor> Attacker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence")
	TWeakObjectPtr<AActor> Target;

	/** Overrides the asset's priority. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence", meta = (InlineEditConditionToggle))
	bool bOverridePriority = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence", meta = (EditCondition = "bOverridePriority"))
	int32 Priority = 1000;

	/**
	 * Sequence time scale. Match it to the montage play rate when shots are timed by Duration (event-driven shots do
	 * not need it). Can be changed while playing (Set ACF Camera Sequence Play Rate).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence", meta = (ClampMin = "0.0"))
	float PlayRate = 1.f;

	/** Optional owner: destroying it ends the playback (its requests use it as instigator). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence")
	TWeakObjectPtr<UObject> Instigator;

	/** End (reason Replaced) every other playback of this player first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence")
	bool bStopOtherSequences = true;

	/** First shot (Jump straight into the middle of a sequence, e.g. after a skip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence", meta = (ClampMin = "0"))
	int32 StartShot = 0;
};

/** Snapshot of a playback (Blueprint / UI / debugging). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraSequenceState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	bool bIsPlaying = false;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	int32 ShotIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	int32 NumShots = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	FName ShotName;

	/** Sequence time spent in the current shot (scaled by play rate / dilation, frozen while held). */
	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	float ShotTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	float ShotDuration = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	float SequenceTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	float PlayRate = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	bool bHeld = false;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	bool bWaitingForEvent = false;

	/** The current shot's camera is the one in control (false while interrupted or before the first camera update). */
	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	bool bInControl = false;

	/** The current shot plays its Fallback Preset. */
	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	bool bUsingFallback = false;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	FACFCameraRequestHandle ShotRequest;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	int32 NumOwnedEffects = 0;
};

/** Notification of a playback (shot start / end, playback end). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraSequenceEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	FACFCameraSequenceHandle Sequence;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	EACFCameraSequenceEventType Type = EACFCameraSequenceEventType::ShotStarted;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	int32 ShotIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	FName ShotName;

	/** ShotEnded / SequenceEnded only. */
	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	EACFCameraSequenceEndReason Reason = EACFCameraSequenceEndReason::None;

	/** ShotStarted: the Fallback Preset is used. */
	UPROPERTY(BlueprintReadOnly, Category = "Sequence")
	bool bFallback = false;
};
