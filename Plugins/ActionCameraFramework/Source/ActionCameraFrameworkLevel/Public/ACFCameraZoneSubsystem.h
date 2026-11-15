#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ACFCameraZoneRequests.h"
#include "ACFCameraZoneSubsystem.generated.h"

class UACFCameraZoneComponent;
class ULocalPlayer;
class UACFCameraSubsystem;
class AActor;
class APlayerController;

/** Phase 7C-3B (Blueprint): the selected zone of a local player changed. Either zone can be null (no zone). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FACFCameraZoneChangedSignature, APlayerController*, PlayerController, UACFCameraZoneComponent*, PreviousZone, UACFCameraZoneComponent*, NewZone);

#if !UE_BUILD_SHIPPING
/** Phase 7C-3B: cost of the zone updates since ResetCost (measurement, development builds only; off by default). */
struct FACFCameraZoneCost
{
	/** Point-in-shape tests of the registered zones (FindCandidate's loop). */
	uint64 GeometryCycles = 0;
	/** ACFCameraZoneResolver::Resolve over the candidates. */
	uint64 ResolverCycles = 0;
	/** The zone request of the player (FACFCameraZoneRequestState::Update and its director calls). */
	uint64 RequestCycles = 0;
	int32 Updates = 0;
	int32 ZonesTested = 0;
	int32 Candidates = 0;
};
#endif

/** Phase 7C-3A: what the zone subsystem knows about one player, for the Debugger (read only, copied). */
struct FACFCameraZonePlayerDebug
{
	/** The selected zone (Resolver + delays), null if none. */
	TWeakObjectPtr<UACFCameraZoneComponent> Selected;
	/** The Pending Candidate waiting for its delay (bHasPending with a null zone: leaving to no zone). */
	bool bHasPending = false;
	TWeakObjectPtr<UACFCameraZoneComponent> Pending;
	float PendingElapsed = 0.f;
	/** The delay the pending change needs: max(selected's Exit Delay, pending's Enter Delay). */
	float PendingRequired = 0.f;
	int32 NumTeleports = 0;
	/** The zone camera request this subsystem owns for the player (FACFCameraZoneRequestState). */
	TWeakObjectPtr<UACFCameraZoneComponent> RequestZone;
	FACFCameraRequestHandle ZoneRequest;
	bool bRequestFailed = false;
	bool bWaitingForRetry = false;
	int32 NumLost = 0;
	/** Seconds until the retry (real time), when waiting. */
	double RetryIn = 0.0;
	bool bTrackingView = false;
	/** 7C-3B: zone requests are suppressed (Suppress Zone Requests): the selection goes on, no request is kept. */
	bool bRequestsSuppressed = false;
};

/** Phase 7C-2A: one update of a player's zone (the frame driver fills everything; direct calls may set only Location). */
struct FACFCameraZonePlayerUpdate
{
	/** The point tested against the zones (the pawn location). Null: no pawn; the zone is left at once. */
	const FVector* Location = nullptr;

	/** Game time since the previous update (Enter / Exit Delay). 0: no time passes. */
	float DeltaTime = 0.f;

	/**
	 * The pawn the location belongs to. A different pawn than in the previous update (possession, respawn) is resolved like a
	 * teleport; a jump of more than Teleport Distance of the same pawn too. Null: no pawn tracking (direct calls).
	 */
	const AActor* Pawn = nullptr;

	/** Teleport (the game's NotifyTeleport, or the caller): resolved at once, no delay, no Exit Margin, cut. */
	bool bTeleport = false;
};

