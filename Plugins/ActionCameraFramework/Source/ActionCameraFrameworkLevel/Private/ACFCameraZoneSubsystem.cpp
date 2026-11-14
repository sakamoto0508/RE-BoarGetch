#include "ACFCameraZoneSubsystem.h"
#include "ACFCameraZoneComponent.h"
#include "ACFCameraZoneResolver.h"
#include "ACFCameraZoneSettings.h"
#include "ACFCameraDirector.h"
#include "ACFCameraSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, ActionCameraFrameworkLevel)

UACFCameraZoneSettings::UACFCameraZoneSettings()
{
}

namespace ACFCameraZoneSubsystemImpl
{
	static UACFCameraSubsystem* GetCamera(const ULocalPlayer* Player)
	{
		return Player ? Player->GetSubsystem<UACFCameraSubsystem>() : nullptr;
	}
}

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
	// The World ends: the zone requests go (the local players and their cameras outlive the World).
	ReleaseAllZoneRequests();
	Zones.Reset();
	Players.Reset();
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
	SyncRequest(State);
}

void UACFCameraZoneSubsystem::SyncRequest(FPlayerState& State)
{
	ULocalPlayer* Player = State.Player.Get();
	UACFCameraSubsystem* Camera = ACFCameraZoneSubsystemImpl::GetCamera(Player);
	UACFCameraDirector* Director = Camera ? Camera->GetDirector() : nullptr;
	UWorld* World = GetWorld();
	if (!Director || !World)
	{
		return; // a player without ACF camera (tests' bare local players): selection only
	}
	if (World->bIsTearingDown)
	{
		ReleasePlayer(State);
		return;
	}
	const UACFCameraZoneSettings* Settings = GetDefault<UACFCameraZoneSettings>();
	FACFCameraZoneRequestContext Context;
	Context.Director = Director;
	Context.Camera = Camera;
	APlayerController* Controller = Player->GetPlayerController(World);
	if (Controller && Controller->GetWorld() == World)
	{
		if (APawn* Pawn = Controller->GetPawn())
		{
			Context.FollowTarget.Actor = Pawn;
		}
		if (const APlayerCameraManager* CameraManager = Controller->PlayerCameraManager)
		{
			Context.bHasViewRotation = true;
			Context.ViewRotation = CameraManager->GetCameraCacheView().Rotation; // the last rendered view
		}
		TWeakObjectPtr<APlayerController> WeakController(Controller);
		Context.SetControlRotation = [WeakController](const FRotator& Rotation)
		{
			if (APlayerController* PC = WeakController.Get())
			{
				PC->SetControlRotation(Rotation);
			}
		};
	}
	Context.Time = World->GetRealTimeSeconds();
	Context.Priority = Settings->ZoneRequestPriority;
	Context.RetryDelay = Settings->RequestRetryDelay;
	Context.RetryMaxDelay = Settings->RequestRetryMaxDelay;
	State.Request.Update(Context, State.Selected.Get());
}

void UACFCameraZoneSubsystem::ReleaseAllZoneRequests()
{
	for (FPlayerState& State : Players)
	{
		ReleasePlayer(State);
	}
}

void UACFCameraZoneSubsystem::ReleasePlayer(FPlayerState& State)
{
	const UACFCameraSubsystem* Camera = ACFCameraZoneSubsystemImpl::GetCamera(State.Player.Get());
	State.Request.Release(Camera ? Camera->GetDirector() : nullptr);
	State.Selected.Reset();
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

FACFCameraRequestHandle UACFCameraZoneSubsystem::GetZoneRequest(const ULocalPlayer* Player) const
{
	const FACFCameraZoneRequestState* State = GetRequestState(Player);
	return State ? State->GetHandle() : FACFCameraRequestHandle();
}

const FACFCameraZoneRequestState* UACFCameraZoneSubsystem::GetRequestState(const ULocalPlayer* Player) const
{
	const FPlayerState* Found = Players.FindByPredicate([Player](const FPlayerState& State) { return State.Player.Get() == Player; });
	return Found ? &Found->Request : nullptr;
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
	const TArray<ULocalPlayer*>& LocalPlayers = GameInstance->GetLocalPlayers();
	for (ULocalPlayer* Player : LocalPlayers)
	{
		const APlayerController* Controller = Player ? Player->GetPlayerController(World) : nullptr;
		if (!Controller || Controller->GetWorld() != World)
		{
			continue;
		}
		const APawn* Pawn = Controller->GetPawn();
		const FVector Location = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		UpdatePlayer(Player, Pawn ? &Location : nullptr);
		if (FPlayerState* State = Players.FindByPredicate([Player](const FPlayerState& Entry) { return Entry.Player.Get() == Player; }))
		{
			State->bDrivenByTick = true;
		}
	}
	// A player this driver updated that is no longer one of this World's players (removed from the GameInstance, or its
	// controller is elsewhere): its zone request goes now and its state is dropped, not when the player object is garbage
	// collected.
	Players.RemoveAll([this, World, &LocalPlayers](FPlayerState& State)
	{
		ULocalPlayer* Player = State.Player.Get();
		if (!State.bDrivenByTick || !Player)
		{
			return false;
		}
		const APlayerController* Controller = Player->GetPlayerController(World);
		if (LocalPlayers.Contains(Player) && Controller && Controller->GetWorld() == World)
		{
			return false;
		}
		ReleasePlayer(State);
		return true;
	});
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
				const FACFCameraZoneRequestState* Request = Subsystem->GetRequestState(Player);
				FString RequestText = TEXT("no state");
				if (Request)
				{
					RequestText = Request->GetHandle().IsValid() ? Request->GetHandle().ToString()
						: Request->HasFailed() ? FString(TEXT("no request (no preset)"))
						: Request->IsWaitingForRetry() ? FString::Printf(TEXT("lost %d time(s), retry at %.2f"), Request->GetNumLost(), Request->GetNextRetryTime())
						: FString(TEXT("no request"));
				}
				UE_LOG(LogTemp, Display, TEXT("  player %s: %s, zone request %s"), *GetNameSafe(Player), Selected ? *GetNameSafe(Selected->GetOwner()) : TEXT("no zone"), *RequestText);
			}
		}
	}

	static FAutoConsoleCommandWithWorld GStatusCmd(TEXT("ACF.Zone.Status"), TEXT("Camera Zones of this World and each local player's selected zone (Phase 7C)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&Status));
}
#endif
