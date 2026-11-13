#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "ACFCameraTypes.generated.h"

class AActor;
class APlayerController;
class UCurveFloat;
class USceneComponent;
class UWorld;

ACTIONCAMERAFRAMEWORK_API DECLARE_LOG_CATEGORY_EXTERN(LogActionCamera, Log, All);

/**
 * The ACF.Sample.* console commands (Core and Sequencer modules) that drive the sample content of
 * /ActionCameraFramework/Samples (README 9 章 / 23 章). Never in Shipping; a project can also leave them out of other
 * builds by defining ACF_WITH_SAMPLE_COMMANDS=0 (e.g. GlobalDefinitions of its game Target.cs).
 */
#ifndef ACF_WITH_SAMPLE_COMMANDS
#define ACF_WITH_SAMPLE_COMMANDS (!UE_BUILD_SHIPPING)
#endif

/**
 * Minimal camera pose produced by ACF rigs.
 *
 * Deliberately smaller than FMinimalViewInfo (which also carries post process settings).
 * Lens / post process data will be added as separate optional layers in Phase 5 so that the
 * per-frame copies through the director stay cheap.
 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraPose
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Horizontal field of view in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (UIMin = "5.0", UIMax = "170", ClampMin = "0.001", ClampMax = "360.0", Units = deg))
	float FieldOfView = 90.f;

	/** Interpolates A -> B. Rotation uses quaternion slerp so blends take the shortest path. */
	static FACFCameraPose Blend(const FACFCameraPose& A, const FACFCameraPose& B, float Alpha);
};

/** Shape of a blend between two camera sources. */
UENUM(BlueprintType)
enum class EACFCameraBlendCurve : uint8
{
	/** Switch immediately (duration is ignored). */
	Cut,
	Linear,
	EaseIn,
	EaseOut,
	EaseInOut,
	/** Use CustomCurve (0..1 time -> 0..1 alpha). Falls back to Linear if the curve is not set. */
	Custom,
};

/** What the outgoing side of a blend shows while the blend runs. */
UENUM(BlueprintType)
enum class EACFCameraBlendSource : uint8
{
	/** The outgoing camera keeps being evaluated (its targets, damping and state keep updating). */
	Live,
	/** The outgoing camera is frozen at the pose it had when the blend started. */
	Snapshot,
};

USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraBlendSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend")
	EACFCameraBlendCurve Curve = EACFCameraBlendCurve::EaseInOut;

	/**
	 * Live keeps evaluating the outgoing camera. A blend that interrupts another blend always starts
	 * from a snapshot of the pose currently on screen, whatever this setting says (no pop).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (EditCondition = "Curve != EACFCameraBlendCurve::Cut"))
	EACFCameraBlendSource Source = EACFCameraBlendSource::Live;

	/** Seconds. 0 behaves like a cut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (ClampMin = "0.0", Units = s, EditCondition = "Curve != EACFCameraBlendCurve::Cut"))
	float Duration = 0.5f;

	/** Exponent for EaseIn / EaseOut / EaseInOut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (ClampMin = "0.1", EditCondition = "Curve == EACFCameraBlendCurve::EaseIn || Curve == EACFCameraBlendCurve::EaseOut || Curve == EACFCameraBlendCurve::EaseInOut"))
	float Exponent = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (EditCondition = "Curve == EACFCameraBlendCurve::Custom"))
	TObjectPtr<UCurveFloat> CustomCurve = nullptr;

	/** A non-finite duration (NaN / Inf) is a cut too: such a blend would never end (Phase 6B / B-4). */
	bool IsCut() const { return Curve == EACFCameraBlendCurve::Cut || !(Duration > 0.f) || !FMath::IsFinite(Duration); }

	/** Maps normalized time [0,1] to blend alpha [0,1]. */
	float EvaluateAlpha(float NormalizedTime) const;

	static FACFCameraBlendSettings MakeCut();
};

