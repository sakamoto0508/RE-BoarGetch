#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "ACFCameraTypes.h"
#include "Effects/ACFCameraEffectTypes.h"
#include "ACFCameraSequence.h"
#include "ACFCameraSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FACFCameraSequenceEventSignature, const FACFCameraSequenceEvent&, Event);

class AActor;
class APlayerCameraManager;
class APlayerController;
class UACFCameraDirector;
class UACFCameraOutputModifier;
class UACFCameraModifierBase;
class UACFCameraPreset;
class UACFCameraEffectPreset;
class UCameraShakeBase;
struct FMinimalViewInfo;

/**
 * Per-local-player entry point of Action Camera Framework.
 *
 * Lifetime follows the ULocalPlayer (survives map travel). Owns the director (camera requests and
 * blend state) and keeps the output modifier attached to whichever PlayerCameraManager currently
 * serves this player. Nothing is attached until the first request, so projects that never use ACF
 * keep an untouched camera pipeline.
 *
 * Blueprint: "Get ACF Camera Subsystem" (UACFCameraBlueprintLibrary) or the engine's
 * Get LocalPlayerSubsystem node.
 */
UCLASS()
class ACTIONCAMERAFRAMEWORK_API UACFCameraSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ ULocalPlayerSubsystem
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

	/**
	 * Pushes a camera request. Returns an invalid handle if Preset is null.
	 * Keep the handle and pass it to ReleaseCamera when done; set Params.Instigator to have the
	 * request released automatically when that object dies.
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera")
	FACFCameraRequestHandle RequestCamera(UACFCameraPreset* Preset, const FACFCameraRequestParams& Params);

	/** Releases a request made by RequestCamera and invalidates the handle. Safe to call twice. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera")
	bool ReleaseCamera(UPARAM(ref) FACFCameraRequestHandle& Handle);

	/** Same as ReleaseCamera but with an explicit blend if control returns to the UE camera. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera")
	bool ReleaseCameraWithBlend(UPARAM(ref) FACFCameraRequestHandle& Handle, const FACFCameraBlendSettings& BlendOut);

	/** Drops every request (e.g. on level reset). bImmediate cuts back to the UE camera. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera")
	void ReleaseAllCameras(bool bImmediate = false);

	/** True while the request exists (it may be inactive because of a higher priority request). */
	UFUNCTION(BlueprintPure, Category = "Action Camera")
	bool IsCameraRequestAlive(FACFCameraRequestHandle Handle) const;

	/** True if this request is the one currently in control. */
	UFUNCTION(BlueprintPure, Category = "Action Camera")
	bool IsActiveCamera(FACFCameraRequestHandle Handle) const;

	/** Result of the most recent evaluation (for UI / debugging). */
	UFUNCTION(BlueprintPure, Category = "Action Camera")
	FACFCameraEvaluationResult GetLastEvaluationResult() const;

	/** Changes the follow / look-at targets of an existing request (e.g. switching lock-on). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera")
	bool SetCameraTargets(FACFCameraRequestHandle Handle, const FACFCameraTarget& FollowTarget, const FACFCameraTarget& LookAtTarget);

	// --- Lock-On / Target Group (camera side only: choosing and switching targets is gameplay) --------------

	/**
	 * Changes the locked target of a lock-on request (its Look At Target); the Follow Target is kept.
	 * The Lock-On node turns smoothly to the new target. An empty target means "nothing locked" (behind the player).
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Lock-On", meta = (DisplayName = "Set ACF Camera Lock-On Target"))
	bool SetCameraLockOnTarget(FACFCameraRequestHandle Handle, const FACFCameraTarget& LockTarget);

	/** Same for an actor (null clears the lock). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Lock-On", meta = (DisplayName = "Set ACF Camera Lock-On Actor"))
	bool SetCameraLockOnActor(FACFCameraRequestHandle Handle, AActor* LockTarget);

	/** Adds a member to the request's target group, or updates its Weight / Radius / Offset if already present. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Target Group", meta = (DisplayName = "Add ACF Camera Group Member"))
	bool AddCameraGroupMember(FACFCameraRequestHandle Handle, const FACFCameraTargetGroupMember& Member);

	/** Adds an actor to the request's target group (or updates it). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Target Group", meta = (DisplayName = "Add ACF Camera Group Actor"))
	bool AddCameraGroupActor(FACFCameraRequestHandle Handle, AActor* Actor, float Weight = 1.f, float Radius = 60.f);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Target Group", meta = (DisplayName = "Remove ACF Camera Group Member"))
	bool RemoveCameraGroupMember(FACFCameraRequestHandle Handle, const FACFCameraTarget& Target);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Target Group", meta = (DisplayName = "Remove ACF Camera Group Actor"))
	bool RemoveCameraGroupActor(FACFCameraRequestHandle Handle, AActor* Actor);

	/** Replaces all members (an empty array clears the group). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Target Group", meta = (DisplayName = "Set ACF Camera Group Members"))
	bool SetCameraGroupMembers(FACFCameraRequestHandle Handle, const TArray<FACFCameraTargetGroupMember>& Members);

	/** Current members (destroyed ones have already been removed). Empty for unknown handles. */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Target Group", meta = (DisplayName = "Get ACF Camera Group Members"))
	TArray<FACFCameraTargetGroupMember> GetCameraGroupMembers(FACFCameraRequestHandle Handle) const;

	// --- Combat camera / constraints (the GAME decides the combat state; ACF only reacts to it) --------------

	/** Combat state for every Combat Camera node of this player's cameras. Changes are blended by the nodes. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Combat", meta = (DisplayName = "Set ACF Camera Combat State"))
	void SetCombatState(const FACFCameraCombatState& State);

	/** Convenience: in combat with Intensity, optional enemy used for the distance rule. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Combat", meta = (DisplayName = "Enter ACF Camera Combat"))
	void EnterCombat(float Intensity = 1.f, AActor* FocusEnemy = nullptr);

	/** Convenience: out of combat (the profile fades out with the nodes' Exit Time). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Combat", meta = (DisplayName = "Exit ACF Camera Combat"))
	void ExitCombat();

	UFUNCTION(BlueprintPure, Category = "Action Camera|Combat", meta = (DisplayName = "Get ACF Camera Combat State"))
	FACFCameraCombatState GetCombatState() const;

	/** Turns the Collision node's Deoccluder on / off for one request (it must be enabled in the preset). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Constraints", meta = (DisplayName = "Set ACF Camera Deoccluder Enabled"))
	bool SetCameraDeoccluderEnabled(FACFCameraRequestHandle Handle, bool bEnabled);

	// --- Finisher / cinematic camera sequences (ACF never plays montages: the GAME drives the playback) ----------

	/**
	 * Plays a camera sequence (shots = ordinary camera requests owned by the playback). The first shot starts on the
	 * next camera update (normally the same frame). Keep the handle to steer / end it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Play ACF Camera Sequence"))
	FACFCameraSequenceHandle PlayCameraSequence(UACFCameraSequence* Sequence, const FACFCameraSequenceParams& Params);

	/** Finisher: plays Sequence with Attacker / Target (other playbacks of this player are replaced). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Start ACF Finisher Camera"))
	FACFCameraSequenceHandle StartFinisherCamera(UACFCameraSequence* Sequence, AActor* Attacker, AActor* Target, float PlayRate = 1.f);

	/**
	 * Finisher with one dedicated preset (requested at Priority). Duration 0 = until Finish / Cancel (anim notify or
	 * gameplay), else it ends on its own after Duration seconds of sequence time.
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Start ACF Finisher Camera (Preset)"))
	FACFCameraSequenceHandle StartFinisherCameraWithPreset(UACFCameraPreset* Preset, AActor* Attacker, AActor* Target, float Duration,
		FACFCameraBlendSettings BlendIn, FACFCameraBlendSettings ReturnBlend, int32 Priority = 1000);

	/** Ends the current shot and starts the next one (applied by the next camera update; repeats in one frame count once). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Advance ACF Camera Shot"))
	bool AdvanceCameraShot(FACFCameraSequenceHandle Handle);

	/** Skips to the named shot (applied by the next camera update). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Jump To ACF Camera Shot"))
	bool JumpToCameraShot(FACFCameraSequenceHandle Handle, FName ShotName);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Jump To ACF Camera Shot (Index)"))
	bool JumpToCameraShotIndex(FACFCameraSequenceHandle Handle, int32 ShotIndex);

	/** Freezes shot time (e.g. while an attack is charged). Counted: call Release once per Hold. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Hold ACF Camera Shot"))
	bool HoldCameraShot(FACFCameraSequenceHandle Handle);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Release ACF Camera Shot"))
	bool ReleaseCameraShot(FACFCameraSequenceHandle Handle);

	/** Normal end: back to the previous camera with the sequence's Return Blend. Invalidates the handle. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Finish ACF Camera Sequence"))
	bool FinishCameraSequence(UPARAM(ref) FACFCameraSequenceHandle& Handle);

	/** Abort: back with the Cancel Blend (default cut), owned effects cut. Invalidates the handle. Safe to call twice. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Cancel ACF Camera Sequence"))
	bool CancelCameraSequence(UPARAM(ref) FACFCameraSequenceHandle& Handle);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Send ACF Camera Sequence Command"))
	bool SendCameraSequenceCommand(FACFCameraSequenceHandle Handle, EACFCameraSequenceCommand Command, FName ShotName);

	/** Sends Command to every playback of this player whose attacker or target is Actor (null = all). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Send ACF Camera Sequence Command To Actor"))
	int32 SendCameraSequenceCommandToActor(AActor* Actor, EACFCameraSequenceCommand Command, FName ShotName);

	/** Sequence time scale (match the montage play rate for Duration-timed shots). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequence", meta = (DisplayName = "Set ACF Camera Sequence Play Rate"))
	bool SetCameraSequencePlayRate(FACFCameraSequenceHandle Handle, float PlayRate);

	UFUNCTION(BlueprintPure, Category = "Action Camera|Sequence", meta = (DisplayName = "Get ACF Camera Sequence State"))
	FACFCameraSequenceState GetCameraSequenceState(FACFCameraSequenceHandle Handle) const;

	UFUNCTION(BlueprintPure, Category = "Action Camera|Sequence", meta = (DisplayName = "Is ACF Camera Sequence Playing"))
	bool IsCameraSequencePlaying(FACFCameraSequenceHandle Handle) const;

	/**
	 * Shot started / ended and playback ended. Broadcast after the director evaluation of the frame (never while it
	 * runs), or right away for Finish / Cancel; handlers may call any ACF function.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Action Camera|Sequence")
	FACFCameraSequenceEventSignature OnCameraSequenceEvent;

	/**
	 * Output authority: while suppressed, ACF keeps evaluating but its output weight fades to 0 so another
	 * system (a debug camera, a game cutscene system, ...) owns the view. Keyed by Source (weak): destroying the
	 * source releases it; several sources stack and the output returns when the last one is released.
	 * Level Sequencer Camera Cuts do NOT use this: the Sequencer module registers the cut cameras as external view
	 * targets instead (AddExternalViewTarget / SetExternalViewTargets), so PCM view target blends stay the only
	 * blend. It suppresses output only for sequences with bSuppressOutputWhilePlaying (README 14 章).
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Authority", meta = (DefaultToSelf = "Source"))
	void SuppressOutput(UObject* Source, float BlendTime = 0.f);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Authority", meta = (DefaultToSelf = "Source"))
	bool ReleaseOutputSuppression(UObject* Source, float BlendTime = 0.f);

	UFUNCTION(BlueprintPure, Category = "Action Camera|Authority")
	bool IsOutputSuppressed() const;

	/** 現在の出力抑制要求の数（Source ごとに 1 つ）。0 になったときにだけ出力が戻る。 */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Authority", meta = (DisplayName = "Get Num ACF Output Suppressions"))
	int32 GetNumOutputSuppressions() const;

	/**
	 * 現在の ViewTarget が Camera Modifier を通さないため、ACF の結果が画面に出ていないか
	 * （Sequencer の Camera Cut が Camera Actor を映している間など）。この間も ACF は裏で評価を続ける。
	 */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Authority", meta = (DisplayName = "Is ACF Output Bypassed"))
	bool IsOutputBypassed() const;

	/**
	 * 外部カメラの姿勢（Sequencer の最終フレームなど）から、いま有効な ACF カメラへ Blend で戻る。
	 * 要求や Rig の状態は変えない。要求が無い / Blend が Cut のときは何もせず false。
	 * PCM が ViewTarget Blend で戻している最中には呼ばないこと（二重ブレンドになる）。
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Authority", meta = (DisplayName = "Blend ACF Camera From Pose"))
	bool BlendFromPose(const FACFCameraPose& FromPose, const FACFCameraBlendSettings& Blend);

	/**
	 * Phase 6B / B-7: BlendFromPose with its conditions checked and the reason reported (Blueprint: Blend ACF Camera From Pose
	 * For Player). Delegates to the same director return blend; nothing new is interpolated or stored. Refuses (does nothing)
	 * when the PCM is blending view targets (the rule the Sequencer integration uses: never two blends at once), when an
	 * external view target is on screen, without a PCM or an ACF camera request, and for an invalid pose / blend time.
	 * Calling it again while a return blend runs restarts it from the new pose.
	 */
	EACFCameraReturnBlendResult TryBlendFromPose(const FACFCameraPose& FromPose, float BlendTime, EACFCameraBlendCurve Curve = EACFCameraBlendCurve::EaseInOut);

	/** TryBlendFromPose from what was rendered last (the PCM camera cache): call it in the frame you cut back to the gameplay view target. */
	EACFCameraReturnBlendResult TryBlendFromCurrentView(float BlendTime, EACFCameraBlendCurve Curve = EACFCameraBlendCurve::EaseInOut);

	/**
	 * Phase 6B / B-7: releases a request; if it is the camera on screen, ReturnBlend is used whichever camera takes over (a
	 * lower request or the UE camera). Release Camera With Blend only applies its blend when returning to the UE camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera", meta = (DisplayName = "Release ACF Camera With Return Blend"))
	bool ReleaseCameraWithReturnBlend(UPARAM(ref) FACFCameraRequestHandle& Handle, const FACFCameraBlendSettings& ReturnBlend);

	/** すべての要求の Orbital Follow の向きを設定する（Input Source = Camera Input の Preset を外部カメラの向きに揃える）。 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera", meta = (DisplayName = "Set All ACF Orbit Rotations"))
	int32 SetAllOrbitRotations(FRotator Rotation, bool bInstant = true);

	/**
	 * Director の評価の直前に、ローカルプレイヤー（Subsystem）ごとに 1 フレーム 1 回呼ばれる（Modifier 経由でも裏評価でも）。
	 * 外部の所有権管理（ActionCameraFrameworkSequencer モジュール）が、カメラ更新と同じフレームで状態を確定するために使う。
	 * ハンドラの中で SuppressOutput / BlendFromPose などを呼んでよい。
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPreEvaluate, UACFCameraSubsystem& /*Subsystem*/, float /*DeltaTime*/);
	static FOnPreEvaluate OnPreEvaluate;

	// --- 外部 ViewTarget（ACF を乗せないカメラ）-----------------------------------------------------------------
	//
	// UE 5.8 の PCM は、ViewTarget が Camera Actor でも Camera Modifier を適用する。そのままだと Sequencer や
	// ゲーム独自のカットシーンカメラの上に ACF が乗ってしまうので、「この Actor が ViewTarget の間は ACF を出力しない」
	// という登録を持つ。判定は ViewTarget 単位: PCM の ViewTarget Blend 中は、ゲームプレイ側の ViewTarget には
	// ACF が 100%、外部 ViewTarget には 0% で乗り、両者を混ぜるのは PCM のブレンドだけになる（ACF の Weight は動かない
	// = 二重ブレンドにならない）。外部 ViewTarget だけが映っている間、ACF は裏で評価を続ける（Is ACF Output Bypassed）。

	/** ViewTarget を「ACF を乗せないカメラ」として登録する。Source（登録者）か ViewTarget が破棄されると自動で無効になる。 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Authority", meta = (DisplayName = "Add ACF External View Target", DefaultToSelf = "Source"))
	void AddExternalViewTarget(AActor* ViewTarget, UObject* Source);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Authority", meta = (DisplayName = "Remove ACF External View Target", DefaultToSelf = "Source"))
	void RemoveExternalViewTarget(AActor* ViewTarget, UObject* Source);

	UFUNCTION(BlueprintPure, Category = "Action Camera|Authority", meta = (DisplayName = "Is ACF External View Target"))
	bool IsExternalViewTarget(const AActor* ViewTarget) const;

	/** Source が登録している外部 ViewTarget を Targets で置き換える（空でクリア）。 */
	void SetExternalViewTargets(const UObject* Source, TConstArrayView<AActor*> Targets);

	/**
	 * ViewTarget が分かっている呼び出し元用。OnPreEvaluate を（1 フレーム 1 回）発行してから、この ViewTarget に ACF を
	 * 出力してよいかを返す（外部 ViewTarget なら false）。null は「外部ではない」として扱う。
	 * Output Modifier は BeginOutputForModifierPass を使う（Phase 6B / B-2）。
	 */
	bool BeginOutputForViewTarget(AActor* ViewTarget, float DeltaTime);

	// --- PlayerCameraManager の互換性（Phase 6B / B-2）-------------------------------------------------------------
	//
	// ACF は Camera Modifier として PCM に乗るので、Modifier の呼び出しが「どの ViewTarget の POV か」を知る必要がある
	// （外部 ViewTarget に書かないため）。判定の順序:
	//   1. PCM が FACFScopedOutputViewTarget で明示した ViewTarget（Explicit）
	//   2. POV が ViewTarget.POV / PendingViewTarget.POV そのもの（標準 PCM。POVAddress）
	//   3. POV がコピーで、PCM の ViewTarget が 1 つだけ（PendingViewTarget なし）: その ViewTarget（SingleViewTarget）
	//   4. POV がコピーで ViewTarget Blend 中: 両方が外部 / 両方が外部でないなら、どちらの呼び出しでも結論は同じ（SameDecision）
	//   5. それ以外（外部とゲームプレイの間のブレンドで POV がコピー）: 判定できない。外部カメラを上書きしないよう
	//      ACF はその呼び出しに書かない（Unresolved）。PCM ごとに 1 回だけ警告を出す。

	/** Output Modifier が呼び出しごとに最初に呼ぶ。OnPreEvaluate を発行し、ViewTarget を判定して、ACF を書いてよいかを返す。 */
	bool BeginOutputForModifierPass(const APlayerCameraManager* CameraManager, const FMinimalViewInfo& POV, float DeltaTime);

	/** Output Modifier が実際に ACF の姿勢を書き込んだときに呼ぶ（診断用）。 */
	void NoteOutputWritten();

	/** Output Modifier: the PCM pose ACF blends with was invalid; ACF wrote its own pose at full weight (B-4, diagnostics). */
	void NoteInvalidOutputUpstream();

	/** Collision Guard Modifier 用: この呼び出しの POV に ACF の出力位置の補正をしてよいか（Begin と同じ判定。状態は変えない）。 */
	bool ShouldGuardModifierPass(const APlayerCameraManager* CameraManager, const FMinimalViewInfo& POV) const;

	/** いまの PCM での互換性の診断（判定方法、最後のフレームの呼び出し・書き込み回数、判定できなかった回数）。 */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Authority", meta = (DisplayName = "Get ACF Camera Output Diagnostics"))
	FACFCameraOutputDiagnostics GetOutputDiagnostics() const { return OutputDiagnostics; }

	/** Invalid values (NaN / Inf / invalid FOV ...) detected and kept out of this player's camera state and output (Phase 6B / B-4). */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Debug", meta = (DisplayName = "Get ACF Camera Health Diagnostics"))
	FACFCameraHealthDiagnostics GetHealthDiagnostics() const;

	/** FACFScopedOutputViewTarget が使う。 */
	void PushExplicitOutputViewTarget(const APlayerCameraManager* CameraManager, AActor* ViewTarget);
	void PopExplicitOutputViewTarget();

	/**
	 * Gameplay camera pose (for aiming / traces): UE camera + ACF after Collision, without UE camera shakes
	 * and ACF rotation / FOV effects (ACF location effects are included; see FACFCameraEvaluationResult::AimPose).
	 * ACF never sends camera data to the server; replicate gameplay aim explicitly.
	 */
	UFUNCTION(BlueprintPure, Category = "Action Camera")
	FACFCameraPose GetAimCameraPose() const;

	/** What is actually rendered (PlayerCameraManager camera cache, includes shakes). */
	UFUNCTION(BlueprintPure, Category = "Action Camera")
	FACFCameraPose GetVisualCameraPose() const;

	/**
	 * Camera input for Orbital Follow (Input Source = Camera Input). Engine-input agnostic: call it from
	 * Enhanced Input, legacy input, AI or UI. X = yaw, Y = pitch, in input units (nodes apply
	 * sensitivity / inversion). Deltas accumulate until the next camera update consumes them once.
	 * Use this for mouse-like deltas.
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Input")
	void AddCameraInput(FVector2D YawPitchDelta);

	/** Per-second input (gamepad stick). Multiplied by the camera DeltaTime; set it every frame. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Input")
	void SetCameraInputRate(FVector2D YawPitchRate);

	/** Sets the orbit rotation (world yaw / pitch) of a request's Orbital Follow nodes. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera")
	bool SetOrbitRotation(FACFCameraRequestHandle Handle, FRotator Rotation, bool bInstant = true);

	/**
	 * Tell ACF the targets were teleported (respawn, level streaming jump): all damping / lookahead
	 * history is dropped so the camera cuts instead of flying. Large jumps are also detected
	 * automatically per node (Target Motion > Teleport Distance).
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera")
	void NotifyTeleport(bool bCutBlend = true);

	// --- Camera effects (Blueprint names carry "ACF" to stay distinct from UE's camera shake nodes) ---------

	/**
	 * Plays an effect preset on this player's ACF camera. Keep the handle to stop this effect only.
	 * Phase 6B / B-5: effects only exist on top of an ACF camera. Without one (Can Play ACF Camera Effects is false: no
	 * camera request and no ACF camera still fading out) the effect is rejected and the returned handle is invalid
	 * (Is Valid (ACF Camera Effect Handle) = false): it is not kept for later, so it can never start seconds afterwards.
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Effects", meta = (DisplayName = "Play ACF Camera Effect", AutoCreateRefTerm = "Params"))
	FACFCameraEffectHandle PlayCameraEffect(UACFCameraEffectPreset* Preset, const FACFCameraEffectParams& Params);

	/** True while effects can play: a camera request exists, or the ACF camera is still on screen (blending out). */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Effects", meta = (DisplayName = "Can Play ACF Camera Effects"))
	bool CanPlayCameraEffects() const;

	/** Effects rejected because no ACF camera was there (B-5), since this subsystem was created. */
	int32 GetNumRejectedCameraEffects() const { return NumRejectedEffects; }

	/** Releases (or cuts) one effect and invalidates the handle. Safe to call twice. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Effects", meta = (DisplayName = "Stop ACF Camera Effect"))
	bool StopCameraEffect(UPARAM(ref) FACFCameraEffectHandle& Handle, bool bImmediate = false);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Effects", meta = (DisplayName = "Stop All ACF Camera Effects"))
	void StopAllCameraEffects(bool bImmediate = false);

	/** Explosion / landing / hit impulse from a world location (Preset: Project Settings default, else built-in). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Effects", meta = (DisplayName = "Play ACF Camera Impulse"))
	FACFCameraEffectHandle PlayCameraImpulse(FVector SourceLocation, float Scale = 1.f, UACFCameraEffectPreset* Preset = nullptr);

	/** Attack recoil (Preset: Project Settings default, else built-in). Shots stack. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Effects", meta = (DisplayName = "Play ACF Camera Recoil"))
	FACFCameraEffectHandle PlayCameraRecoil(float Scale = 1.f, UACFCameraEffectPreset* Preset = nullptr);

	/**
	 * Plays a standard UE camera shake class through ACF (location collided, rotation / FOV after collision).
	 * The shake is NOT also started on the PlayerCameraManager, so it is applied exactly once.
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Effects", meta = (DisplayName = "Play ACF Camera Shake"))
	FACFCameraEffectHandle PlayCameraShake(TSubclassOf<UCameraShakeBase> ShakeClass, float Scale = 1.f);

	UFUNCTION(BlueprintPure, Category = "Action Camera|Effects", meta = (DisplayName = "Get Active ACF Camera Effects"))
	TArray<FACFCameraEffectInfo> GetActiveCameraEffects() const;

	UFUNCTION(BlueprintPure, Category = "Action Camera|Effects", meta = (DisplayName = "Is ACF Camera Effect Active"))
	bool IsCameraEffectActive(FACFCameraEffectHandle Handle) const;

	/** Transient effect preset owned by this subsystem, created once per key (samples, convenience APIs). */
	UACFCameraEffectPreset* GetOrCreateTransientEffectPreset(FName Key, TFunctionRef<void(UACFCameraEffectPreset*)> Build);

	UACFCameraDirector* GetDirector() const { return Director; }

	/**
	 * Called by the output adapter. Collects context (stage 1) and runs the director once per frame.
	 */
	const FACFCameraEvaluationResult& EvaluateForOutput(float DeltaTime, const FMinimalViewInfo& UpstreamPOV);

	/**
	 * Called by the final guard modifier: re-sweeps from the controlling camera's pivot to the final
	 * position (after the Weight blend and camera shakes) and clamps it. True if it moved the camera.
	 */
	bool ApplyOutputCollisionGuard(FVector& InOutLocation);

	/** Distance the guard moved the camera in the given frame (0 if it did not act). Tests / debugging. */
	float GetGuardCorrection(uint64 Frame) const { return Frame == LastGuardFrame ? LastGuardCorrection : 0.f; }

	/** Broadcasts queued sequence notifications (outside the camera update). */
	void FlushSequenceEvents();

