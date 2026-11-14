#include "ACFCameraZoneSubsystem.h"
#include "ACFCameraZoneComponent.h"
#include "ACFCameraZoneResolver.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, ActionCameraFrameworkLevel)

bool UACFCameraZoneSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Players only exist in game / PIE worlds (editor and preview worlds have no zone selection).
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UACFCameraZoneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UACFCameraZoneSubsystem::Deinitialize()
{
	Zones.Reset();
	Players.Reset(); // the World ends: nothing to leave (7C-1B releases the zone requests here)
	Super::Deinitialize();
}

void UACFCameraZoneSubsystem::RegisterZone(UACFCameraZoneComponent* Zone)
{
	if (Zone && !Zones.Contains(Zone))
	{
		Zones.Add(Zone);
	}
}

void UACFCameraZoneSubsystem::UnregisterZone(UACFCameraZoneComponent* Zone)
{
	Zones.RemoveAll([Zone](const TWeakObjectPtr<UACFCameraZoneComponent>& Entry) { return !Entry.IsValid() || Entry.Get() == Zone; });
	ResolvePlayersOf(Zone);
}

void UACFCameraZoneSubsystem::NotifyZoneChanged(UACFCameraZoneComponent* Zone)
{
	// Enabled or disabled: every player is resolved again from its last location (a disabled zone is left at once; an
	// enabled zone is taken by players already inside it without waiting for the next frame).
	for (FPlayerState& State : Players)
	{
		if (State.Player.IsValid())
		{
			Resolve(State);
		}
	}
}

void UACFCameraZoneSubsystem::GetZones(TArray<UACFCameraZoneComponent*>& Out) const
{
	Out.Reset();
	for (const TWeakObjectPtr<UACFCameraZoneComponent>& Entry : Zones)
	{
		if (UACFCameraZoneComponent* Zone = Entry.Get())
		{
			Out.Add(Zone);
		}
	}
}

int32 UACFCameraZoneSubsystem::GetNumZones() const
{
	int32 Num = 0;
	for (const TWeakObjectPtr<UACFCameraZoneComponent>& Entry : Zones)
	{
		Num += Entry.IsValid() ? 1 : 0;
	}
	return Num;
}

UACFCameraZoneSubsystem::FPlayerState& UACFCameraZoneSubsystem::FindOrAddState(ULocalPlayer* Player)
{
	Players.RemoveAll([](const FPlayerState& State) { return !State.Player.IsValid(); }); // removed local players
	if (FPlayerState* Found = Players.FindByPredicate([Player](const FPlayerState& State) { return State.Player.Get() == Player; }))
	{
		return *Found;
	}
	FPlayerState& Added = Players.AddDefaulted_GetRef();
	Added.Player = Player;
	return Added;
}

UACFCameraZoneComponent* UACFCameraZoneSubsystem::UpdatePlayer(ULocalPlayer* Player, const FVector* Location)
{
	if (!Player)
	{
		return nullptr;
	}
	FPlayerState& State = FindOrAddState(Player);
	State.bHasLocation = Location && !Location->ContainsNaN();
	State.Location = State.bHasLocation ? *Location : FVector::ZeroVector;
	Resolve(State);
	return State.Selected.Get();
}

void UACFCameraZoneSubsystem::Resolve(FPlayerState& State)
{
	UACFCameraZoneComponent* Previous = State.Selected.Get();
	UACFCameraZoneComponent* Next = nullptr;
	if (State.bHasLocation)
	{
		// Only zones that contain the point (or the current one within its margin) become candidates; the others cannot win.
		TArray<FACFCameraZoneCandidate, TInlineAllocator<16>> Candidates;
		TArray<UACFCameraZoneComponent*, TInlineAllocator<16>> Owners;
		for (const TWeakObjectPtr<UACFCameraZoneComponent>& Entry : Zones)
		{
			UACFCameraZoneComponent* Zone = Entry.Get();
			if (!Zone || !Zone->IsZoneEnabled() || !Zone->GetShapeComponent())
			{
				continue;
			}
			FACFCameraZoneCandidate Candidate;
			Candidate.bCurrent = Zone == Previous;
			Candidate.bInside = Zone->ContainsPoint(State.Location, 0.0);
			Candidate.bInsideWithMargin = Candidate.bInside || (Candidate.bCurrent && Zone->ContainsPoint(State.Location, Zone->ExitMargin));
			if (!ACFCameraZoneResolver::IsEligible(Candidate))
			{
				continue;
			}
			Candidate.Priority = Zone->ZonePriority;
			Candidate.Volume = Zone->GetZoneVolume();
			Candidate.StableKey = Zone->GetStableKey();
			Candidates.Add(MoveTemp(Candidate));
			Owners.Add(Zone);
		}
		const int32 Winner = ACFCameraZoneResolver::Resolve(Candidates);
		Next = Winner != INDEX_NONE ? Owners[Winner] : nullptr;
	}
	State.Selected = Next;
	if (Next != Previous)
	{
		OnZoneSelectionChanged.Broadcast(State.Player.Get(), Previous, Next);
	}
}