/**
 * Opaque identifier of one camera request. Returned by RequestCamera and used by the requester
 * to release only its own request. Ids are unique for the lifetime of the process, so a stale
 * handle can never release somebody else's newer request.
 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraRequestHandle
{
	GENERATED_BODY()

	FACFCameraRequestHandle() = default;
	explicit FACFCameraRequestHandle(int32 InId) : Id(InId) {}

	bool IsValid() const { return Id != INDEX_NONE; }
	void Invalidate() { Id = INDEX_NONE; }
	int32 GetId() const { return Id; }

	bool operator==(const FACFCameraRequestHandle& Other) const { return Id == Other.Id; }
	bool operator!=(const FACFCameraRequestHandle& Other) const { return Id != Other.Id; }
	friend uint32 GetTypeHash(const FACFCameraRequestHandle& Handle) { return ::GetTypeHash(Handle.Id); }

	FString ToString() const { return IsValid() ? FString::Printf(TEXT("ACFRequest#%d"), Id) : TEXT("ACFRequest#None"); }

private:
	UPROPERTY()
	int32 Id = INDEX_NONE;
};

/**
 * A runtime camera target (follow / look-at), given per request because shared presets cannot
 * reference level actors. Weak references: a destroyed target never keeps an actor alive and
 * never crashes; nodes decide what to do when it is lost (EACFCameraTargetLostBehavior).
 *
 * Resolution order: Component (with optional socket), then Actor (socket on its root component).
 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	TWeakObjectPtr<AActor> Actor;

	/** Optional; takes precedence over Actor (e.g. a specific mesh or a head socket owner). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	TWeakObjectPtr<USceneComponent> Component;

	/** Optional socket / bone on Component (or on Actor's root component). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	FName SocketName;

	/** True if a target was assigned at all (even if it has since been destroyed). */
	bool IsSet() const { return !Actor.IsExplicitlyNull() || !Component.IsExplicitlyNull(); }

	/** True while the assigned actor / component exists (whatever its transform). Use this for "target lost" decisions. */
	bool IsAlive() const { return Component.IsValid() || Actor.IsValid(); }

	/**
	 * World transform of the live target. False if unset or destroyed, and (Phase 6B / B-4) also for this frame when the
	 * transform is not finite or its rotation cannot be normalized: the camera then treats the target as not resolved
	 * (Hold Last Known keeps the last valid transform) instead of storing NaN in its damping state. Reported as a health issue.
	 * OutObject identifies the target in both cases.
	 */
	bool Resolve(FTransform& OutTransform, const UObject*& OutObject) const;
};

/** One member of a camera target group: a target plus how much it matters for framing. */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraTargetGroupMember
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Group")
	FACFCameraTarget Target;

	/** World-space offset from the resolved target (e.g. chest height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Group")
	FVector Offset = FVector::ZeroVector;

	/**
	 * Importance. Pulls the group centre towards this member and scales how strongly it must stay on
	 * screen (relative to the heaviest member). 0 = ignored (kept in the group, e.g. to fade it in later).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Group", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float Weight = 1.f;

	/** Radius (cm) of the sphere around the member that must stay inside the framing area. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Group", meta = (ClampMin = "0.0", Units = cm))
	float Radius = 60.f;

	/** True if both refer to the same actor / component / socket. */
	bool RefersTo(const FACFCameraTarget& Other) const
	{
		return Target.Actor == Other.Actor && Target.Component == Other.Component && Target.SocketName == Other.SocketName;
	}
};

/**
 * Several actors / components framed together (party, boss + adds, player + lock-on target).
 * Lives in the request parameters, so it is per request and per local player; destroyed members are
 * removed automatically by the director.
 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraTargetGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Group")
	TArray<FACFCameraTargetGroupMember> Members;

	/** Adds the member, or updates Weight / Radius / Offset if the same target is already in the group. */
	void AddOrUpdate(const FACFCameraTargetGroupMember& Member);

	/** Removes the member referring to Target. False if it was not in the group. */
	bool Remove(const FACFCameraTarget& Target);

	/** Drops members whose target has been destroyed. Returns how many were removed (no allocation). */
	int32 RemoveLostMembers();
};

