#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFTransitionSettings.h"
#include "ACFTransitionTestUtils.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

/**
 * Phase 7D review (README 33 章「ロードの担当」): who loads the level of a level transition.
 *  - bManualLoad = true: the game loads. ACF never calls OpenLevel, even when a LevelName is given; the game reports the
 *    load with NotifyLoadStarted / NotifyLoadCompleted (/ NotifyWorldReady). Before the fix ACF opened the level anyway.
 *  - bManualLoad = false with a LevelName: ACF calls OpenLevel after Covered (unchanged).
 *  - A Covered listener that cancels, interrupts or clears the transition: no OpenLevel. Before the fix an Interrupt
 *    requested by the Covered listener was applied after OpenLevel, so the map loaded under the interrupting transition.
 * OpenLevel only stores the travel URL in the world context (the engine travels on its next tick); the test reads it and
 * empties it, so no map is ever loaded (the map name does not exist either). Synchronous (AdvanceForTest), -game.
 */
namespace ACFTransitionLoadOwnership
{
	using namespace ACFTransitionTest;

	static const FName NoSuchMap(TEXT("/Game/ACFTest_NoSuchMap"));

	struct FTravelProbe
	{
		FWorldContext* Context = nullptr;
		explicit FTravelProbe(UWorld* World) : Context(GEngine && World ? GEngine->GetWorldContextFromWorld(World) : nullptr) {}
		~FTravelProbe() { Drop(); }
		bool Pending() const { return Context && !Context->TravelURL.IsEmpty(); }
		/** True when OpenLevel was called since the last Drop (and forgets it, so the engine does not travel). */
		bool Take()
		{
			const bool bWas = Pending();
			Drop();
			return bWas;
		}
		void Drop()
		{
			if (Context)
			{
				Context->TravelURL.Empty();
			}
		}
	};

	struct FEvents
	{
		TWeakObjectPtr<UACFTransitionSubsystem> S;
		FDelegateHandle Binding;
		TArray<TPair<FACFTransitionHandle, EACFTransitionEvent>> All;
		TFunction<void(FACFTransitionHandle, EACFTransitionEvent)> React;
		explicit FEvents(UACFTransitionSubsystem* Sub) : S(Sub)
		{
			Binding = Sub->OnTransitionEventNative.AddLambda([this](FACFTransitionHandle H, EACFTransitionEvent E)
			{
				All.Emplace(H, E);
				if (React)
				{
					React(H, E);
				}
			});
		}
		~FEvents()
		{
			if (UACFTransitionSubsystem* Sub = S.Get())
			{
				Sub->OnTransitionEventNative.Remove(Binding);
			}
		}
		FString Of(FACFTransitionHandle H) const
		{
			FString Out;
			for (const auto& P : All)
			{
				if (P.Key == H)
				{
					Out += (Out.IsEmpty() ? TEXT("") : TEXT(",")) + FString(ACFTransition::LexEvent(P.Value));
				}
			}
			return Out;
		}
		bool Has(FACFTransitionHandle H, EACFTransitionEvent E) const
		{
			return All.ContainsByPredicate([&](const auto& P) { return P.Key == H && P.Value == E; });
		}
	};

	struct FSync
	{
		int32 OldCovered = 0;
		bool bOldCapture = true;
		FSync()
		{
			UACFTransitionProjectSettings* P = GetMutableDefault<UACFTransitionProjectSettings>();
			OldCovered = P->CoveredFrames;
			bOldCapture = P->bCaptureScreen;
			P->CoveredFrames = 0;
			P->bCaptureScreen = false;
		}
		~FSync()
		{
			UACFTransitionProjectSettings* P = GetMutableDefault<UACFTransitionProjectSettings>();
			P->CoveredFrames = OldCovered;
			P->bCaptureScreen = bOldCapture;
		}
	};

	static void Run(UACFTransitionSubsystem* S, TFunctionRef<bool()> Until, int32 MaxTicks = 400)
	{
		for (int32 i = 0; i < MaxTicks && !Until(); ++i)
		{
			S->AdvanceForTest(0.02f);
		}
	}

	static bool Ended(UACFTransitionSubsystem* S, FACFTransitionHandle H)
	{
		const FACFTransitionInfo Info = S->GetTransitionInfo(H);
		return !Info.bActive && !Info.bQueued && ACFTransition::IsTerminal(Info.LastEvent);
	}