private:
	void EnsureOutputAttached();
	TArray<FACFCameraSequenceEvent> SequenceEventScratch;
	bool AttachModifier(APlayerCameraManager* CameraManager, TSubclassOf<UACFCameraModifierBase> ModifierClass, TWeakObjectPtr<UACFCameraModifierBase>& InOutModifier);
	void DetachOutput();
	APlayerCameraManager* GetCurrentCameraManager() const;

	/**
	 * Runs at the start of every world tick (before the camera update of that tick) and re-attaches
	 * the output modifier if the PlayerCameraManager was recreated or the modifier was removed.
	 * Cost while idle: one pointer check; while ACF is in use: a short scan of the PCM modifier list.
	 */
	void HandleWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	/**
	 * 裏評価（Phase 5A）。カメラ更新の直後に呼ばれ、このフレームに Director が 1 度も評価されなかった場合
	 * （映っているのが外部 ViewTarget だけ = Sequencer の Camera Cut など）に Director を評価する。
	 * これにより、外部カメラが映っている間も Damping・ブレンド・Finisher の時間・要求の自動解除が進み、
	 * 復帰した最初のフレームから最新の構図を出せる。
	 */
	void HandleWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	const FACFCameraEvaluationResult& EvaluateInternal(float DeltaTime, const FMinimalViewInfo& UpstreamPOV, bool bOutputBypassed);

	FDelegateHandle WorldPostActorTickHandle;

	void BroadcastPreEvaluate(float DeltaTime);

	/** ACF を出力した最後の ViewTarget（= 戻り先のゲームプレイカメラ）。裏評価の Upstream（UE カメラ）をここから求める。 */
	TWeakObjectPtr<AActor> LastOutputViewTarget;

	/** Modifier の 1 回の呼び出しの判定結果（B-2）。 */
	struct FModifierPassTarget
	{
		AActor* ViewTarget = nullptr;
		EACFViewTargetResolution Resolution = EACFViewTargetResolution::None;
		bool bWrite = true;
		bool bPOVCopied = false;
	};
	FModifierPassTarget ResolveModifierPass(const APlayerCameraManager* CameraManager, const FMinimalViewInfo& POV) const;

	FACFCameraOutputDiagnostics OutputDiagnostics;
	TWeakObjectPtr<const APlayerCameraManager> DiagnosedCameraManager;
	TWeakObjectPtr<const AActor> HealthViewTarget; // B-4: view target of the last known good pose
	int32 NumRejectedEffects = 0; // B-5
	bool bCompatibilityWarned = false;
	struct FExplicitOutputViewTarget
	{
		TWeakObjectPtr<const APlayerCameraManager> CameraManager;
		TWeakObjectPtr<AActor> ViewTarget;
	};
	TArray<FExplicitOutputViewTarget, TInlineAllocator<2>> ExplicitOutputViewTargets;

	struct FExternalViewTarget
	{
		TWeakObjectPtr<const AActor> Target;
		TWeakObjectPtr<const UObject> Source;
	};
	TArray<FExternalViewTarget> ExternalViewTargets;
	uint64 LastPreEvaluateFrame = MAX_uint64;

	UPROPERTY(Transient)
	TObjectPtr<UACFCameraDirector> Director;

	/** Resolved once from settings (avoids a soft-class load on every re-attach check). */
	UPROPERTY(Transient)
	TSubclassOf<UACFCameraOutputModifier> OutputModifierClass;

	FDelegateHandle WorldTickStartHandle;

	/** Lazily created presets for the convenience functions (transient, one per kind / shake class). */
	UACFCameraEffectPreset* GetDefaultEffectPreset(bool bImpulse);
	UPROPERTY(Transient)
	TObjectPtr<UACFCameraEffectPreset> BuiltinImpulsePreset;
	UPROPERTY(Transient)
	TObjectPtr<UACFCameraEffectPreset> BuiltinRecoilPreset;
	UPROPERTY(Transient)
	TMap<TObjectPtr<UClass>, TObjectPtr<UACFCameraEffectPreset>> ShakePresets;
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UACFCameraEffectPreset>> TransientEffectPresets;

