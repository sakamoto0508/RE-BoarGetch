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

	/** Zone A -> zone B waits max(A's Exit Delay, B's Enter Delay); entering waits the Enter Delay, leaving the Exit Delay. */
	static float RequiredDelay(const UACFCameraZoneComponent* From, const UACFCameraZoneComponent* To)
	{
		float Delay = 0.f;
		if (From)
		{
			Delay = FMath::Max(Delay, From->ExitDelay);
		}
		if (To)
		{
			Delay = FMath::Max(Delay, To->EnterDelay);
		}
		return FMath::IsFinite(Delay) ? Delay : 0.f;
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
	SuppressionSources.Reset();
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
			Resolve(State, EResolveMode::Normal, 0.f);
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
	FACFCameraZonePlayerUpdate Update;
	Update.Location = Location;
	return UpdatePlayer(Player, Update);
}

UACFCameraZoneComponent* UACFCameraZoneSubsystem::UpdatePlayer(ULocalPlayer* Player, const FACFCameraZonePlayerUpdate& Update)
{
	if (!Player)
	{
		return nullptr;
	}
	FPlayerState& State = FindOrAddState(Player);
	const bool bHasLocation = Update.Location && !Update.Location->ContainsNaN();
	EResolveMode Mode = EResolveMode::Normal;
	if (!bHasLocation)
	{
		// No pawn: nothing to follow, the zone is left at once. The next pawn starts without history.
		Mode = EResolveMode::Immediate;
		State.LastPawn.Reset();
	}
	else
	{
		bool bTeleport = Update.bTeleport;
		if (Update.Pawn)
		{
			if (State.LastPawn.Get() != Update.Pawn || State.LastPawn.IsExplicitlyNull())
			{
				// A new pawn (first one, possession, respawn): no movement history and no pending change of the old pawn.
				bTeleport = true;
				State.LastPawn = Update.Pawn;
			}
			else if (State.bHasLocation && FVector::DistSquared(State.Location, *Update.Location) > FMath::Square(static_cast<double>(GetDefault<UACFCameraZoneSettings>()->TeleportDistance)))
			{
				bTeleport = true; // the same pawn jumped
			}
		}
		if (bTeleport)
		{
			Mode = EResolveMode::Teleport;
			++State.NumTeleports;
		}
	}
	State.bHasLocation = bHasLocation;
	State.Location = bHasLocation ? *Update.Location : FVector::ZeroVector;
	Resolve(State, Mode, FMath::IsFinite(Update.DeltaTime) ? FMath::Max(Update.DeltaTime, 0.f) : 0.f);
	return State.Selected.Get();
}

bool UACFCameraZoneSubsystem::IsUsable(const UACFCameraZoneComponent* Zone) const
{
	return Zone && Zone->IsZoneEnabled() && Zone->GetShapeComponent() && Zones.Contains(Zone);
}

UACFCameraZoneComponent* UACFCameraZoneSubsystem::FindCandidate(const FPlayerState& State, const UACFCameraZoneComponent* Selected, bool bUseMargin) const
{
	if (!State.bHasLocation)
	{
		return nullptr;
	}
#if !UE_BUILD_SHIPPING
	const uint64 GeometryStart = bRecordCost ? FPlatformTime::Cycles64() : 0;
#endif
	// Only zones that contain the point (or the selected one within its margin) become candidates; the others cannot win.
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
		Candidate.bCurrent = Zone == Selected;
		Candidate.bInside = Zone->ContainsPoint(State.Location, 0.0);
		Candidate.bInsideWithMargin = Candidate.bInside || (bUseMargin && Candidate.bCurrent && Zone->ContainsPoint(State.Location, Zone->ExitMargin));
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
#if !UE_BUILD_SHIPPING
	const uint64 ResolverStart = bRecordCost ? FPlatformTime::Cycles64() : 0;
#endif
	const int32 Winner = ACFCameraZoneResolver::Resolve(Candidates);
#if !UE_BUILD_SHIPPING
	if (bRecordCost)
	{
		const uint64 End = FPlatformTime::Cycles64();
		Cost.GeometryCycles += ResolverStart - GeometryStart;
		Cost.ResolverCycles += End - ResolverStart;
		Cost.ZonesTested += Zones.Num();
		Cost.Candidates += Candidates.Num();
	}
#endif
	return Winner != INDEX_NONE ? Owners[Winner] : nullptr;
}