	static FACFTransitionOptions Level(bool bManual, bool bLock = true)
	{
		FACFTransitionOptions O;
		O.bManualLoad = bManual;
		O.bLockInput = bLock;
		O.TimeoutSeconds = 60.f;
		return O;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionLoadOwnershipTest, "ActionCameraFramework.Transitions.Level.LoadOwnership", ACFTransitionTest::Flags)
bool FACFTransitionLoadOwnershipTest::RunTest(const FString&)
{
	using namespace ACFTransitionLoadOwnership;
	UACFTransitionSubsystem* S = FindSubsystem();
	UWorld* World = FindGameWorld();
	if (!S || !World)
	{
		AddWarning(TEXT("Skipped: no game instance (run with -game)."));
		return true;
	}
	S->ClearScreen();
	S->AdvanceForTest(0.f);
	FTravelProbe Travel(World);
	if (!Travel.Context)
	{
		AddWarning(TEXT("Skipped: no world context."));
		return true;
	}
	Travel.Drop();
	FSync Sync;
	FEvents Ev(S);
	ON_SCOPE_EXIT { Ev.React = nullptr; S->ClearScreen(); S->AdvanceForTest(0.f); Travel.Drop(); };
	APlayerController* PC = World->GetFirstPlayerController();
	auto Free = [PC]() { return !PC || (!PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored()); };
	const FACFTransitionSettings Close = Settings(EACFTransitionEffect::BlackFade, 0.1f);
	const FACFTransitionSettings Open = Settings(EACFTransitionEffect::BlackFade, 0.1f, EACFTransitionPhase::Open);
	const FString Full(TEXT("Started,Covered,LoadStarted,LoadCompleted,Finished"));

	// 1. ManualLoad + LevelName: the game owns the load. ACF does not open the level and follows the notifications.
	{
		const FACFTransitionHandle H = S->PlayLevelTransition(Close, Open, Level(true), NoSuchMap, FString());
		Run(S, [&]() { return Ev.Has(H, EACFTransitionEvent::Covered); });
		for (int32 i = 0; i < 5; ++i) { S->AdvanceForTest(0.02f); }
		TestFalse(TEXT("ManualLoad + LevelName: ACF does not call OpenLevel"), Travel.Take());
		TestTrue(TEXT("ManualLoad + LevelName: waits for the game's load"), S->GetActiveHandle() == H && S->GetCoverage() >= 1.f);
		TestTrue(TEXT("NotifyLoadStarted"), S->NotifyLoadStarted(H));
		TestTrue(TEXT("NotifyLoadCompleted"), S->NotifyLoadCompleted(H));
		Run(S, [&]() { return Ended(S, H); });
		TestEqual(TEXT("ManualLoad + LevelName: events"), Ev.Of(H), Full);
		TestFalse(TEXT("ManualLoad + LevelName: still no OpenLevel at the end"), Travel.Take());
		TestTrue(TEXT("ManualLoad + LevelName: input free"), Free());
	}

	// 2. ManualLoad without a LevelName: NotifyLoadCompleted alone (LoadStarted is sent for it).
	{
		const FACFTransitionHandle H = S->PlayLevelTransition(Close, Open, Level(true), NAME_None, FString());
		Run(S, [&]() { return Ev.Has(H, EACFTransitionEvent::Covered); });
		TestTrue(TEXT("ManualLoad: NotifyLoadCompleted"), S->NotifyLoadCompleted(H));
		Run(S, [&]() { return Ended(S, H); });
		TestEqual(TEXT("ManualLoad without LevelName: events"), Ev.Of(H), Full);
		TestFalse(TEXT("ManualLoad without LevelName: no OpenLevel"), Travel.Take());
	}

	// 3. Automatic load (ManualLoad = false) with a LevelName: OpenLevel right after Covered (unchanged).
	{
		const FACFTransitionHandle H = S->PlayLevelTransition(Close, Open, Level(false), NoSuchMap, TEXT("listen"));
		Run(S, [&]() { return Ev.Has(H, EACFTransitionEvent::Covered); });
		TestTrue(TEXT("Automatic load: OpenLevel is called after Covered"), Travel.Pending() && Travel.Context->TravelURL.Contains(TEXT("ACFTest_NoSuchMap")) && Travel.Context->TravelURL.Contains(TEXT("listen")));
		Travel.Drop();
		for (int32 i = 0; i < 5; ++i) { S->AdvanceForTest(0.02f); }
		TestFalse(TEXT("Automatic load: OpenLevel only once"), Travel.Take());
		TestTrue(TEXT("Automatic load: waits for the map load"), S->GetActiveHandle() == H);
		TestTrue(TEXT("Cancel while waiting for the map load"), S->Cancel(H, true));
		TestEqual(TEXT("Automatic load cancelled: events"), Ev.Of(H), FString(TEXT("Started,Covered,Cancelled")));
		TestTrue(TEXT("Automatic load cancelled: input free"), Free());
	}

	// 4. The Covered listener cancels / clears / interrupts / queues: OpenLevel only when the transition is still the one
	//    playing after the listeners' requests are applied.
	struct FCase { const TCHAR* Name; int32 What; bool bTravel; const TCHAR* Events; };
	const FCase Cases[] = {
		{ TEXT("Covered listener: Cancel"), 0, false, TEXT("Started,Covered,Cancelled") },
		{ TEXT("Covered listener: ClearScreen"), 1, false, TEXT("Started,Covered,Cancelled") },
		{ TEXT("Covered listener: Interrupt request"), 2, false, TEXT("Started,Covered,Interrupted") },
		{ TEXT("Covered listener: Queue request"), 3, true, TEXT("Started,Covered") },
		{ TEXT("Covered listener: Reject request"), 4, true, TEXT("Started,Covered") },
	};
	for (const FCase& Case : Cases)
	{
		FACFTransitionHandle Other;
		FACFTransitionHandle H;
		Ev.React = [&](FACFTransitionHandle E, EACFTransitionEvent Event)
		{
			if (E != H || Event != EACFTransitionEvent::Covered)
			{
				return;
			}
			FACFTransitionOptions O;
			switch (Case.What)
			{
			case 0: S->Cancel(H, false); break;
			case 1: S->ClearScreen(); break;
			case 2: O.Concurrency = EACFTransitionConcurrency::Interrupt; Other = S->PlayOpen(Open, O); break;
			case 3: O.Concurrency = EACFTransitionConcurrency::Queue; Other = S->PlayOpen(Open, O); break;
			default: O.Concurrency = EACFTransitionConcurrency::Reject; Other = S->PlayOpen(Open, O); break;
			}
		};
		H = S->PlayLevelTransition(Close, Open, Level(false), NoSuchMap, FString());
		Run(S, [&]() { return Ev.Has(H, EACFTransitionEvent::Covered); });
		S->AdvanceForTest(0.02f);
		Ev.React = nullptr;
		TestEqual(*FString::Printf(TEXT("%s: OpenLevel"), Case.Name), Travel.Take(), Case.bTravel);
		TestEqual(*FString::Printf(TEXT("%s: events"), Case.Name), Ev.Of(H), FString(Case.Events));
		if (Case.What == 2)
		{
			TestTrue(TEXT("Covered listener: the interrupting Open plays"), S->GetActiveHandle() == Other);
		}
		S->ClearScreen();
		S->AdvanceForTest(0.f);
		TestTrue(*FString::Printf(TEXT("%s: ended"), Case.Name), Ended(S, H));
		TestTrue(*FString::Printf(TEXT("%s: input free"), Case.Name), Free());
		TestEqual(*FString::Printf(TEXT("%s: no input lock left"), Case.Name), S->GetLockedControllerCount(), 0);
	}

	// 5. ManualLoad while the game loads: Cancel (Loading), Interrupt (waiting), Timeout. No OpenLevel, input given back.
	{
		const FACFTransitionHandle H = S->PlayLevelTransition(Close, Open, Level(true), NoSuchMap, FString());
		Run(S, [&]() { return Ev.Has(H, EACFTransitionEvent::Covered); });
		TestTrue(TEXT("ManualLoad: NotifyLoadStarted"), S->NotifyLoadStarted(H));
		TestTrue(TEXT("ManualLoad: Cancel while loading"), S->Cancel(H, true));
		TestEqual(TEXT("ManualLoad cancelled: events"), Ev.Of(H), FString(TEXT("Started,Covered,LoadStarted,Cancelled")));
		TestFalse(TEXT("ManualLoad cancelled: NotifyLoadCompleted is ignored"), S->NotifyLoadCompleted(H));
		TestTrue(TEXT("ManualLoad cancelled: input free"), Free());

		const FACFTransitionHandle I = S->PlayLevelTransition(Close, Open, Level(true), NoSuchMap, FString());
		Run(S, [&]() { return Ev.Has(I, EACFTransitionEvent::Covered); });
		FACFTransitionOptions O;
		O.Concurrency = EACFTransitionConcurrency::Interrupt;
		const FACFTransitionHandle J = S->PlayOpen(Open, O);
		TestEqual(TEXT("ManualLoad interrupted: events"), Ev.Of(I), FString(TEXT("Started,Covered,Interrupted")));
		TestFalse(TEXT("ManualLoad interrupted: NotifyLoadCompleted is ignored"), S->NotifyLoadCompleted(I));
		Run(S, [&]() { return Ended(S, J); });
		TestTrue(TEXT("ManualLoad interrupted: input free"), Free());

		FACFTransitionOptions T = Level(true);
		T.TimeoutSeconds = 0.3f;
		const FACFTransitionHandle K = S->PlayLevelTransition(Close, Open, T, NoSuchMap, FString());
		Run(S, [&]() { return Ended(S, K); });
		TestEqual(TEXT("ManualLoad timeout: events"), Ev.Of(K), FString(TEXT("Started,Covered,Failed")));
		TestEqual(TEXT("ManualLoad timeout: clear"), S->GetScreenState(), EACFTransitionScreenState::Clear);
		TestTrue(TEXT("ManualLoad timeout: input free"), Free());
		TestFalse(TEXT("ManualLoad: no OpenLevel in any case"), Travel.Take());
	}

	S->ClearScreen();
	S->AdvanceForTest(0.f);
	TestFalse(TEXT("End: nothing active"), S->IsTransitionActive());
	TestEqual(TEXT("End: no input lock"), S->GetLockedControllerCount(), 0);
	TestEqual(TEXT("End: no waiting request"), S->GetWaitingRequestCount(), 0);
	return true;
}

#endif
