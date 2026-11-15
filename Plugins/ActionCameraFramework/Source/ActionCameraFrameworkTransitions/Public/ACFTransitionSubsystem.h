#pragma once

#include "CoreMinimal.h"
#include "ACFTransitionTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "ACFTransitionSubsystem.generated.h"

class APlayerController;
class FACFTransitionView;
class UACFTransitionPreset;
class UTexture;
class UTexture2D;
class UWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FACFOnTransitionEvent, FACFTransitionHandle, Handle, EACFTransitionEvent, Event);
DECLARE_MULTICAST_DELEGATE_TwoParams(FACFOnTransitionEventNative, FACFTransitionHandle /*Handle*/, EACFTransitionEvent /*Event*/);

/**
 * Screen and level transitions (Phase 7D, README 33 章).
 *
 * Owned by the GameInstance, so it lives across map loads. It draws one overlay widget in the game viewport (above the
 * game's UMG; the viewport and its widgets are not destroyed by a map load) and plays one transition at a time:
 *
 *  - PlayClose / PlayOpen: scene -> covered / covered -> scene (screen fades, warps, camera cuts, Game Over, ...).
 *  - PlayLevelTransition: Close -> Covered (the game loads: OpenLevel, streaming, its own stage system) -> the new world
 *    is ready -> Open. The subsystem loads only when a level name is given and bManualLoad is off (then it calls
 *    OpenLevel after Covered, once the Covered listeners' requests are applied). It follows the load through PreLoadMap /
 *    PostLoadMapWithWorld, or, with bManualLoad (the game owns the load; a level name is then not opened), through
 *    NotifyLoadStarted / NotifyLoadCompleted.
 *
 * Every handle ends with exactly one terminal event (Finished, Failed, Cancelled, Interrupted); a handle that sent Started
 * always gets one. Events are sent synchronously (OnTransitionEventNative, then OnTransitionEvent). Re-entrancy (README 33
 * 章「イベントと再入」): a Play* request made while an event is being sent is not applied inside that event; it waits and
 * is applied, in request order and with its own Concurrency, as soon as the operation that sent the event has finished its
 * own state change (before queued transitions start). Cancel / Skip / ClearScreen / Notify* act at once, also from a
 * listener. ClearScreen cancels everything requested before it (also such waiting requests); requests made by the
 * listeners of its Cancelled events start after the screen is cleared. The overlay is removed
 * (material instance and capture released) when the screen is clear; a failed / timed-out level transition opens the
 * screen instead of leaving it covered. No camera request is needed; the camera Core is not used.
 */