void UACFCameraZoneSubsystem::Resolve(FPlayerState& State, EResolveMode Mode, float DeltaTime)
{
	UACFCameraZoneComponent* Previous = State.Selected.Get();
	// A selected zone that is gone (destroyed, streamed out), disabled or without shape is never kept, not even for a delay.
	const bool bSelectedInvalid = !State.Selected.IsExplicitlyNull() && !IsUsable(Previous);
	if (bSelectedInvalid && Mode == EResolveMode::Normal)
	{
		Mode = EResolveMode::Immediate;
	}
	if (State.bHasPending && !State.Pending.IsExplicitlyNull() && !IsUsable(State.Pending.Get()))
	{
		State.bHasPending = false; // the waiting zone went away: no reference to it is kept
		State.Pending.Reset();
	}
	const UACFCameraZoneComponent* Current = bSelectedInvalid ? nullptr : Previous;
	UACFCameraZoneComponent* Candidate = FindCandidate(State, Current, /*bUseMargin*/ Mode != EResolveMode::Teleport);

	bool bCommit = false;
	if (!bSelectedInvalid && Candidate == Previous)
	{
		State.bHasPending = false; // back in the selected zone (or still there): the waiting change is dropped
		State.Pending.Reset();
		State.PendingElapsed = 0.f;
	}
	else if (Mode != EResolveMode::Normal)
	{
		bCommit = true;
	}
	else
	{
		const float Required = ACFCameraZoneSubsystemImpl::RequiredDelay(Current, Candidate);
		if (!State.bHasPending || State.Pending.Get() != Candidate)
		{
			State.bHasPending = true; // a new candidate: the wait starts now
			State.Pending = Candidate;
			State.PendingElapsed = 0.f;
		}
		else
		{
			State.PendingElapsed += DeltaTime;
		}
		bCommit = State.PendingElapsed >= Required - 1e-4f;
	}

	if (bCommit)
	{
		State.Selected = Candidate;
		State.bHasPending = false;
		State.Pending.Reset();
		State.PendingElapsed = 0.f;
		if (Candidate != Previous || bSelectedInvalid)
		{
			OnZoneSelectionChanged.Broadcast(State.Player.Get(), Previous, Candidate);
			if (OnZoneChanged.IsBound())
			{
				ULocalPlayer* Player = State.Player.Get();
				OnZoneChanged.Broadcast(Player ? Player->GetPlayerController(GetWorld()) : nullptr, Previous, Candidate);
			}
		}
	}
#if !UE_BUILD_SHIPPING
	const uint64 RequestStart = bRecordCost ? FPlatformTime::Cycles64() : 0;
#endif
	SyncRequest(State, /*bCut*/ bCommit && Mode == EResolveMode::Teleport);
#if !UE_BUILD_SHIPPING
	if (bRecordCost)
	{
		Cost.RequestCycles += FPlatformTime::Cycles64() - RequestStart;
		++Cost.Updates;
	}
#endif
}

void UACFCameraZoneSubsystem::SyncRequest(FPlayerState& State, bool bCut)
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
	Context.bCut = bCut;
	Context.Time = World->GetRealTimeSeconds();
	Context.Priority = Settings->ZoneRequestPriority;
	Context.RetryDelay = Settings->RequestRetryDelay;
	Context.RetryMaxDelay = Settings->RequestRetryMaxDelay;
	// 7C-3B: while zone requests are suppressed, the request follows "no zone" (released; the selection goes on).
	State.Request.Update(Context, AreZoneRequestsSuppressed() ? nullptr : State.Selected.Get()); // a destroyed source no longer counts
}

