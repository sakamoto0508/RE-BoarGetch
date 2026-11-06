#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACFCameraTypes.h"
#include "ACFCameraRigInstance.h"
#include "ACFCameraNode.h"
#include "Effects/ACFCameraEffectStack.h"
#include "ACFCameraDirector.generated.h"

class UACFCameraPreset;

/** Hard position constraint of the camera in control, kept between frames for the final output guard. */
struct FACFCameraOutputConstraint
{
	bool bValid = false;
	FVector Pivot = FVector::ZeroVector;
	float ProbeRadius = 0.f;
	TEnumAsByte<ECollisionChannel> Channel = ECC_Camera;
	TArray<TWeakObjectPtr<const AActor>> IgnoredActors;
};

/** Internal bookkeeping of one request. */
USTRUCT()
struct FACFCameraRequestEntry
{
	GENERATED_BODY()

	FACFCameraRequestHandle Handle;

	UPROPERTY()
	FACFCameraRequestParams Params;

	UPROPERTY()
	FACFCameraRigInstance Rig;

	/** Monotonic insertion order; breaks priority ties (newest wins). */
	uint64 Sequence = 0;

	/** True if an instigator was supplied, so losing it means "auto release". */
	bool bTracksInstigator = false;
};

/**
 * Decides WHICH camera is in control and HOW control changes (priority + blends).
 *
 * The director owns the camera requests of one local player and produces one
 * FACFCameraEvaluationResult per engine frame. It never talks to the PlayerCameraManager;
 * that is the job of the output adapter (UACFCameraOutputModifier), which keeps the director
 * testable without a world.
 *
 * Transition model (see README.md, "ルール"):
 *  - The highest priority request is active; ties go to the most recent one.
 *  - Switching cameras blends with the new camera's blend-in. The outgoing side is either
 *    evaluated live (its rig keeps updating, even if it was released) or frozen (Snapshot).
 *  - A transition that interrupts another one always starts from a snapshot of the pose
 *    currently produced, so there is never a pop.
 *  - The UE camera is the implicit lowest layer: RequestWeight blends 0 -> 1 when ACF takes over
 *    and 1 -> 0 (released request's blend-out) when the last request goes away.
 *  - Output suppression (e.g. Sequencer owns the camera) fades the final Weight without stopping
 *    evaluation, so ACF resumes from an up-to-date state.
 */
UCLASS(BlueprintType)
class ACTIONCAMERAFRAMEWORK_API UACFCameraDirector : public UObject
{
	GENERATED_BODY()

public:
	/** Returns an invalid handle (and logs) if the preset is null. */
	FACFCameraRequestHandle AddRequest(const UACFCameraPreset* Preset, const FACFCameraRequestParams& Params);

	/**
	 * Removes a request. Unknown / already released handles are ignored (returns false).
	 * @param BlendOutOverride  Optional blend used if this release returns control to the UE camera.
	 */
	bool RemoveRequest(FACFCameraRequestHandle Handle, const FACFCameraBlendSettings* BlendOutOverride = nullptr);

	/** Removes every request. bImmediate cuts straight back to the UE camera. */
	void RemoveAllRequests(bool bImmediate);

	/** Replaces the follow / look-at targets of a live request. Returns false for unknown handles. */
	bool SetRequestTargets(FACFCameraRequestHandle Handle, const FACFCameraTarget& FollowTarget, const FACFCameraTarget& LookAtTarget);

	bool HasRequest(FACFCameraRequestHandle Handle) const;
	int32 GetNumRequests() const { return Requests.Num(); }
	FACFCameraRequestHandle GetActiveRequest() const { return ActiveHandle; }

	/**
	 * Output authority: while at least one source suppresses output, the final Weight fades to 0
	 * (the UE camera, e.g. a Sequencer camera cut, shows unmodified). Keyed by a weak source
	 * object; a destroyed source releases its suppression automatically. Calling twice with the
	 * same source is harmless.
	 */
	void SuppressOutput(const UObject* Source, float BlendTime);
	bool ReleaseOutputSuppression(const UObject* Source, float BlendTime);
	bool IsOutputSuppressed() const { return SuppressionSources.Num() > 0; }

	/**
	 * Engine-agnostic camera input (X = yaw, Y = pitch, input units; nodes apply sensitivity).
	 * AddCameraInput accumulates deltas (mouse); SetCameraInputRate holds a per-second rate (stick)
	 * that is multiplied by DeltaTime. Both are consumed by the next time-advancing evaluation only,
	 * so several PCM calls in one frame never apply input twice. Set the rate every frame.
	 */
	void AddCameraInput(const FVector2D& Delta);
	void SetCameraInputRate(const FVector2D& Rate);

	/** Camera effects (see FACFCameraEffectStack). They are applied to the ACF camera after request blending. */
	FACFCameraEffectHandle PlayEffect(const UACFCameraEffectPreset* Preset, const FACFCameraEffectParams& Params) { return Effects.Play(Preset, Params); }
	bool StopEffect(FACFCameraEffectHandle Handle, bool bImmediate) { return Effects.Stop(Handle, bImmediate); }
	void StopAllEffects(bool bImmediate) { Effects.StopAll(bImmediate); }
	bool IsEffectActive(FACFCameraEffectHandle Handle) const { return Effects.IsActive(Handle); }
	int32 GetNumActiveEffects() const { return Effects.Num(); }
	void GetActiveEffects(TArray<FACFCameraEffectInfo>& Out) const { Effects.GetInfos(Out); }