UCLASS()
class ACTIONCAMERAFRAMEWORKTRANSITIONS_API UACFTransitionSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UACFTransitionSubsystem();

	/** The subsystem of the world's game instance (nullptr in editor worlds / without a game instance). */
	static UACFTransitionSubsystem* Get(const UObject* WorldContextObject);

	// -- Play

	/** Scene -> covered. With bHoldWhenCovered (default) the screen stays covered after Finished. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition", meta = (AutoCreateRefTerm = "Options"))
	FACFTransitionHandle PlayClose(const FACFTransitionSettings& Settings, const FACFTransitionOptions& Options);

	/** Covered -> scene. When the screen is clear the Open starts from fully covered. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition", meta = (AutoCreateRefTerm = "Options"))
	FACFTransitionHandle PlayOpen(const FACFTransitionSettings& Settings, const FACFTransitionOptions& Options);

	/** Close or Open of a preset. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition", meta = (AutoCreateRefTerm = "Options"))
	FACFTransitionHandle PlayPreset(const UACFTransitionPreset* Preset, EACFTransitionPhase Phase, const FACFTransitionOptions& Options);

	/**
	 * Close -> covered -> (load) -> world ready -> Open. LevelName (optional): OpenLevel(LevelName, TravelOptions) is
	 * called once the screen is covered; otherwise the game loads when it receives Covered (OnTransitionEvent).
	 */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition", meta = (AutoCreateRefTerm = "Options,TravelOptions"))
	FACFTransitionHandle PlayLevelTransition(const FACFTransitionSettings& Close, const FACFTransitionSettings& Open, const FACFTransitionOptions& Options, FName LevelName, const FString& TravelOptions);

	/** PlayLevelTransition with a preset's Close and Open. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition", meta = (AutoCreateRefTerm = "Options,TravelOptions"))
	FACFTransitionHandle PlayLevelTransitionPreset(const UACFTransitionPreset* Preset, const FACFTransitionOptions& Options, FName LevelName, const FString& TravelOptions);

	// -- Control

	/** Ends a playing or queued transition with Cancelled. bClearScreen: remove the overlay at once (otherwise the screen
	 *  keeps what it shows; a covered screen stays covered until an Open or ClearScreen). */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition")
	bool Cancel(FACFTransitionHandle Handle, bool bClearScreen = true);

	/** Jumps the current phase of the transition to its end (a Close becomes covered, an Open finishes). */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition")
	bool Skip(FACFTransitionHandle Handle);

	/** Cancels everything (playing and queued) and removes the overlay: the scene is visible. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition")
	void ClearScreen();

	/** Level transitions with bManualLoad: the game started loading (sends LoadStarted). */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition")
	bool NotifyLoadStarted(FACFTransitionHandle Handle);

	/** Level transitions with bManualLoad: the content is loaded. The Open starts when the world is ready. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition")
	bool NotifyLoadCompleted(FACFTransitionHandle Handle);

	/** Level transitions with bWaitForReadySignal: the new world is set up (any handle of the game instance when
	 *  Handle is invalid, so a level script can call it without knowing the handle). */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition")
	bool NotifyWorldReady(FACFTransitionHandle Handle);

	/** Reports a load failure (the transition opens and ends with Failed). */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition")
	bool NotifyLoadFailed(FACFTransitionHandle Handle);

	/** Shows one effect frozen at a coverage (no handle; for previews, screenshots and GPU measurements). Replaces
	 *  nothing that is playing: returns false while a transition is active. ClearScreen removes it. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition|Debug")
	bool ShowStill(const FACFTransitionSettings& Settings, EACFTransitionPhase Phase, float Coverage);

	// -- Query

	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	EACFTransitionScreenState GetScreenState() const;

	/** 0 = scene visible .. 1 = fully covered (what is drawn now). */
	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	float GetCoverage() const { return Coverage; }

	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	bool IsCovered() const { return Coverage >= 1.f && bOverlayAttached; }

	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	bool IsTransitionActive() const;

	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	FACFTransitionHandle GetActiveHandle() const;

	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	FACFTransitionInfo GetTransitionInfo(FACFTransitionHandle Handle) const;

	/** Queued transitions (Concurrency Queue) waiting for the active one. */
	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	int32 GetQueueLength() const { return Queue.Num(); }

	/** Every event of every handle. */
	UPROPERTY(BlueprintAssignable, Category = "ACF|Transition")
	FACFOnTransitionEvent OnTransitionEvent;

	FACFOnTransitionEventNative OnTransitionEventNative;

	// -- Test / debug access
	bool IsOverlayAttached() const { return bOverlayAttached; }
	bool HasView() const { return View.IsValid(); }
	bool HasCapture() const { return Capture != nullptr; }
	/** A GPU capture is requested and the Close waits for it (README 33 章「キャプチャ」). */
	bool IsCapturePending() const { return PendingCapture.IsValid(); }
	/** Frames the last Close waited for its capture, and whether it fell back to the synchronous read back (or to no capture). */
	int32 GetLastCaptureWaitFrames() const { return LastCaptureWaitFrames; }
	bool DidLastCaptureFallBack() const { return bLastCaptureFellBack; }
	/** -1 = no capture request; otherwise FACFTransitionGpuCapture::EState of the pending request (tests). */
	int32 GetPendingCaptureState() const;
	FACFTransitionView* GetView() const { return View.Get(); }
	/** Play* requests made by event listeners that wait for the current operation (see the class comment). */
	int32 GetWaitingRequestCount() const { return Incoming.Num() + (Starting.IsSet() ? 1 : 0); }
	/** True while an event is being sent (a listener is running). */
	bool IsSendingEvent() const { return DispatchDepth > 0; }
	/** Player controllers whose move / look input this subsystem currently ignores. */
	int32 GetLockedControllerCount() const { return LockedControllers.Num(); }
	/** CPU seconds of the last Tick (cycles of this subsystem only). */
	double GetLastTickSeconds() const { return LastTickSeconds; }
	/** Advances the transition by DeltaSeconds (tests drive time explicitly). */
	void AdvanceForTest(float DeltaSeconds) { TickTransitions(DeltaSeconds); }

	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// UObject: the UObjects of the active, starting, waiting and queued requests (FRequest is not reflected).
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;

