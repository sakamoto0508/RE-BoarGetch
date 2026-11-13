#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACFCameraTypes.h"
#include "ACFCameraRigInstance.h"
#include "ACFCameraNode.h"
#include "Effects/ACFCameraEffectStack.h"
#include "ACFCameraSequencePlayer.h"
#include "ACFCameraProfiler.h"
#include "ACFCameraHealth.h"
#include "ACFCameraTuning.h"
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
 *  - Output suppression (a game / debug system takes the view) fades the final Weight without stopping
 *    evaluation, so ACF resumes from an up-to-date state. Level Sequencer camera cuts do not suppress: their
 *    cameras are external view targets that the output modifier leaves alone (bOutputBypassed evaluations).
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
	 * @param BlendOutOverride    Optional blend used if this release returns control to the UE camera.
	 * @param bOverrideAnyReturn  Use BlendOutOverride whatever camera takes over (a lower request too, instead of its
	 *                            blend-in). Only when the removed request is the active one. Sequences use it to return
	 *                            from a finisher to the lock-on / gameplay camera with the sequence's Return Blend.
	 */
	bool RemoveRequest(FACFCameraRequestHandle Handle, const FACFCameraBlendSettings* BlendOutOverride = nullptr, bool bOverrideAnyReturn = false);

	/** Removes every request. bImmediate cuts straight back to the UE camera. */
	void RemoveAllRequests(bool bImmediate);

	/** Replaces the follow / look-at targets of a live request. Returns false for unknown handles. */
	bool SetRequestTargets(FACFCameraRequestHandle Handle, const FACFCameraTarget& FollowTarget, const FACFCameraTarget& LookAtTarget);

	/** Replaces only the look-at target (= the Lock-On node's locked target). Nodes glide to the new target. */
	bool SetRequestLookAtTarget(FACFCameraRequestHandle Handle, const FACFCameraTarget& LookAtTarget);

	/** Target group of a live request. AddOrUpdate / Remove / Set return false for unknown handles. */
	bool AddGroupMember(FACFCameraRequestHandle Handle, const FACFCameraTargetGroupMember& Member);
	bool RemoveGroupMember(FACFCameraRequestHandle Handle, const FACFCameraTarget& Target);
	bool SetGroupMembers(FACFCameraRequestHandle Handle, const TArray<FACFCameraTargetGroupMember>& Members);
	bool SetRequestDeoccluderEnabled(FACFCameraRequestHandle Handle, bool bEnabled);
	const FACFCameraTargetGroup* GetGroup(FACFCameraRequestHandle Handle) const;

	bool HasRequest(FACFCameraRequestHandle Handle) const;
	int32 GetNumRequests() const { return Requests.Num(); }
	FACFCameraRequestHandle GetActiveRequest() const { return ActiveHandle; }

	/** Request that would be in control right now (highest priority, newest); the active one changes on the next Evaluate. */
	FACFCameraRequestHandle GetBestRequest() const;

	/**
	 * Evaluates Preset once without time and without touching any live request (fresh rig, DeltaTime 0, Body -> Aim ->
	 * Framing -> PostBlend). Used to test whether a shot fits before it starts. False if Preset is null.
	 * Allocates the temporary rig state: call it on events (shot start), not every frame.
	 */
	bool ProbeCamera(const UACFCameraPreset* Preset, const FACFCameraRequestParams& Params, const FACFCameraEvaluationContext& Context, FACFCameraProbeResult& OutResult) const;

	// --- Camera sequences / finisher cameras (see FACFCameraSequencePlayer) -----------------------------------

	FACFCameraSequenceHandle PlaySequence(const UACFCameraSequence* Sequence, const FACFCameraSequenceParams& Params) { return Sequences.Play(*this, Sequence, Params); }

	/**
	 * Finisher with a single preset: plays a one-shot sequence (transient asset created here). Duration 0 = until
	 * Finish / Cancel (Wait For Event without timeout).
	 */
	FACFCameraSequenceHandle PlayFinisherPreset(const UACFCameraPreset* Preset, AActor* Attacker, AActor* Target, float Duration,
		const FACFCameraBlendSettings& BlendIn, const FACFCameraBlendSettings& ReturnBlend, int32 Priority);

	bool SendSequenceCommand(FACFCameraSequenceHandle Handle, EACFCameraSequenceCommand Command, FName ShotName = NAME_None) { return Sequences.SendCommand(*this, Handle, Command, ShotName); }
	bool JumpToShot(FACFCameraSequenceHandle Handle, int32 ShotIndex) { return Sequences.JumpToShot(Handle, ShotIndex); }
	int32 SendSequenceCommandToActor(const AActor* Actor, EACFCameraSequenceCommand Command, FName ShotName = NAME_None) { return Sequences.SendCommandToActor(*this, Actor, Command, ShotName); }
	bool SetSequencePlayRate(FACFCameraSequenceHandle Handle, float PlayRate) { return Sequences.SetPlayRate(Handle, PlayRate); }
	bool GetSequenceState(FACFCameraSequenceHandle Handle, FACFCameraSequenceState& OutState) const { return Sequences.GetState(*this, Handle, OutState); }
	bool IsSequencePlaying(FACFCameraSequenceHandle Handle) const { return Sequences.IsPlaying(Handle); }
	int32 GetNumSequences() const { return Sequences.Num(); }
	void GetActiveSequences(TArray<FACFCameraSequenceHandle>& Out) const { Sequences.GetHandles(Out); }
	void StopAllSequences(bool bImmediate) { Sequences.StopAll(*this, bImmediate); }
	void ConsumeSequenceEvents(TArray<FACFCameraSequenceEvent>& Out) { Sequences.ConsumeEvents(Out); }
	bool HasSequenceEvents() const { return Sequences.HasEvents(); }

	/**
	 * Output authority: while at least one source suppresses output, the final Weight fades to 0
	 * (the UE camera shows unmodified; Sequencer camera cuts use external view targets instead). Keyed by a weak source
	 * object; a destroyed source releases its suppression automatically. Calling twice with the
	 * same source is harmless.
	 */
	void SuppressOutput(const UObject* Source, float BlendTime);
	bool ReleaseOutputSuppression(const UObject* Source, float BlendTime);
	bool IsOutputSuppressed() const { return SuppressionSources.Num() > 0; }

	/** 現在の抑制要求の数（Source ごとに 1 つ。同じ Source の重複は数えない）。 */
	int32 GetNumOutputSuppressions() const { return SuppressionSources.Num(); }

	/**
	 * 画面を ACF 以外が握っているか（出力抑制中、または直前の評価が出力バイパス）。
	 * Camera Sequence の Interrupt Policy はこれを「割り込み」として扱う。
	 */
	bool IsViewOwnedExternally() const { return IsOutputSuppressed() || LastResult.bOutputBypassed; }

	/**
	 * 外部カメラ（Sequencer の最終姿勢など）から、いま有効な ACF カメラへブレンドして戻る（Phase 5A）。
	 * 次の Evaluate で「FromPose のスナップショット → 有効な要求」の遷移を開始する。要求の優先度・Rig の状態は
	 * 変えない（ゲームプレイカメラを破棄しない）。要求が 1 つも無い、または Blend が Cut なら false。
	 * PCM が ViewTarget Blend で戻している間は呼ばないこと（二重ブレンドになる）。
	 */
	bool BlendFromPose(const FACFCameraPose& FromPose, const FACFCameraBlendSettings& Blend);

	/** すべての要求の Orbital Follow Node の向きを設定する（Sequencer 復帰時に ControlRotation と揃える用途）。 */
	int32 SetAllOrbitRotations(const FRotator& Rotation, bool bInstant);

	/** 最後に評価したフレーム（GFrameCounter）。未評価なら MAX_uint64。 */
	uint64 GetLastEvaluatedFrame() const { return LastEvaluatedFrame; }

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

	/** Combat situation decided by gameplay (read by Combat Camera nodes). Per director = per local player. */
	void SetCombatState(const FACFCameraCombatState& State) { CombatState = State; }
	const FACFCameraCombatState& GetCombatState() const { return CombatState; }

	/** Drops interpolation history of every rig (next frame snaps). bCutBlend also ends a running blend. */
	void NotifyTeleport(bool bCutBlend);

	/** Sets the orbit rotation (world) of the request's Orbital Follow nodes. False if none. */
	bool SetOrbitRotation(FACFCameraRequestHandle Handle, const FRotator& Rotation, bool bInstant);

	// --- Runtime tuning (Phase 7A-1, ACFCameraTuning.h, README 24 章) ------------------------------------------------------
	// Handle = a live request of this player; an invalid handle means the active request. The request evaluates a transient
	// copy of its preset while tuned; the shared asset is never written. Development builds only (else NotAvailable).

	/** Gives the request its own copy of its preset (nodes included) and switches its rig to it, keeping the rig state. */
	EACFCameraTuningResult StartTuning(FACFCameraRequestHandle Handle);
	/** Switches the request back to the shared preset (rig state kept) and drops the copy. */
	EACFCameraTuningResult StopTuning(FACFCameraRequestHandle Handle);
	void StopAllTuning();
	/** Current value (the copy while tuned, else the shared preset) and the asset value. */
	EACFCameraTuningResult GetTuningValue(FACFCameraRequestHandle Handle, EACFCameraTuningParameter Parameter, int32 NodeIndex, FACFCameraTuningValue& Out);
	/** Writes the copy (needs StartTuning). Invalid values are rejected and change nothing. */
	EACFCameraTuningResult SetTuningValue(FACFCameraRequestHandle Handle, EACFCameraTuningParameter Parameter, const FVector& Value, int32 NodeIndex = INDEX_NONE);
	EACFCameraTuningResult ResetTuningValue(FACFCameraRequestHandle Handle, EACFCameraTuningParameter Parameter, int32 NodeIndex = INDEX_NONE);
	EACFCameraTuningResult ResetAllTuningValues(FACFCameraRequestHandle Handle);
	FACFCameraTuningStatus GetTuningStatus(FACFCameraRequestHandle Handle);
	/** The preset the request evaluates right now (its tuning copy while tuned). Null if no such request. */
	const UACFCameraPreset* GetRequestPreset(FACFCameraRequestHandle Handle);
	int32 GetNumTunedRequests() const { return TuningSessions.Num(); }
	/** Phase 7A-2 (tools, read only): the live requests of this player, the nodes of a request, why a parameter cannot be tuned. */
	void GetRequestInfos(TArray<FACFCameraRequestInfo>& Out);
	EACFCameraTuningResult GetRequestNodes(FACFCameraRequestHandle Handle, TArray<FACFCameraTuningNodeInfo>& Out);
	FString GetTuningUnavailableReason(FACFCameraRequestHandle Handle, EACFCameraTuningParameter Parameter);

	/** True while ACF has any influence on the output (requests exist or a fade is running). */
	bool IsInfluencingOutput() const { return Requests.Num() > 0 || LastResult.RequestWeight > 0.f; }

	/**
	 * Phase 6B / B-5: true while an ACF camera is on screen or stays there for a while (a request exists, or the last one
	 * blends out to the UE camera). False once nothing ACF is left to put an effect on, including right after the last
	 * request was released with a cut (LastResult still shows the previous frame).
	 */
	bool CanHostEffects() const
	{
		if (Requests.Num() > 0)
		{
			return true;
		}
		if (LastResult.RequestWeight <= 0.f)
		{
			return false;
		}
		if (ActiveHandle.IsValid())
		{
			// Released since the last evaluation: the blend out starts with the next one.
			return !(bHasPendingReturnBlend ? PendingReturnBlend : PendingBlendOut).IsCut();
		}
		return Transition.bActive; // blending out to the UE camera now
	}

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

	/** What the Collision node's constraint solver did in the last evaluation (empty if it did not run). */
	const FACFCameraSolverReport& GetLastSolverReport() const { return LastSolverReport; }

	/** Number of rig evaluations performed by the last Evaluate (tests / debugging). */
	int32 GetLastRigEvaluationCount() const { return LastRigEvaluationCount; }

