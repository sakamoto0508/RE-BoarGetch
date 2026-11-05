#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "ACFCameraTypes.generated.h"

class AActor;
class APlayerController;
class UCurveFloat;
class USceneComponent;

ACTIONCAMERAFRAMEWORK_API DECLARE_LOG_CATEGORY_EXTERN(LogActionCamera, Log, All);

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

	bool IsCut() const { return Curve == EACFCameraBlendCurve::Cut || Duration <= 0.f; }

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

	/** World transform of the live target. False if unset or destroyed. OutObject identifies it. */
	bool Resolve(FTransform& OutTransform, const UObject*& OutObject) const;
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

	/** Used by LookAt nodes; independent of FollowTarget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request")
	FACFCameraTarget LookAtTarget;

	/** Shown in logs and debug output. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Request")
	FName DebugName;
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

	/** The pose UE's own camera pipeline produced before ACF (view target + earlier modifiers). */
	FACFCameraPose UpstreamPose;
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
	 * Gameplay camera: upstream UE pose combined with Pose by Weight, BEFORE camera shakes and any
	 * future ACF visual effects. Use this for aiming / interaction traces. The visual camera (what is
	 * rendered, including shakes) is the PlayerCameraManager's camera cache.
	 * During a PlayerCameraManager view-target blend it is based on the outgoing view target's pose.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	FACFCameraPose AimPose;

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

	/** Engine frame this result was produced for. */
	uint64 FrameNumber = 0;

	bool HasInfluence() const { return Weight > 0.f; }
};