/**
 * Phase 7C-1A: the Camera Zones of a World and the zone each local player has selected (README 32 章).
 *
 * - Zones register from BeginPlay to EndPlay (destruction, level streaming, end of play); the list keeps weak references.
 * - Selection: once per frame for every local player of the World, from the player's pawn location: ACFCameraZoneResolver
 *   over every registered zone. The subsystem ticks with the World's tickable objects, after the actors (the pawn has moved)
 *   and before the camera update of the same frame; also when ACF has no request (OnPreEvaluate only fires while ACF's
 *   output modifier is attached), not while the game is paused. Each local player has its own state; a player without a
 *   pawn selects nothing.
 * - A zone that unregisters or is disabled is left at once: the players that had selected it are resolved again from
 *   their last location (the next zone, or none), without waiting for the next frame.
 * - 7C-2A: a different zone (the Pending Candidate) must stay the resolver's answer for the Enter / Exit Delay before it
 *   becomes the selection; a change of the candidate restarts the wait, the candidate going back to the selection drops it
 *   (no request is made while waiting). Teleports (distance, the game's NotifyTeleport), a new pawn, a pawn that goes, a
 *   disabled / removed selected zone change the selection at once (teleports also without Exit Margin, with cuts).
 * - 7C-1B: after every resolve the player's zone camera request is brought in line with the selection
 *   (FACFCameraZoneRequestState: at most one request per player, Priority UACFCameraZoneSettings::ZoneRequestPriority).
 *   The request is released when the zone is left, disabled, destroyed or streamed out, when the player leaves this World
 *   or is removed, and when the World ends.
 * - Notifications and re-entrancy (README 32 章「通知と再入」): a selection change is told to the listeners
 *   (OnZoneSelectionChanged, then OnZoneChanged, per change) after the call that made it has finished all of its work:
 *   the selection and the zone request of every player it touched are up to date, and no reference into the players'
 *   states is held. Changes are told once each, in the order they happened. A listener may call any function of this
 *   subsystem (UpdatePlayer of any player, RemovePlayer, enable / disable / destroy zones); the changes its calls make
 *   are told after the current ones by the same dispatch (no nested broadcast), at most 1024 per dispatch (the rest at
 *   the next call). Nothing is told after the World ends.
 */
UCLASS(BlueprintType)
class ACTIONCAMERAFRAMEWORKLEVEL_API UACFCameraZoneSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Native: (local player, previous zone, new zone); either can be null. Called when the selection changes (see the class
	 *  comment for when: after the change and its zone request, never while the players' states are being updated). */
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnZoneSelectionChanged, ULocalPlayer*, UACFCameraZoneComponent*, UACFCameraZoneComponent*);
	FOnZoneSelectionChanged OnZoneSelectionChanged;

	/** 7C-3B (Blueprint): the selected zone of a local player changed (after Enter / Exit Delay; at once for teleports). */
	UPROPERTY(BlueprintAssignable, Category = "Action Camera|Zone", meta = (DisplayName = "On ACF Camera Zone Changed"))
	FACFCameraZoneChangedSignature OnZoneChanged;

	/** 7C-3B (Blueprint): the zone the player is in (its selected zone), null if none. */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Zone", meta = (DisplayName = "Get ACF Camera Zone"))
	UACFCameraZoneComponent* GetZoneOfPlayer(const APlayerController* PlayerController) const;

	/**
	 * 7C-3B: while at least one source suppresses them, no zone camera is requested for any player (the zone requests are
	 * released now: with the zone's Blend Out when on screen); the selection and the delays go on. When the last source is
	 * released (or destroyed), the players in a zone get its camera again. Use it to stop zone cameras on purpose (a cut
	 * scene, a menu) instead of releasing the zone request, which the zone subsystem requests again. A source counts once.
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Zone", meta = (DefaultToSelf = "Source", DisplayName = "Suppress ACF Camera Zone Requests"))
	void SuppressZoneRequests(UObject* Source);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Zone", meta = (DefaultToSelf = "Source", DisplayName = "Release ACF Camera Zone Request Suppression"))
	bool ReleaseZoneRequestSuppression(UObject* Source);

	UFUNCTION(BlueprintPure, Category = "Action Camera|Zone", meta = (DisplayName = "Are ACF Camera Zone Requests Suppressed"))
	bool AreZoneRequestsSuppressed() const;

#if !UE_BUILD_SHIPPING
	/** 7C-3B: measures the cost of the zone updates (FACFCameraZoneCost). Off by default; tests / ACF.Zone.Profile. */
	void SetCostRecording(bool bEnabled) { bRecordCost = bEnabled; Cost = FACFCameraZoneCost(); }
	/** ACF.Zone.Profile: measures the next Frames frame updates and logs the result. */
	void StartProfile(int32 Frames);
	bool IsRecordingCost() const { return bRecordCost; }
	void ResetCost() { Cost = FACFCameraZoneCost(); }
	const FACFCameraZoneCost& GetCost() const { return Cost; }