#if !UE_BUILD_SHIPPING
	/** Debug data published by the nodes of the camera in control during the last evaluation. */
	const FACFCameraRigDebug& GetLastDebug() const { return LastDebug; }
#endif

#if ACF_WITH_PROFILER
	/**
	 * Profiler of this player (Phase 6B / B-1). Records while ACF.Profile is 1, or while forced on (tests, tools).
	 * Recording never changes the evaluation result.
	 */
	FACFCameraProfiler& GetProfiler() { return Profiler; }
	const FACFCameraProfiler& GetProfiler() const { return Profiler; }
	void SetProfilerForced(bool bForced) { bProfilerForced = bForced; }
#endif

	// --- Health (Phase 6B / B-4) ---------------------------------------------------------------------------------------

	/** Invalid values detected and kept out of this player's camera state and output. */
	FACFCameraHealthDiagnostics GetHealthDiagnostics() const { return Health.GetDiagnostics(); }
	FACFCameraHealth& GetHealth() { return Health; }

	/**
	 * Forgets the last known good pose (the fallback for an invalid final pose). Called when the camera context changes
	 * (the subsystem: output view target changed). The director also forgets it when the World or PlayerController changes.
	 */
	void InvalidateLastGoodPose() { bHasLastGoodPose = false; bHasLastGoodUpstream = false; }

	/** True while a last known good pose exists for the current camera context (tests / debugging). */
	bool HasLastGoodPose() const { return bHasLastGoodPose; }