bool UACFCameraZoneSubsystem::PruneSuppressionSources()
{
	return SuppressionSources.RemoveAll([](const TWeakObjectPtr<UObject>& Source) { return !Source.IsValid(); }) > 0;
}

void UACFCameraZoneSubsystem::SyncAllRequests()
{
	for (FPlayerState& State : Players)
	{
		if (State.Player.IsValid())
		{
			SyncRequest(State);
		}
	}
}

void UACFCameraZoneSubsystem::SuppressZoneRequests(UObject* Source)
{
	PruneSuppressionSources();
	if (!Source || SuppressionSources.Contains(Source))
	{
		return;
	}
	const bool bWasSuppressed = !SuppressionSources.IsEmpty();
	SuppressionSources.Add(Source);
	if (!bWasSuppressed)
	{
		SyncAllRequests(); // the zone requests go now
	}
}

bool UACFCameraZoneSubsystem::ReleaseZoneRequestSuppression(UObject* Source)
{
	PruneSuppressionSources();
	const bool bRemoved = Source && SuppressionSources.Remove(Source) > 0;
	if (bRemoved && SuppressionSources.IsEmpty())
	{
		SyncAllRequests(); // the players in a zone get its camera again now
	}
	return bRemoved;
}

bool UACFCameraZoneSubsystem::AreZoneRequestsSuppressed() const
{
	return SuppressionSources.ContainsByPredicate([](const TWeakObjectPtr<UObject>& Source) { return Source.IsValid(); });
}

UACFCameraZoneComponent* UACFCameraZoneSubsystem::GetZoneOfPlayer(const APlayerController* PlayerController) const
{
	return PlayerController ? GetSelectedZone(PlayerController->GetLocalPlayer()) : nullptr;
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
	State.bHasPending = false;
	State.Pending.Reset();
	State.PendingElapsed = 0.f;
}