	/** Drops interpolation history of every rig (next frame snaps). bCutBlend also ends a running blend. */
	void NotifyTeleport(bool bCutBlend);

	/** Sets the orbit rotation (world) of the request's Orbital Follow nodes. False if none. */
	bool SetOrbitRotation(FACFCameraRequestHandle Handle, const FRotator& Rotation, bool bInstant);

	/** True while ACF has any influence on the output (requests exist or a fade is running). */
	bool IsInfluencingOutput() const { return Requests.Num() > 0 || LastResult.RequestWeight > 0.f; }

	/**
	 * Runs stages 2-6 of the pipeline (time advances here only). Evaluates at most once per
	 * Context.FrameNumber: further calls in the same frame return the cached result, so the PCM
	 * evaluating both its current and pending view target never double-steps damping or blends.
	 * Exception: if the cached evaluation had DeltaTime == 0 (e.g. APlayerCameraManager::InitializeFor)
	 * and a later call in the same frame has real time, it is re-evaluated; nodes never advance on a
	 * zero delta, so time still advances exactly once.
	 * Combining the result with the UE camera is the output adapter's job (per call).
	 */
	const FACFCameraEvaluationResult& Evaluate(const FACFCameraEvaluationContext& InContext);

	const FACFCameraEvaluationResult& GetLastResult() const { return LastResult; }

	/** Collision constraint of the camera in control (invalid if it has no Collision node). */
	const FACFCameraOutputConstraint& GetOutputConstraint() const { return OutputConstraint; }

	/** Number of rig evaluations performed by the last Evaluate (tests / debugging). */
	int32 GetLastRigEvaluationCount() const { return LastRigEvaluationCount; }

#if !UE_BUILD_SHIPPING
	/** Debug data published by the nodes of the camera in control during the last evaluation. */
	const FACFCameraRigDebug& GetLastDebug() const { return LastDebug; }
#endif

private:
	struct FTransition
	{
		FACFCameraPose FromPose;
		/** Live source (preempted request or retired request); invalid = use FromPose. */
		FACFCameraRequestHandle LiveSource;
		float FromWeight = 0.f;
		float ToWeight = 0.f;
		float Elapsed = 0.f;
		bool bBlendPose = false;
		bool bActive = false;
	};

	struct FFade
	{
		float From = 1.f;
		float To = 1.f;
		float Duration = 0.f;
		float Elapsed = 0.f;
		float Value = 1.f;
	};

	void PruneLostInstigators();
	void PruneLostSuppressionSources();
	int32 FindBestRequestIndex() const;
	int32 FindRequestIndex(FACFCameraRequestHandle Handle) const;
	FACFCameraRequestEntry* FindLiveEntry(FACFCameraRequestHandle Handle);
	void RetireActiveEntry(int32 Index, const FACFCameraBlendSettings* BlendOutOverride);
	void BeginTransition(int32 NewActiveIndex, const FACFCameraEvaluationContext& Context);
	float AdvanceTransition(float DeltaTime);
	void EndTransition();
	FACFCameraPose EvaluateEntry(FACFCameraRequestEntry& Entry, const FACFCameraEvaluationContext& Context, FACFCameraRigScratch& OutScratch);
	void StartSuppressionFade(float BlendTime);
	float AdvanceSuppressionFade(float DeltaTime);

	UPROPERTY()
	TArray<FACFCameraRequestEntry> Requests;

	/**
	 * Released requests kept alive only while they are the live source of a running blend
	 * (normally zero or one entry). Their rig state keeps updating until the blend ends.
	 */
	UPROPERTY()
	TArray<FACFCameraRequestEntry> RetiredRequests;

	UPROPERTY()
	FACFCameraEffectStack Effects;

	/** Blended pose of the last evaluation before effects / collision (snapshot source, hold pose). */
	FACFCameraPose LastBlendedPose;

	/** Blend of the transition in progress (UPROPERTY so a custom curve stays referenced). */
	UPROPERTY()
	FACFCameraBlendSettings TransitionBlend;

	/** Blend-out of the most recently removed active request (used if control returns to UE). */
	UPROPERTY()
	FACFCameraBlendSettings PendingBlendOut;

	UPROPERTY()
	FACFCameraEvaluationResult LastResult;

	TArray<TWeakObjectPtr<const UObject>> SuppressionSources;
	float PendingSuppressionBlendTime = 0.f;
	bool bSuppressionDirty = false;

	FTransition Transition;
	FFade SuppressionFade;
	FACFCameraRequestHandle ActiveHandle;
	uint64 NextSequence = 0;
	uint64 LastEvaluatedFrame = MAX_uint64;
	float LastEvaluatedDeltaTime = 0.f;
	int32 LastRigEvaluationCount = 0;
	FACFCameraOutputConstraint OutputConstraint;
	FVector2D PendingInputDelta = FVector2D::ZeroVector;
	FVector2D InputRate = FVector2D::ZeroVector;
#if !UE_BUILD_SHIPPING
	FACFCameraRigDebug LastDebug;
#endif
};
