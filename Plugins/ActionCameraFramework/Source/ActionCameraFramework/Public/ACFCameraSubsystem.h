#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "ACFCameraTypes.h"
#include "ACFCameraSubsystem.generated.h"

class APlayerCameraManager;
class APlayerController;
class UACFCameraDirector;
class UACFCameraOutputModifier;
class UACFCameraPreset;
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

	/**
	 * Output authority extension point. While suppressed, ACF keeps evaluating but its output weight
	 * fades to 0 so another system (Sequencer camera cuts, a debug camera, ...) owns the view.
	 * Keyed by Source (weak): destroying the source releases it. Phase 5 will call this
	 * automatically for Sequencer; until then call it from the sequence's Play / Stop events.
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Authority", meta = (DefaultToSelf = "Source"))
	void SuppressOutput(UObject* Source, float BlendTime = 0.f);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Authority", meta = (DefaultToSelf = "Source"))
	bool ReleaseOutputSuppression(UObject* Source, float BlendTime = 0.f);

	UFUNCTION(BlueprintPure, Category = "Action Camera|Authority")
	bool IsOutputSuppressed() const;

	/**
	 * Gameplay camera pose (for aiming / traces): UE camera + ACF, without camera shakes or visual
	 * effects. ACF never sends camera data to the server; replicate gameplay aim explicitly.
	 */
	UFUNCTION(BlueprintPure, Category = "Action Camera")
	FACFCameraPose GetAimCameraPose() const;

	/** What is actually rendered (PlayerCameraManager camera cache, includes shakes). */
	UFUNCTION(BlueprintPure, Category = "Action Camera")
	FACFCameraPose GetVisualCameraPose() const;

	UACFCameraDirector* GetDirector() const { return Director; }

	/**
	 * Called by the output adapter. Collects context (stage 1) and runs the director once per frame.
	 */
	const FACFCameraEvaluationResult& EvaluateForOutput(float DeltaTime, const FMinimalViewInfo& UpstreamPOV);

private:
	void EnsureOutputAttached();
	void DetachOutput();
	APlayerCameraManager* GetCurrentCameraManager() const;

	/**
	 * Runs at the start of every world tick (before the camera update of that tick) and re-attaches
	 * the output modifier if the PlayerCameraManager was recreated or the modifier was removed.
	 * Cost while idle: one pointer check; while ACF is in use: a short scan of the PCM modifier list.
	 */
	void HandleWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	UPROPERTY(Transient)
	TObjectPtr<UACFCameraDirector> Director;

	/** Resolved once from settings (avoids a soft-class load on every re-attach check). */
	UPROPERTY(Transient)
	TSubclassOf<UACFCameraOutputModifier> OutputModifierClass;

	FDelegateHandle WorldTickStartHandle;

	/** Owned by the PCM's ModifierList; weak so a destroyed PCM simply drops it. */
	TWeakObjectPtr<UACFCameraOutputModifier> OutputModifier;
};