void UACFCameraZoneSubsystem::ResolvePlayersOf(const UACFCameraZoneComponent* Zone)
{
	for (FPlayerState& State : Players)
	{
		// The weak reference of a zone being destroyed can already be stale: those players are resolved too. A selected zone
		// that went away is left at once (Resolve); a pending one is dropped.
		const bool bSelected = State.Selected.Get() == Zone || State.Selected.IsStale();
		const bool bPending = State.bHasPending && (State.Pending.Get() == Zone || State.Pending.IsStale());
		if (State.Player.IsValid() && (bSelected || bPending))
		{
			Resolve(State, EResolveMode::Normal, 0.f);
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

bool UACFCameraZoneSubsystem::GetPendingZone(const ULocalPlayer* Player, UACFCameraZoneComponent*& OutZone, float& OutElapsed) const
{
	OutZone = nullptr;
	OutElapsed = 0.f;
	const FPlayerState* Found = Players.FindByPredicate([Player](const FPlayerState& State) { return State.Player.Get() == Player; });
	if (!Found || !Found->bHasPending)
	{
		return false;
	}
	OutZone = Found->Pending.Get();
	OutElapsed = Found->PendingElapsed;
	return true;
}

bool UACFCameraZoneSubsystem::GetPlayerDebug(const ULocalPlayer* Player, FACFCameraZonePlayerDebug& Out) const
{
	Out = FACFCameraZonePlayerDebug();
	const FPlayerState* Found = Players.FindByPredicate([Player](const FPlayerState& State) { return Player && State.Player.Get() == Player; });
	if (!Found)
	{
		return false;
	}
	Out.Selected = Found->Selected;
	Out.bHasPending = Found->bHasPending;
	Out.Pending = Found->Pending;
	Out.PendingElapsed = Found->PendingElapsed;
	const UACFCameraZoneComponent* Current = IsUsable(Found->Selected.Get()) ? Found->Selected.Get() : nullptr;
	Out.PendingRequired = Found->bHasPending ? ACFCameraZoneSubsystemImpl::RequiredDelay(Current, Found->Pending.Get()) : 0.f;
	Out.NumTeleports = Found->NumTeleports;
	const FACFCameraZoneRequestState& Request = Found->Request;
	Out.RequestZone = Request.GetActiveZone();
	Out.ZoneRequest = Request.GetHandle();
	Out.bRequestFailed = Request.HasFailed();
	Out.bWaitingForRetry = Request.IsWaitingForRetry();
	Out.NumLost = Request.GetNumLost();
	const UWorld* World = GetWorld();
	Out.RetryIn = Request.IsWaitingForRetry() && World ? FMath::Max(0.0, Request.GetNextRetryTime() - World->GetRealTimeSeconds()) : 0.0;
	Out.bTrackingView = Request.IsTrackingView();
	Out.bRequestsSuppressed = AreZoneRequestsSuppressed();
	return true;
}

int32 UACFCameraZoneSubsystem::GetNumTeleports(const ULocalPlayer* Player) const
{
	const FPlayerState* Found = Players.FindByPredicate([Player](const FPlayerState& State) { return State.Player.Get() == Player; });
	return Found ? Found->NumTeleports : 0;
}

UACFCameraZoneComponent* UACFCameraZoneSubsystem::GetSelectedZone(const ULocalPlayer* Player) const
{
	const FPlayerState* Found = Players.FindByPredicate([Player](const FPlayerState& State) { return State.Player.Get() == Player; });
	return Found ? Found->Selected.Get() : nullptr;
}

bool UACFCameraZoneSubsystem::RemovePlayer(const ULocalPlayer* Player)
{
	const int32 Index = Players.IndexOfByPredicate([Player](const FPlayerState& State) { return State.Player.Get() == Player; });
	if (!Player || Index == INDEX_NONE)
	{
		return false;
	}
	ReleasePlayer(Players[Index]);
	Players.RemoveAt(Index);
	return true;
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
	// A destroyed suppression source releases its suppression (the requests come back with this frame's update).
	PruneSuppressionSources();
#if !UE_BUILD_SHIPPING
	// ACF.Zone.Profile: the cost of this frame's updates goes into the running measurement.
	const uint64 TickStart = ProfileFramesLeft > 0 ? FPlatformTime::Cycles64() : 0;
	ON_SCOPE_EXIT
	{
		if (ProfileFramesLeft > 0)
		{
			ProfileTickCycles.Add(FPlatformTime::Cycles64() - TickStart);
			if (--ProfileFramesLeft == 0)
			{
				ReportProfile();
			}
		}
	};
#endif
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
		FACFCameraZonePlayerUpdate Update;
		Update.Location = Pawn ? &Location : nullptr;
		Update.DeltaTime = DeltaTime; // game time (time dilation applies); no tick while paused, so no delay passes
		Update.Pawn = Pawn;
		{
			// The game's NotifyTeleport on this player's ACF camera since the last frame.
			FPlayerState& State = FindOrAddState(Player);
			const UACFCameraSubsystem* Camera = ACFCameraZoneSubsystemImpl::GetCamera(Player);
			if (const UACFCameraDirector* Director = Camera ? Camera->GetDirector() : nullptr)
			{
				const uint32 Count = Director->GetNumTeleportNotifications();
				Update.bTeleport = State.bHasTeleportCount && Count != State.LastTeleportCount;
				State.LastTeleportCount = Count;
				State.bHasTeleportCount = true;
			}
			State.bDrivenByTick = true;
		}
		UpdatePlayer(Player, Update);
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
void UACFCameraZoneSubsystem::StartProfile(int32 Frames)
{
	ProfileFramesLeft = FMath::Clamp(Frames, 1, 100000);
	ProfileTickCycles.Reset(ProfileFramesLeft);
	SetCostRecording(true);
	UE_LOG(LogTemp, Display, TEXT("ACF.Zone.Profile: measuring the next %d frames of %s (%d zone(s), %d player(s))."), ProfileFramesLeft, *GetNameSafe(GetWorld()), GetNumZones(), GetNumPlayerStates());
}

void UACFCameraZoneSubsystem::ReportProfile()
{
	TArray<double> Us;
	Us.Reserve(ProfileTickCycles.Num());
	for (const uint64 Cycles : ProfileTickCycles)
	{
		Us.Add(FPlatformTime::ToMilliseconds64(Cycles) * 1000.0);
	}
	Us.Sort();
	auto At = [&Us](double Fraction) { return Us.IsEmpty() ? 0.0 : Us[FMath::Clamp(FMath::FloorToInt32(Fraction * (Us.Num() - 1) + 0.5), 0, Us.Num() - 1)]; };
	const double Updates = FMath::Max(Cost.Updates, 1);
	auto PerUpdate = [Updates](uint64 Cycles) { return FPlatformTime::ToMilliseconds64(Cycles) * 1000.0 / Updates; };
	UE_LOG(LogTemp, Display, TEXT("ACF.Zone.Profile: %d frames, %d zone(s): Tick p50 %.2f us, p95 %.2f us, max %.2f us (counters included). Per player update: geometry %.2f us (%.0f zones tested), resolver %.2f us (%.1f candidates), request %.2f us."),
		Us.Num(), GetNumZones(), At(0.5), At(0.95), Us.IsEmpty() ? 0.0 : Us.Last(), PerUpdate(Cost.GeometryCycles), Cost.ZonesTested / Updates, PerUpdate(Cost.ResolverCycles), Cost.Candidates / Updates, PerUpdate(Cost.RequestCycles));
	SetCostRecording(false);
	ProfileTickCycles.Empty();
}

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
		UE_LOG(LogTemp, Display, TEXT("ACF.Zone.Status: %d zone(s) in %s%s"), Zones.Num(), *World->GetName(), Subsystem->AreZoneRequestsSuppressed() ? TEXT(", zone requests SUPPRESSED") : TEXT(""));
		for (const UACFCameraZoneComponent* Zone : Zones)
		{
			UE_LOG(LogTemp, Display, TEXT("  %s: priority %d, %s, exit margin %.0f cm, enter / exit delay %.2f / %.2f s, volume %.3g cm3%s"), *GetNameSafe(Zone->GetOwner()), Zone->ZonePriority,
				Zone->IsZoneEnabled() ? TEXT("enabled") : TEXT("disabled"), Zone->ExitMargin, Zone->EnterDelay, Zone->ExitDelay, Zone->GetZoneVolume(), Zone->GetShapeComponent() ? TEXT("") : TEXT(" (no shape)"));
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
				UACFCameraZoneComponent* Pending = nullptr;
				float Elapsed = 0.f;
				const FString PendingText = Subsystem->GetPendingZone(Player, Pending, Elapsed)
					? FString::Printf(TEXT(", pending %s for %.2f s"), Pending ? *GetNameSafe(Pending->GetOwner()) : TEXT("no zone"), Elapsed) : FString();
				UE_LOG(LogTemp, Display, TEXT("  player %s: %s%s, zone request %s"), *GetNameSafe(Player), Selected ? *GetNameSafe(Selected->GetOwner()) : TEXT("no zone"), *PendingText, *RequestText);
			}
		}
	}

		// ACF.Zone.Profile [Frames=300]: cost of the zone frame driver in this World over the next frames (Development builds).
	static void Profile(const TArray<FString>& Args, UWorld* World)
	{
		if (UACFCameraZoneSubsystem* Subsystem = World ? World->GetSubsystem<UACFCameraZoneSubsystem>() : nullptr)
		{
			Subsystem->StartProfile(Args.IsValidIndex(0) ? FCString::Atoi(*Args[0]) : 300);
		}
	}
	static FAutoConsoleCommandWithWorldAndArgs GProfileCmd(TEXT("ACF.Zone.Profile"), TEXT("Measure the Camera Zone frame driver over the next [Frames=300] frames (p50 / p95 / max, geometry / resolver / request)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Profile));

	static FAutoConsoleCommandWithWorld GStatusCmd(TEXT("ACF.Zone.Status"), TEXT("Camera Zones of this World and each local player's selected zone (Phase 7C)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&Status));
}
#endif