/** Per-request parameters supplied by the requester (not stored in the shared preset). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraRequestParams
{
	GENERATED_BODY()

	/** Higher wins. Equal priority: the most recent request wins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request")
	int32 Priority = 0;

	/**
	 * Optional owner. If set, the request is removed automatically (with its blend-out) once
	 * this object is destroyed or garbage collected. Leave empty for requests that are released
	 * explicitly through their handle only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request")
	TWeakObjectPtr<UObject> Instigator;

	/** If true, BlendIn overrides the preset's DefaultBlendIn for this request. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request", meta = (InlineEditConditionToggle))
	bool bOverrideBlendIn = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request", meta = (EditCondition = "bOverrideBlendIn"))
	FACFCameraBlendSettings BlendIn;

	/** Used by Follow nodes. Can be changed later with UACFCameraSubsystem::SetCameraTargets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request")
	FACFCameraTarget FollowTarget;

	/** Used by LookAt nodes; independent of FollowTarget. The Lock-On node locks onto this target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request")
	FACFCameraTarget LookAtTarget;

	/**
	 * If true, the request is released automatically (with its blend-out) once LookAtTarget has been
	 * assigned and then destroyed. Typical for a lock-on camera: the enemy dies, the camera returns.
	 * Unlike Instigator it follows SetCameraTargets / SetCameraLockOnTarget (switching targets is safe).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request")
	bool bReleaseWhenLookAtTargetLost = false;

	/** Per-request switch for the Collision node's Deoccluder (the preset decides whether it exists). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request")
	bool bEnableDeoccluder = true;

	/** Targets framed together by Group Framing / aimed at by Look At or Rotation Composer (Target Source = Target Group). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request")
	FACFCameraTargetGroup TargetGroup;

	/**
	 * Extra actors the Collision node ignores (the follow / look-at target actors are ignored
	 * automatically when the node says so). Destroyed actors are skipped.
	 * Phase 6B / B-3: this is the input (Blueprint / C++). ACF does not own these actors: when the director takes the
	 * request, it moves them into IgnoredActorRefs (weak references) and keeps no strong reference, so the list never
	 * keeps an actor or its World alive, whatever the GC settings (Blueprint does not support arrays of weak references).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request")
	TArray<TObjectPtr<AActor>> CollisionIgnoredActors;

	/** Weak copy of CollisionIgnoredActors held by the director (B-3). Filled by MoveIgnoredActorsToWeak; not for Blueprint. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> IgnoredActorRefs;

	/** Moves CollisionIgnoredActors into IgnoredActorRefs (called by whoever keeps the parameters beyond the call: the director). */
	void MoveIgnoredActorsToWeak()
	{
		for (const TObjectPtr<AActor>& Actor : CollisionIgnoredActors)
		{
			if (Actor)
			{
				IgnoredActorRefs.AddUnique(Actor.Get());
			}
		}
		CollisionIgnoredActors.Empty();
	}

	/** Shown in logs and debug output. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request")
	FName DebugName;
};

/** How a Combat Camera node changes the camera while in combat (deltas from the incoming camera). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraCombatProfile
{
	GENERATED_BODY()

	/** Player-enemy distance where the Near / Far values apply (linear in between). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0.0", Units = cm))
	float NearRange = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0.0", Units = cm))
	float FarRange = 1500.f;

	/** Arm length multiplier (distance pivot -> camera) for a close / far enemy. Without an enemy: Far. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float NearDistanceScale = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float FarDistanceScale = 1.05f;

	/** Degrees added to the field of view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "-60.0", ClampMax = "60.0", Units = deg))
	float FieldOfViewOffset = 5.f;

	/** Added over-the-shoulder offset in camera space (Y = right, Z = up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FVector ShoulderOffset = FVector(0.f, 25.f, 10.f);
};

/**
 * Combat situation as decided by the GAME (ACF never detects combat). Set it per local player with
 * UACFCameraSubsystem::SetCombatState; every Combat Camera node of that player's cameras reacts to it.
 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraCombatState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bInCombat = false;

	/** 0..1: how strongly the combat profile applies (e.g. number of enemies, danger). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Intensity = 1.f;

	/** Optional enemy used for the distance rule; else the request's Look At target, else the nearest Target Group member. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FACFCameraTarget FocusTarget;

	/** Use Profile instead of the node's own profile (runtime tuning from gameplay). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (InlineEditConditionToggle))
	bool bOverrideProfile = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (EditCondition = "bOverrideProfile"))
	FACFCameraCombatProfile Profile;
};

/**
 * Everything gathered before evaluating cameras for one frame (stage 1 of the pipeline).
 * Stack-only; never stored, so raw pointers are fine here.
 */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraEvaluationContext
{
	float DeltaTime = 0.f;

	/** GFrameCounter of the evaluation; used to evaluate at most once per engine frame. */
	uint64 FrameNumber = 0;

	/** May be null (e.g. in unit tests). */
	APlayerController* PlayerController = nullptr;

	/** World of the player (collision queries, debug drawing). Null in unit tests. */
	UWorld* World = nullptr;

	/** The pose UE's own camera pipeline produced before ACF (view target + earlier modifiers). */
	FACFCameraPose UpstreamPose;

	/** PlayerController control rotation, if there is a controller. */
	FRotator ControlRotation = FRotator::ZeroRotator;
	bool bHasControlRotation = false;

	/** Width / height of the view, used by screen-space composers. */
	float AspectRatio = 16.f / 9.f;

	/**
	 * Camera input (X = yaw, Y = pitch, in "input units") consumed by this evaluation: the deltas
	 * accumulated since the previous time-advancing evaluation plus rate input * DeltaTime.
	 * Filled by the director; zero on zero-delta evaluations (input is kept for the next one).
	 */
	FVector2D CameraInput = FVector2D::ZeroVector;

	/** Combat state of this local player (filled by the director from SetCombatState). */
	FACFCameraCombatState Combat;

	/**
	 * 出力バイパス中の評価（Phase 5A）。現在の ViewTarget が Camera Modifier を通さない（Sequencer の Camera Cut が
	 * ACameraActor を映している等）ため、この評価結果は画面に出ない。内部状態を最新に保つための裏評価である。
	 */
	bool bOutputBypassed = false;
};