private:
	enum class EStage : uint8
	{
		None,
		Closing,
		CoveredWait,      // covered; waiting CoveredFrames presented frames
		WaitingForLoad,   // level: covered, the game has not started loading yet
		Loading,          // level: PreLoadMap / NotifyLoadStarted seen
		WaitingWorld,     // level: loaded; waiting for begun play + frames + ready signal
		Opening,
	};

	struct FRequest
	{
		FACFTransitionHandle Handle;
		EACFTransitionKind Kind = EACFTransitionKind::Close;
		FACFTransitionSettings Close;
		FACFTransitionSettings Open;
		FACFTransitionOptions Options;
		FName LevelName;
		FString TravelOptions;
		EACFTransitionEvent LastEvent = EACFTransitionEvent::Started;
	};

	FACFTransitionHandle Submit(FRequest&& Request);
	void Admit(FRequest&& Request);
	void Settle(bool bStartQueued);
	void Start(FRequest&& Request);
	void Broadcast(FACFTransitionHandle Handle, EACFTransitionEvent Event);
	void SkipPendingCapture();
	void TickTransitions(float DeltaSeconds);
	void Emit(FRequest& Request, EACFTransitionEvent Event);
	void EmitDeferred(FACFTransitionHandle Handle, EACFTransitionEvent Event);
	void EndActive(EACFTransitionEvent Terminal);
	void StartNextQueued();
	void BeginOpening(bool bFailed);
	void EnterCovered();
	void SetStage(EStage NewStage);
	void UpdateView();
	void AttachOverlay();
	void DetachOverlay();
	void ReleaseCapture();
	void StartCapture();
	bool ResolvePendingCapture();
	void ApplyInputLock(bool bLock);
	float GetTimeoutSeconds() const;
	const FACFTransitionSettings& CurrentSettings() const;
	UWorld* GetGameWorld() const;

	void HandlePreLoadMap(const FString& MapName);
	void HandlePostLoadMap(UWorld* World);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);
	void HandleNetworkFailure(UWorld* World, class UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);

	// Every UObject in these requests is reported to the GC by AddReferencedObjects.
	TOptional<FRequest> Active;
	TArray<FRequest> Queue;
	TArray<FRequest> Incoming;     // Play* requests not applied yet (made while an event was being sent), oldest first
	TOptional<FRequest> Starting;  // an Interrupt request while the interrupted handle sends its terminal event
	bool bStartingCancelled = false;
	int32 DispatchDepth = 0;       // > 0 while listeners run
	bool bSettling = false;
	TArray<TPair<FACFTransitionHandle, EACFTransitionEvent>> PendingEvents;
	TMap<FACFTransitionHandle, EACFTransitionEvent> FinishedEvents; // terminal events of ended handles (bounded)

	EStage Stage = EStage::None;
	float PhaseTime = 0.f;       // seconds in the current phase
	float PhaseStartAlpha = 0.f; // linear progress at which the phase started (continuity after an interrupt)
	float EffectTime = 0.f;      // seconds since the transition started (animated details)
	float StageWait = 0.f;       // seconds waited covered (timeouts)
	uint64 CoveredFrame = 0;
	int32 WorldFramesSinceLoad = 0;
	bool bReadySignal = false;
	bool bLoadFailed = false;
	bool bOpeningAfterFailure = false;
	bool bLevelOpened = false;

	float Coverage = 0.f;
	EACFTransitionPhase DrawPhase = EACFTransitionPhase::Close;
	/** What the overlay draws (the active phase, a held cover or a still). Reset when the overlay is removed. */
	UPROPERTY(Transient)
	FACFTransitionSettings DrawSettings;
	bool bStill = false;

	TSharedPtr<FACFTransitionView> View;
	bool bOverlayAttached = false;
	TWeakObjectPtr<class UGameViewportClient> AttachedViewport;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> Capture = nullptr;

	TSharedPtr<class FACFTransitionGpuCapture> PendingCapture;
	int32 LastCaptureWaitFrames = 0;
	bool bLastCaptureFellBack = false;

	/** The locks this subsystem took (SetIgnoreMoveInput / SetIgnoreLookInput are counters: only these are given back). */
	struct FInputLock
	{
		TWeakObjectPtr<APlayerController> Controller;
		bool bMove = false;
		bool bLook = false;
	};
	TArray<FInputLock> LockedControllers;
	bool bInputLocked = false;

	int32 NextHandleId = 1;
	double LastTickSeconds = 0.0;
	bool bInitialized = false;

	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle NetworkFailureHandle;
};
