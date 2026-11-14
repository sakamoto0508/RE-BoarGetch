#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ACFCameraZoneSettings.generated.h"

/** Project Settings > Plugins > Action Camera Framework Zones (Phase 7C-1B, README 32 章). */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Action Camera Framework Zones"))
class ACTIONCAMERAFRAMEWORKLEVEL_API UACFCameraZoneSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UACFCameraZoneSettings();

	/**
	 * Priority of every zone camera request (each local player has at most one; the zones decide among themselves).
	 * Contract: the game's exploration camera below this value (recommended 0), cameras that must win over zones above it
	 * (Lock-On / aim / sequences: the samples use 10 and 1000). A game request with the same priority competes with the
	 * zone by "the most recent wins": the zone subsystem logs a warning once per player when it sees one.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Requests")
	int32 ZoneRequestPriority = 5;

	/**
	 * A zone request that disappears while the player stays in the zone (released by the game, lost instigator) is requested
	 * again after this delay (seconds, real time), doubled after each further loss up to Max Retry Delay. Leaving the zone
	 * or staying with a live request for Max Retry Delay resets it. Never less than 0.1 s.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Requests", meta = (ClampMin = "0.1", Units = s))
	float RequestRetryDelay = 1.f;

	UPROPERTY(Config, EditAnywhere, Category = "Requests", meta = (ClampMin = "0.1", Units = s))
	float RequestRetryMaxDelay = 8.f;

	/**
	 * Phase 7C-2A: a pawn that moves farther than this between two zone updates (one frame) is treated as teleported: its zone
	 * is resolved again at once, without Enter / Exit Delay and Exit Margin, and the zone camera changes with a cut. The game's
	 * NotifyTeleport on the player's ACF camera and a new pawn count as a teleport too.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Selection", meta = (ClampMin = "1.0", Units = cm))
	float TeleportDistance = 1000.f;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