/**
 * How ACF decided which view target a camera modifier pass of the PlayerCameraManager belongs to (Phase 6B / B-2).
 * ACF must know it to keep its camera off external view targets (Sequencer cameras, cutscene cameras).
 */
UENUM(BlueprintType)
enum class EACFViewTargetResolution : uint8
{
	/** No modifier pass yet (or no camera manager). */
	None,
	/** Standard APlayerCameraManager: the POV is ViewTarget.POV / PendingViewTarget.POV itself (exact). */
	POVAddress,
	/** The camera manager said which view target the pass is for (FACFScopedOutputViewTarget) (exact). */
	Explicit,
	/** The POV is a copy, but the camera manager has only one view target (no PendingViewTarget): the pass is for it. */
	SingleViewTarget,
	/** The POV is a copy during a view target blend, and both view targets get the same decision (both external or neither). */
	SameDecision,
	/** The POV is a copy during a blend between an external and a non-external view target: unknown. ACF does not write. */
	Unresolved,
};

/** Result of a checked return blend from a pose (Phase 6B / B-7, Blend ACF Camera From Pose / Current View For Player). */
UENUM(BlueprintType)
enum class EACFCameraReturnBlendResult : uint8
{
	/** The blend from the pose to the ACF camera starts with the next camera update. */
	Started,
	/** Blend Time 0 (or a Cut curve): nothing to blend, the ACF camera is shown directly. Counts as success. */
	Cut,
	/** No player controller / ACF subsystem for the given player. */
	NoPlayer,
	/** The player has no PlayerCameraManager yet (nothing would be evaluated; the blend would start late). */
	NoCameraManager,
	/** No ACF camera request: there is no ACF camera to blend to. */
	NoCameraRequest,
	/** The PlayerCameraManager is blending view targets (SetViewTargetWithBlend / Sequencer ease): ACF does not add a second blend. */
	CameraManagerBlending,
	/** The view target on screen is an external view target (ACF does not output there): switch back to the gameplay view target first. */
	ExternalViewTargetActive,
	/** The pose is not finite / its field of view is not valid (counted in the camera health diagnostics). */
	InvalidPose,
	/** Blend Time is NaN, infinite or negative. */
	InvalidBlendTime,
};