private:
	/** Valid evaluation context for this frame (delta time, upstream pose, control rotation). */
	void SanitizeContext(FACFCameraEvaluationContext& Context);
	/** Replaces invalid components of Pose with the fallback (last known good of this context, else the UE camera). False if no fallback. */
	bool RepairPose(FACFCameraPose& Pose, const FACFCameraEvaluationContext& Context, const TCHAR* Stage);
	bool IsSameCameraContext(const FACFCameraEvaluationContext& Context) const;

	FACFCameraHealth Health;
	FACFCameraPose LastGoodPose;
	bool bHasLastGoodPose = false;
	FACFCameraPose LastGoodUpstream;
	bool bHasLastGoodUpstream = false;
	FRotator LastGoodControlRotation = FRotator::ZeroRotator;
	bool bHasLastGoodControlRotation = false;
	/** This frame's UE camera was invalid with no fallback: evaluate, but do not output or remember the result. */
	bool bUpstreamUnusable = false;
	/** B-5: World of the last evaluation; effects still running when it changes are stopped. */
	TWeakObjectPtr<const UWorld> EffectsWorld;
	bool bHasEffectsWorld = false;
	TWeakObjectPtr<const UWorld> LastGoodWorld;
	TWeakObjectPtr<const APlayerController> LastGoodController;
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

	/** Instigator lost, or look-at target lost with bReleaseWhenLookAtTargetLost; also drops destroyed group members. */
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
	FACFCameraRequestHandle ResolveTuningHandle(FACFCameraRequestHandle Handle) const { return Handle.IsValid() ? Handle : ActiveHandle; }
	FACFCameraTuningSession* FindTuningSession(FACFCameraRequestHandle Handle);
	/** Once per evaluation while sessions exist: drops sessions of ended requests, ends all of them when the World changes. */
	void UpdateTuningSessions(const UWorld* World);
	float AdvanceSuppressionFade(float DeltaTime);

	UPROPERTY()
	TArray<FACFCameraRequestEntry> Requests;

	/**
	 * Released requests kept alive only while they are the live source of a running blend
	 * (normally zero or one entry). Their rig state keeps updating until the blend ends.
	 */
	UPROPERTY()
	TArray<FACFCameraRequestEntry> RetiredRequests;

	/** Phase 7A-1: requests evaluated on a tuning copy of their preset (owns the copies). */
	UPROPERTY()
	TArray<FACFCameraTuningSession> TuningSessions;

	UPROPERTY()
	FACFCameraEffectStack Effects;

	/** Camera sequences of this player. Ticked inside Evaluate's frame guard, before the active request is chosen. */
	UPROPERTY()
	FACFCameraSequencePlayer Sequences;

	/** Blended pose of the last evaluation before effects / collision (snapshot source, hold pose). */
	FACFCameraPose LastBlendedPose;

	/** Blend of the transition in progress (UPROPERTY so a custom curve stays referenced). */
	UPROPERTY()
	FACFCameraBlendSettings TransitionBlend;

	/** Blend-out of the most recently removed active request (used if control returns to UE). */
	UPROPERTY()
	FACFCameraBlendSettings PendingBlendOut;

	/** RemoveRequest(..., bOverrideAnyReturn): blend of the next transition whatever camera takes over. */
	UPROPERTY()
	FACFCameraBlendSettings PendingReturnBlend;
	bool bHasPendingReturnBlend = false;

	/** BlendFromPose の予約（次の Evaluate で消費）。 */
	UPROPERTY()
	FACFCameraBlendSettings PendingEntryBlend;
	FACFCameraPose PendingEntryPose;
	bool bHasPendingEntryBlend = false;

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
	UPROPERTY()
	FACFCameraCombatState CombatState;
	FACFCameraSolverReport LastSolverReport;
	FVector2D PendingInputDelta = FVector2D::ZeroVector;
	FVector2D InputRate = FVector2D::ZeroVector;
#if !UE_BUILD_SHIPPING
	FACFCameraRigDebug LastDebug;
#endif
#if ACF_WITH_PROFILER
	/** Starts / stops recording to follow the console variable (or the forced state). Called outside any profiler scope. */
	void SyncProfiler();
	FACFCameraProfiler Profiler;
	bool bProfilerForced = false;
#endif
};
