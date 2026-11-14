#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ACFCameraZoneSubsystem.generated.h"

class UACFCameraZoneComponent;
class ULocalPlayer;

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
 * 7C-1A selects only. 7C-1B requests the selected zone's camera from this state.
 */
UCLASS()
class ACTIONCAMERAFRAMEWORKLEVEL_API UACFCameraZoneSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Native: (local player, previous zone, new zone); either can be null. Called when the selection changes. */
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnZoneSelectionChanged, ULocalPlayer*, UACFCameraZoneComponent*, UACFCameraZoneComponent*);
	FOnZoneSelectionChanged OnZoneSelectionChanged;

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

	/** The player's selected zone, null if none (or the player is unknown). */
	UACFCameraZoneComponent* GetSelectedZone(const ULocalPlayer* Player) const;

	/** Number of local players with a state (players that are gone are dropped at the next update). */
	int32 GetNumPlayerStates() const;

private:
	struct FPlayerState
	{
		TWeakObjectPtr<ULocalPlayer> Player;
		TWeakObjectPtr<UACFCameraZoneComponent> Selected;
		FVector Location = FVector::ZeroVector;
		bool bHasLocation = false;
	};

	FPlayerState& FindOrAddState(ULocalPlayer* Player);
	void Resolve(FPlayerState& State);
	void ResolvePlayersOf(const UACFCameraZoneComponent* Zone);

	TArray<TWeakObjectPtr<UACFCameraZoneComponent>> Zones;
	TArray<FPlayerState> Players;
};
