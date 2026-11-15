#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestEnvironment.h"
#include "Core/ACFCameraTestUtils.h"
#include "ACFCameraZoneTestListener.h"
#include "ACFCameraDirector.h"
#include "ACFCameraSubsystem.h"
#include "ACFCameraZone.h"
#include "ACFCameraZoneComponent.h"
#include "ACFCameraZoneSubsystem.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase C (README 32 章「通知と再入」): zone change listeners (native OnZoneSelectionChanged and the Blueprint OnZoneChanged)
 * that call the zone API. Before the fix Resolve kept a reference into the Players array across the broadcast and used
 * it afterwards (SyncRequest, UpdatePlayer's return value), and NotifyZoneChanged / ResolvePlayersOf broadcast while
 * iterating that array: a listener that removed or added a player made the subsystem use another player's state or a
 * freed element (ensure "Array has changed during ranged-for iteration"). Synchronous, real game World (-game).
 */
namespace ACFZoneReentry
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
	const FVector Far(0.0, 30000.0, -200000.0);

	struct FFixture
	{
		UWorld* World = nullptr;
		UACFCameraZoneSubsystem* Zones = nullptr;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TArray<TStrongObjectPtr<ULocalPlayer>> Bare;
		TArray<TStrongObjectPtr<UObject>> Keep;
		TArray<FString> Events;
		FDelegateHandle Native;
		TFunction<void(ULocalPlayer*, UACFCameraZoneComponent*, UACFCameraZoneComponent*)> React;
		TStrongObjectPtr<UACFCameraZoneTestListener> Listener;

		bool Init(FAutomationTestBase& Test)
		{
			World = ACFTestEnvironment::FindGameWorld();
			Zones = World ? World->GetSubsystem<UACFCameraZoneSubsystem>() : nullptr;
			if (!World || !World->HasBegunPlay() || !Zones)
			{
				Test.AddWarning(TEXT("Skipped: needs a game World that has begun play (run with -game)."));
				return false;
			}
			Native = Zones->OnZoneSelectionChanged.AddLambda([this](ULocalPlayer* Player, UACFCameraZoneComponent* From, UACFCameraZoneComponent* To)
			{
				Events.Add(FString::Printf(TEXT("%s:%s>%s"), *Name(Player), *Name(From), *Name(To)));
				if (React)
				{
					React(Player, From, To);
				}
			});
			Listener.Reset(NewObject<UACFCameraZoneTestListener>());
			Zones->OnZoneChanged.AddDynamic(Listener.Get(), &UACFCameraZoneTestListener::OnZoneChanged);
			return true;
		}

		~FFixture()
		{
			if (Zones)
			{
				Zones->OnZoneSelectionChanged.Remove(Native);
				if (Listener)
				{
					Zones->OnZoneChanged.RemoveDynamic(Listener.Get(), &UACFCameraZoneTestListener::OnZoneChanged);
				}
				for (const TStrongObjectPtr<ULocalPlayer>& Player : Bare)
				{
					Zones->RemovePlayer(Player.Get());
				}
			}
			for (const TWeakObjectPtr<AActor>& Actor : Spawned)
			{
				if (Actor.IsValid() && !Actor->IsActorBeingDestroyed())
				{
					Actor->Destroy();
				}
			}
		}

		FString Name(const UObject* Object) const
		{
			if (!Object)
			{
				return TEXT("-");
			}
			const UACFCameraZoneComponent* Zone = Cast<UACFCameraZoneComponent>(Object);
			const UObject* Named = Zone ? Zone->GetOwner() : Object;
			return Named ? Named->GetName() : TEXT("?");
		}

		/** A local player without an ACF camera (selection only), like the zone tests' second key. */
		ULocalPlayer* NewPlayer(const TCHAR* InName)
		{
			ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine, MakeUniqueObjectName(GEngine, ULocalPlayer::StaticClass(), InName));
			Bare.Emplace(Player);
			return Player;
		}

		AACFCameraZone* Box(const TCHAR* InName, const FVector& Center, const FVector& Extent, int32 Priority, UACFCameraPreset* Preset = nullptr)
		{
			FActorSpawnParameters Params;
			Params.Name = MakeUniqueObjectName(World->PersistentLevel, AACFCameraZone::StaticClass(), InName);
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AACFCameraZone* Zone = World->SpawnActor<AACFCameraZone>(Center, FRotator::ZeroRotator, Params);
			Zone->GetBox()->SetBoxExtent(Extent, false);
			Zone->GetZone()->ZonePriority = Priority;
			Zone->GetZone()->Preset = Preset;
			Spawned.Add(Zone);
			return Zone;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneReentryPlayersTest, "ActionCameraFramework.Zone.Reentry.ListenersChangePlayers", ACFZoneReentry::Flags)
bool FACFZoneReentryPlayersTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneReentry;
	FFixture F;
	if (!F.Init(*this))
	{
		return true;
	}
	AddExpectedMessagePlain(TEXT("has no Camera Preset"), EAutomationExpectedMessageFlags::Contains, -1); // selection-only zones
	AACFCameraZone* A = F.Box(TEXT("ZoneA"), Far, FVector(1000.0, 1000.0, 500.0), 0);
	AACFCameraZone* B = F.Box(TEXT("ZoneB"), Far + FVector(500.0, 0.0, 0.0), FVector(200.0, 200.0, 200.0), 1);
	AACFCameraZone* C = F.Box(TEXT("ZoneC"), Far + FVector(5000.0, 0.0, 0.0), FVector(300.0, 300.0, 300.0), 0);
	FVector InA = Far + FVector(-500.0, 0.0, 0.0);
	FVector InB = Far + FVector(500.0, 0.0, 0.0);
	FVector InC = Far + FVector(5000.0, 0.0, 0.0);
	ULocalPlayer* P1 = F.NewPlayer(TEXT("P1"));
	ULocalPlayer* P2 = F.NewPlayer(TEXT("P2"));

	// 1. The native listener removes the notified player. P1's state is before P2's: removing it moves P2's state into
	//    P1's slot, so the old code synced and returned P2's state (C) for P1.
	F.Zones->UpdatePlayer(P1, nullptr);
	TestTrue(TEXT("1: P2 in C"), F.Zones->UpdatePlayer(P2, &InC) == C->GetZone());
	const int32 States = F.Zones->GetNumPlayerStates();
	F.React = [&F, P1](ULocalPlayer* Player, UACFCameraZoneComponent*, UACFCameraZoneComponent*)
	{
		if (Player == P1)
		{
			F.Zones->RemovePlayer(P1);
		}
	};
	UACFCameraZoneComponent* Returned = F.Zones->UpdatePlayer(P1, &InA);
	F.React = nullptr;
	TestTrue(TEXT("1: UpdatePlayer of the removed player returns nothing (not another player's zone)"), Returned == nullptr);
	TestTrue(TEXT("1: P2 keeps C"), F.Zones->GetSelectedZone(P2) == C->GetZone());
	TestTrue(TEXT("1: P1 is gone"), F.Zones->GetSelectedZone(P1) == nullptr);
	TestEqual(TEXT("1: one state less"), F.Zones->GetNumPlayerStates(), States - 1);
	TestEqual(TEXT("1: P1 notified once"), F.Events.FilterByPredicate([&](const FString& E) { return E.StartsWith(F.Name(P1) + TEXT(":")); }).Num(), 1);

	// 2. The Blueprint listener updates 40 other local players (the Players array grows and reallocates) while P1 enters A.
	TArray<ULocalPlayer*> Many;
	for (int32 I = 0; I < 40; ++I)
	{
		Many.Add(F.NewPlayer(*FString::Printf(TEXT("M%d"), I)));
	}
	F.Events.Reset();
	F.Listener->Callback = [&F, &Many, &InC](APlayerController*, UACFCameraZoneComponent*, UACFCameraZoneComponent* NewZone)
	{
		if (F.Listener->Calls.Num() == 1)
		{
			for (ULocalPlayer* M : Many)
			{
				F.Zones->UpdatePlayer(M, &InC);
			}
		}
	};
	F.Listener->Calls.Reset();
	TestTrue(TEXT("2: P1 enters A"), F.Zones->UpdatePlayer(P1, &InA) == A->GetZone());
	int32 InCNum = 0;
	for (ULocalPlayer* M : Many)
	{
		InCNum += F.Zones->GetSelectedZone(M) == C->GetZone() ? 1 : 0;
	}
	TestEqual(TEXT("2: the 40 players updated by the listener are in C"), InCNum, 40);
	TestEqual(TEXT("2: native events: P1 then the 40 players"), F.Events.Num(), 41);
	TestTrue(TEXT("2: P1's event first"), F.Events.Num() > 0 && F.Events[0].StartsWith(F.Name(P1) + TEXT(":")));
	TestEqual(TEXT("2: Blueprint events"), F.Listener->Calls.Num(), 41);
	TestTrue(TEXT("2: P1 still in A"), F.Zones->GetSelectedZone(P1) == A->GetZone());

	// 3. Enabling / disabling a zone resolves every player while iterating them: the listener adds players meanwhile.
	TestTrue(TEXT("3: P1 enters B"), F.Zones->UpdatePlayer(P1, &InB) == B->GetZone());
	TArray<ULocalPlayer*> More;
	for (int32 I = 0; I < 20; ++I)
	{
		More.Add(F.NewPlayer(*FString::Printf(TEXT("N%d"), I)));
	}
	F.Listener->Callback = nullptr;
	bool bAdded = false;
	F.React = [&](ULocalPlayer* Player, UACFCameraZoneComponent*, UACFCameraZoneComponent*)
	{
		if (Player == P1 && !bAdded)
		{
			bAdded = true;
			for (ULocalPlayer* N : More)
			{
				F.Zones->UpdatePlayer(N, &InA);
			}
		}
	};
	B->GetZone()->SetZoneEnabled(false);
	F.React = nullptr;
	TestTrue(TEXT("3: B disabled: P1 in A"), F.Zones->GetSelectedZone(P1) == A->GetZone());
	int32 InANum = 0;
	for (ULocalPlayer* N : More)
	{
		InANum += F.Zones->GetSelectedZone(N) == A->GetZone() ? 1 : 0;
	}
	TestEqual(TEXT("3: players added by the listener are in A"), InANum, 20);
	int32 InCStill = 0;
	for (ULocalPlayer* M : Many)
	{
		InCStill += F.Zones->GetSelectedZone(M) == C->GetZone() ? 1 : 0;
	}
	TestEqual(TEXT("3: the other players are unaffected"), InCStill, 40);
	B->GetZone()->SetZoneEnabled(true);

	// 4. The listener moves the same player on: A -> B -> C in one call, in order, and the last change wins.
	TestTrue(TEXT("4: P1 in A"), F.Zones->UpdatePlayer(P1, &InA) == A->GetZone());
	F.Events.Reset();
	F.React = [&](ULocalPlayer* Player, UACFCameraZoneComponent*, UACFCameraZoneComponent* To)
	{
		if (Player == P1 && To == B->GetZone())
		{
			F.Zones->UpdatePlayer(P1, &InC);
		}
	};
	UACFCameraZoneComponent* Chained = F.Zones->UpdatePlayer(P1, &InB);
	F.React = nullptr;
	const FString Expected4 = FString::Printf(TEXT("%s:%s>%s|%s:%s>%s"), *F.Name(P1), *F.Name(A), *F.Name(B), *F.Name(P1), *F.Name(B), *F.Name(C));
	TestEqual(TEXT("4: events in the order of the changes"), FString::Join(F.Events, TEXT("|")), Expected4);
	TestTrue(TEXT("4: UpdatePlayer returns the selection after its listeners (C)"), Chained == C->GetZone());
	TestTrue(TEXT("4: P1 in C"), F.Zones->GetSelectedZone(P1) == C->GetZone());

	// 5. Two players in different zones: P1's listener moves P2; each keeps its own state.
	TestTrue(TEXT("5: P2 in C"), F.Zones->UpdatePlayer(P2, &InC) == C->GetZone());
	F.Events.Reset();
	F.React = [&](ULocalPlayer* Player, UACFCameraZoneComponent*, UACFCameraZoneComponent*)
	{
		if (Player == P1)
		{
			F.Zones->UpdatePlayer(P2, &InB);
		}
	};
	TestTrue(TEXT("5: P1 to A"), F.Zones->UpdatePlayer(P1, &InA) == A->GetZone());
	F.React = nullptr;
	TestTrue(TEXT("5: P2 in B"), F.Zones->GetSelectedZone(P2) == B->GetZone());
	TestTrue(TEXT("5: P1 in A"), F.Zones->GetSelectedZone(P1) == A->GetZone());
	TestEqual(TEXT("5: events"), FString::Join(F.Events, TEXT("|")),
		FString::Printf(TEXT("%s:%s>%s|%s:%s>%s"), *F.Name(P1), *F.Name(C), *F.Name(A), *F.Name(P2), *F.Name(C), *F.Name(B)));

	// 6. The listener destroys the zone the player just entered (EndPlay unregisters it): the player falls back to A.
	const int32 ZonesBefore = F.Zones->GetNumZones();
	const FString NameB = F.Name(B);
	F.Events.Reset();
	F.React = [&](ULocalPlayer* Player, UACFCameraZoneComponent*, UACFCameraZoneComponent* To)
	{
		if (Player == P1 && To == B->GetZone())
		{
			B->Destroy();
		}
	};
	F.Zones->UpdatePlayer(P1, &InB);
	F.React = nullptr;
	TestTrue(TEXT("6: P1 back in A"), F.Zones->GetSelectedZone(P1) == A->GetZone());
	TestTrue(TEXT("6: P2 left the destroyed zone"), F.Zones->GetSelectedZone(P2) != nullptr && F.Zones->GetSelectedZone(P2) != B->GetZone());
	TestEqual(TEXT("6: B unregistered"), F.Zones->GetNumZones(), ZonesBefore - 1);
	TestTrue(TEXT("6: P1 entered B, then left it"), F.Events.Num() >= 2 && F.Events[0] == FString::Printf(TEXT("%s:%s>%s"), *F.Name(P1), *F.Name(A), *NameB)
		&& F.Events.Contains(FString::Printf(TEXT("%s:%s>%s"), *F.Name(P1), *NameB, *F.Name(A))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneReentryRequestTest, "ActionCameraFramework.Zone.Reentry.RequestAndRemoval", ACFZoneReentry::Flags)
bool FACFZoneReentryRequestTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneReentry;
	FFixture F;
	if (!F.Init(*this))
	{
		return true;
	}
	ULocalPlayer* Real = F.World->GetGameInstance() ? F.World->GetGameInstance()->GetFirstGamePlayer() : nullptr;
	UACFCameraSubsystem* Acf = Real ? Real->GetSubsystem<UACFCameraSubsystem>() : nullptr;
	UACFCameraDirector* Director = Acf ? Acf->GetDirector() : nullptr;
	if (!Director)
	{
		AddWarning(TEXT("Skipped: needs the first player's ACF camera (run with -game)."));
		return true;
	}
	UACFCameraPreset* Preset = ACFTests::MakeFixedPreset(Far + FVector(0.0, 0.0, 800.0), 70.f);
	F.Keep.Emplace(Preset);
	AACFCameraZone* R = F.Box(TEXT("ZoneR"), Far + FVector(0.0, 20000.0, 0.0), FVector(300.0, 300.0, 300.0), 0, Preset);
	FVector InR = Far + FVector(0.0, 20000.0, 0.0);
	FVector Out = Far + FVector(0.0, 40000.0, 0.0);
	const int32 RequestsBefore = Director->GetNumRequests();

	// The zone request is made before the listeners hear of the change (they see the camera they will get).
	bool bRequestAtEvent = false;
	F.React = [&](ULocalPlayer* Player, UACFCameraZoneComponent*, UACFCameraZoneComponent* To)
	{
		if (Player == Real && To == R->GetZone())
		{
			bRequestAtEvent = F.Zones->GetZoneRequest(Real).IsValid() && Director->HasRequest(F.Zones->GetZoneRequest(Real));
		}
	};
	TestTrue(TEXT("Enters R"), F.Zones->UpdatePlayer(Real, &InR) == R->GetZone());
	TestTrue(TEXT("The zone request exists when the listeners are told"), bRequestAtEvent);
	TestEqual(TEXT("One zone request"), Director->GetNumRequests(), RequestsBefore + 1);

	// The listener moves the player back in, then out again (the same player, re-entrant): no request left.
	int32 Moves = 0;
	F.React = [&](ULocalPlayer* Player, UACFCameraZoneComponent*, UACFCameraZoneComponent* To)
	{
		if (Player != Real || Moves >= 2)
		{
			return;
		}
		++Moves;
		F.Zones->UpdatePlayer(Real, To == nullptr ? &InR : &Out);
	};
	F.Zones->UpdatePlayer(Real, &Out);
	F.React = nullptr;
	TestTrue(TEXT("Out -> listener back in -> listener out: no zone"), F.Zones->GetSelectedZone(Real) == nullptr);
	TestFalse(TEXT("No zone request when out"), F.Zones->GetZoneRequest(Real).IsValid());
	TestEqual(TEXT("Requests as before"), Director->GetNumRequests(), RequestsBefore);

	// The listener removes the notified player: its request goes, none is made again for the removed state.
	F.React = [&](ULocalPlayer* Player, UACFCameraZoneComponent*, UACFCameraZoneComponent* To)
	{
		if (Player == Real && To == R->GetZone())
		{
			F.Zones->RemovePlayer(Real);
		}
	};
	UACFCameraZoneComponent* Returned = F.Zones->UpdatePlayer(Real, &InR);
	F.React = nullptr;
	TestTrue(TEXT("Removed in the event: UpdatePlayer returns nothing"), Returned == nullptr);
	TestFalse(TEXT("Removed in the event: no zone request"), F.Zones->GetZoneRequest(Real).IsValid());
	TestEqual(TEXT("Removed in the event: requests as before"), Director->GetNumRequests(), RequestsBefore);
	return true;
}

#endif