/** Invalid values (NaN / Inf / invalid FOV ...) ACF detected and kept out of its state and output (Phase 6B / B-4). Counts since the director was created. */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraHealthDiagnostics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int32 TotalIssues = 0;

	/** Target transforms that were not finite (treated as not resolved for that frame). */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int32 InvalidTargets = 0;

	/** Delta times that were NaN / Inf / negative (evaluated as zero time). */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int32 InvalidDeltaTimes = 0;

	/** UE camera poses (upstream), control rotations or PCM poses under ACF that were not valid. */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int32 InvalidUpstreamPoses = 0;

	/** Rejected runtime inputs (camera input, orbit rotation, Blend From Pose, blend / suppression times). */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int32 RejectedInputs = 0;

	/** Node outputs that were invalid (the node's invalid components were dropped). */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int32 InvalidNodeOutputs = 0;

	/** Frames whose effect output was invalid (effects dropped for that frame). */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int32 InvalidEffectOutputs = 0;

	/** Final / blended poses that still were invalid (fallback pose used). */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int32 InvalidFinalPoses = 0;

	/** Description of the last issue: what, node, request, parameter, fallback, frame. */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	FString LastIssue;

	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int64 LastIssueFrame = 0;
};

/** Compatibility diagnostics of ACF's output on the current PlayerCameraManager (Phase 6B / B-2). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraOutputDiagnostics
{
	GENERATED_BODY()

	/** Class of the camera manager the diagnostics are for. */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	FName CameraManagerClass;

	/** Resolution of the last output modifier pass. */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	EACFViewTargetResolution LastResolution = EACFViewTargetResolution::None;

	/** Engine frame of the last camera update that ran the output modifier. */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int64 Frame = 0;

	/** Output modifier passes in that frame (standard PCM: 1, or 2 during a view target blend). */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int32 Passes = 0;

	/** Passes of that frame into which ACF wrote its camera. */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int32 Writes = 0;

	/** True once the camera manager has passed a copy of the POV (not ViewTarget.POV / PendingViewTarget.POV). */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	bool bPOVCopied = false;

	/** Passes ACF skipped because the view target was unknown (since this camera manager was created). */
	UPROPERTY(BlueprintReadOnly, Category = "Action Camera")
	int32 UnresolvedPasses = 0;
};

/** Final result of the director for one frame (stage 6 of the pipeline). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraEvaluationResult
{
	GENERATED_BODY()

	/** Camera pose produced by ACF (already including request-to-request blends). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	FACFCameraPose Pose;

	/**
	 * How much ACF overrides the upstream UE camera. 0 = UE camera untouched, 1 = fully ACF.
	 * Applied by the output adapter against each upstream POV it receives.
	 * Already includes output suppression (e.g. while Sequencer owns the camera).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	float Weight = 0.f;

	/** Weight from requests / blends alone, before output suppression. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	float RequestWeight = 0.f;

	/** True while some source suppresses ACF output (it still evaluates so it can resume cleanly). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	bool bOutputSuppressed = false;

	/**
	 * 現在の ViewTarget が Camera Modifier を通さないため、ACF の結果が画面に出ていない（Sequencer の Camera Cut など）。
	 * Weight は変えない（PCM の ViewTarget Blend と二重にフェードしないため）。評価は裏で続いている。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	bool bOutputBypassed = false;

	/**
	 * Gameplay camera: upstream UE pose combined with Pose by Weight, BEFORE camera shakes and any
	 * future ACF visual effects. Use this for aiming / interaction traces. The visual camera (what is
	 * rendered, including shakes) is the PlayerCameraManager's camera cache.
	 * During a PlayerCameraManager view-target blend it is based on the outgoing view target's pose.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	FACFCameraPose AimPose;

	/** The UE camera pose ACF received this frame (first PCM call). Debugging / comparisons. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	FACFCameraPose UpstreamPose;

	/** Request currently in control (invalid when blending back to / idling on the UE camera). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	FACFCameraRequestHandle ActiveRequest;

	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	bool bIsBlending = false;

	/** Progress of the current transition, 1 when not blending. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	float BlendAlpha = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	int32 NumRequests = 0;

	/** Constraint solver: targets the deoccluder could not reveal (hidden behind geometry). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Constraints")
	int32 NumOccludedTargets = 0;

	/** The player itself stays hidden even after the deoccluder's correction (unsolvable). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Constraints")
	bool bPlayerOccluded = false;

	/** Group Framing needed more FOV than the recovery allows: some targets may be partly off screen. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Constraints")
	bool bFramingLimited = false;

	/** Engine frame this result was produced for. */
	uint64 FrameNumber = 0;

	bool HasInfluence() const { return Weight > 0.f; }
};