void UACFCameraZoneSubsystem::ResolvePlayersOf(const UACFCameraZoneComponent* Zone)
{
	for (FPlayerState& State : Players)
	{
		// The weak reference of a zone being destroyed can already be stale: those players are resolved too.
		if (State.Player.IsValid() && (State.Selected.Get() == Zone || State.Selected.IsStale()))
		{
			Resolve(State);
		}
	}
}

UACFCameraZoneComponent* UACFCameraZoneSubsystem::GetSelectedZone(const ULocalPlayer* Player) const
{
	const FPlayerState* Found = Players.FindByPredicate([Player](const FPlayerState& State) { return State.Player.Get() == Player; });
	return Found ? Found->Selected.Get() : nullptr;
}

int32 UACFCameraZoneSubsystem::GetNumPlayerStates() const
{
	int32 Num = 0;
	for (const FPlayerState& State : Players)
	{
		Num += State.Player.IsValid() ? 1 : 0;
	}
	return Num;
}

TStatId UACFCameraZoneSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UACFCameraZoneSubsystem, STATGROUP_Tickables);
}

void UACFCameraZoneSubsystem::Tick(float DeltaTime)
{
	// After the actors of this frame (the pawns have moved), before the camera update (LevelTick: tickable objects, then the
	// player camera managers). Every local player of this World, also when ACF has no request.
	UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		return;
	}
	for (ULocalPlayer* Player : GameInstance->GetLocalPlayers())
	{
		const APlayerController* Controller = Player ? Player->GetPlayerController(World) : nullptr;
		if (!Controller || Controller->GetWorld() != World)
		{
			continue;
		}
		const APawn* Pawn = Controller->GetPawn();
		const FVector Location = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		UpdatePlayer(Player, Pawn ? &Location : nullptr);
	}
}

#if !UE_BUILD_SHIPPING
namespace ACFCameraZoneDebug
{
	// ACF.Zone.Status: the registered zones and each local player's selection in this World.
	static void Status(UWorld* World)
	{
		const UACFCameraZoneSubsystem* Subsystem = World ? World->GetSubsystem<UACFCameraZoneSubsystem>() : nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogTemp, Display, TEXT("ACF.Zone.Status: no zone subsystem in %s."), *GetNameSafe(World));
			return;
		}
		TArray<UACFCameraZoneComponent*> Zones;
		Subsystem->GetZones(Zones);
		UE_LOG(LogTemp, Display, TEXT("ACF.Zone.Status: %d zone(s) in %s"), Zones.Num(), *World->GetName());
		for (const UACFCameraZoneComponent* Zone : Zones)
		{
			UE_LOG(LogTemp, Display, TEXT("  %s: priority %d, %s, exit margin %.0f cm, volume %.3g cm3%s"), *GetNameSafe(Zone->GetOwner()), Zone->ZonePriority,
				Zone->IsZoneEnabled() ? TEXT("enabled") : TEXT("disabled"), Zone->ExitMargin, Zone->GetZoneVolume(), Zone->GetShapeComponent() ? TEXT("") : TEXT(" (no shape)"));
		}
		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			for (ULocalPlayer* Player : GameInstance->GetLocalPlayers())
			{
				const UACFCameraZoneComponent* Selected = Subsystem->GetSelectedZone(Player);
				UE_LOG(LogTemp, Display, TEXT("  player %s: %s"), *GetNameSafe(Player), Selected ? *GetNameSafe(Selected->GetOwner()) : TEXT("no zone"));
			}
		}
	}

	static FAutoConsoleCommandWithWorld GStatusCmd(TEXT("ACF.Zone.Status"), TEXT("Camera Zones of this World and each local player's selected zone (Phase 7C)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&Status));
}
#endif