#if !UE_BUILD_SHIPPING
	/** Screen-space debug (dead / soft zones, text) via UDebugDrawService; gated by ACF.Debug.Draw. */
	void HandleDebugDraw(class UCanvas* Canvas, APlayerController* PlayerController);
	FDelegateHandle DebugDrawHandle;
	uint64 LastDebugDrawFrame = 0;
#endif

	/** Owned by the PCM's ModifierList; weak so a destroyed PCM simply drops it. */
	TWeakObjectPtr<UACFCameraModifierBase> OutputModifier;
	TWeakObjectPtr<UACFCameraModifierBase> GuardModifier;
	float LastGuardCorrection = 0.f;
	uint64 LastGuardFrame = 0;
};

/**
 * 連携 API（Phase 6B / B-2）: POV のコピーを Camera Modifier に渡す PlayerCameraManager の派生クラス用。
 * ApplyCameraModifiers を呼ぶ間、この呼び出しがどの ViewTarget の POV かを ACF に伝える（ViewTarget Blend 中も正確になる）。
 *
 *   void AMyCameraManager::ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV)
 *   {
 *       FACFScopedOutputViewTarget Scope(this, ViewTargetOfThisPass);
 *       Super::ApplyCameraModifiers(DeltaTime, InOutPOV);
 *   }
 *
 * PCM の LocalPlayer に ACF のサブシステムが無ければ何もしない。GameplayCameras など他のプラグインへの依存は無い。
 */
class ACTIONCAMERAFRAMEWORK_API FACFScopedOutputViewTarget
{
public:
	FACFScopedOutputViewTarget(const APlayerCameraManager* CameraManager, AActor* ViewTarget);
	~FACFScopedOutputViewTarget();

	FACFScopedOutputViewTarget(const FACFScopedOutputViewTarget&) = delete;
	FACFScopedOutputViewTarget& operator=(const FACFScopedOutputViewTarget&) = delete;

private:
	TWeakObjectPtr<UACFCameraSubsystem> Subsystem;
};