#endif

	//~ UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Called by the zones (BeginPlay / EndPlay / Set Zone Enabled). */
	void RegisterZone(UACFCameraZoneComponent* Zone);
	void UnregisterZone(UACFCameraZoneComponent* Zone);
	void NotifyZoneChanged(UACFCameraZoneComponent* Zone);

	/** Registered zones that still exist (registration order). */
	void GetZones(TArray<UACFCameraZoneComponent*>& Out) const;
	int32 GetNumZones() const;

	/**
	 * Resolves the player's zone at Location (null Location: no pawn, nothing selected) and remembers it. The frame driver
	 * calls it with the pawn location; tests and tools can call it directly. Returns the selected zone.
	 */
	UACFCameraZoneComponent* UpdatePlayer(ULocalPlayer* Player, const FVector* Location);

	/** 7C-2A: the same with game time (Enter / Exit Delay), the pawn (teleport / pawn change detection) and a teleport hint. */
	UACFCameraZoneComponent* UpdatePlayer(ULocalPlayer* Player, const FACFCameraZonePlayerUpdate& Update);

	/**
	 * 7C-2A: the zone waiting to become the player's selection (Enter / Exit Delay). False if none is waiting. OutZone null
	 * with true: leaving to no zone is waiting.
	 */
	bool GetPendingZone(const ULocalPlayer* Player, UACFCameraZoneComponent*& OutZone, float& OutElapsed) const;

	/** 7C-2A: teleports (distance, NotifyTeleport, hint) and pawn changes seen for the player. Tests / debugging. */
	int32 GetNumTeleports(const ULocalPlayer* Player) const;

	/** 7C-3A: the player's selection, pending change and zone request in one copy. False if the player is unknown. */
	bool GetPlayerDebug(const ULocalPlayer* Player, FACFCameraZonePlayerDebug& Out) const;

	/** The player's selected zone, null if none (or the player is unknown). */
	UACFCameraZoneComponent* GetSelectedZone(const ULocalPlayer* Player) const;

	/** Number of local players with a state (players that are gone are dropped at the next update). */
	int32 GetNumPlayerStates() const;

	/**
	 * 7C-2B: releases the player's zone request and drops its state now (a local player that is being removed, the bare local
	 * players of tests). Players of the GameInstance are dropped by the frame driver without it. True if it had a state.
	 */
	bool RemovePlayer(const ULocalPlayer* Player);

	/**
	 * 7C-1B: releases every player's zone camera request (plain release) and clears the selections (what the end of the
	 * World does). The next frame selects and requests again for the players still inside a zone.
	 */
	void ReleaseAllZoneRequests();

	/** 7C-1B: the player's zone camera request (invalid if none: no zone, no preset, or lost and waiting for a retry). */
	FACFCameraRequestHandle GetZoneRequest(const ULocalPlayer* Player) const;

	/** 7C-1B: the player's request state (null if the player is unknown). Tests / debugging. */
	const FACFCameraZoneRequestState* GetRequestState(const ULocalPlayer* Player) const;

private:
	struct FPlayerState
	{
		TWeakObjectPtr<ULocalPlayer> Player;
		TWeakObjectPtr<UACFCameraZoneComponent> Selected;
		FVector Location = FVector::ZeroVector;
		bool bHasLocation = false;
		/** Updated by the frame driver (a player of the GameInstance), not only by direct UpdatePlayer calls. */
		bool bDrivenByTick = false;
		FACFCameraZoneRequestState Request;
		/** 7C-2A: the candidate waiting for its delay (bHasPending with a null zone: leaving to no zone). */
		TWeakObjectPtr<UACFCameraZoneComponent> Pending;
		bool bHasPending = false;
		float PendingElapsed = 0.f;
		/** 7C-2A: the pawn of the last update (pawn changes), the director's teleport count of the last frame. */
		TWeakObjectPtr<const AActor> LastPawn;
		uint32 LastTeleportCount = 0;
		bool bHasTeleportCount = false;
		int32 NumTeleports = 0;
	};

	/** How a resolve may change the selection. */
	enum class EResolveMode : uint8
	{
		/** Through the Pending Candidate and its delay; Exit Margin applies. */
		Normal,
		/** At once (no delay): the selected zone went away / was disabled, or there is no pawn. Exit Margin applies. */
		Immediate,
		/** At once, without Exit Margin, and the camera change is a cut (teleport, new pawn). */
		Teleport,
	};

	FPlayerState& FindOrAddState(ULocalPlayer* Player);
	void Resolve(FPlayerState& State, EResolveMode Mode = EResolveMode::Normal, float DeltaTime = 0.f);
	/** The resolver's answer at the player's location (Exit Margin for the selected zone when bUseMargin). */
	UACFCameraZoneComponent* FindCandidate(const FPlayerState& State, const UACFCameraZoneComponent* Selected, bool bUseMargin) const;
	/** Registered, enabled, with a shape. */
	bool IsUsable(const UACFCameraZoneComponent* Zone) const;
	void ResolvePlayersOf(const UACFCameraZoneComponent* Zone);
	/** 7C-1B: brings the player's zone camera request in line with its selection. */
	void SyncRequest(FPlayerState& State, bool bCut = false);
	/** Releases the player's zone request (no blend override) and forgets its selection. */
	void ReleasePlayer(FPlayerState& State);

	/** 7C-3B: suppression sources still alive (drops the destroyed ones; true if any changed). */
	bool PruneSuppressionSources();
	/** Brings every player's zone request in line (after the suppression changed). */
	void SyncAllRequests();

	/** A selection change waiting to be told (no reference into Players: listeners may add / remove players). */
	struct FZoneNotification
	{
		TWeakObjectPtr<ULocalPlayer> Player;
		TWeakObjectPtr<UACFCameraZoneComponent> Previous;
		TWeakObjectPtr<UACFCameraZoneComponent> New;
	};

	/** Public entry points that change selections: the outermost one tells the listeners when it has finished. */
	struct FNotifyScope
	{
		explicit FNotifyScope(UACFCameraZoneSubsystem& InOwner) : Owner(InOwner) { ++Owner.NotifyDepth; }
		~FNotifyScope()
		{
			if (--Owner.NotifyDepth == 0)
			{
				Owner.DispatchNotifications();
			}
		}
		UACFCameraZoneSubsystem& Owner;
	};

	/** Tells the waiting changes, FIFO (re-entrant calls only queue). */
	void DispatchNotifications();

	TArray<FZoneNotification> PendingNotifications;
	int32 NotifyDepth = 0;
	bool bNotifying = false;
	bool bNotificationsClosed = false;

	TArray<TWeakObjectPtr<UACFCameraZoneComponent>> Zones;
	TArray<FPlayerState> Players;
	TArray<TWeakObjectPtr<UObject>> SuppressionSources;
#if !UE_BUILD_SHIPPING
	void ReportProfile();
	bool bRecordCost = false;
	mutable FACFCameraZoneCost Cost;
	int32 ProfileFramesLeft = 0;
	TArray<uint64> ProfileTickCycles;
#endif
};
