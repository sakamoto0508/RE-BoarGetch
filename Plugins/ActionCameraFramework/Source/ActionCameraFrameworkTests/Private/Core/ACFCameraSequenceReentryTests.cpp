#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraSequenceTestListener.h"
#include "ACFCameraTestEnvironment.h"
#include "ACFCameraTestUtils.h"
#include "ACFCameraDirector.h"
#include "ACFCameraSequence.h"
#include "ACFCameraSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase B (README 24 章「イベントの配信」): OnCameraSequenceEvent listeners that call the sequence API, in a real game World
 * (the PlayerCameraManager evaluates ACF every frame). Each case was written to fail on the code before the fix:
 *  - a listener's Play / Finish / Cancel re-entered FlushSequenceEvents, which reset and refilled the array the outer
 *    broadcast was iterating (ensure "Array has changed during ranged-for iteration", events lost / sent twice / out of
 *    order, and a dangling element when the array grew);
 *  - events made by the evaluation were broadcast inside the camera manager's update (the camera cache of the frame
 *    was not written yet).
 * The sequence event is a dynamic delegate only (no native one); it is bound from C++ the way a Blueprint binds it.
 */
namespace ACFSequenceReentry
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	inline const TCHAR* LexType(EACFCameraSequenceEventType Type)
	{
		switch (Type)
		{
		case EACFCameraSequenceEventType::ShotStarted: return TEXT("ShotStarted");
		case EACFCameraSequenceEventType::ShotEnded: return TEXT("ShotEnded");
		default: return TEXT("SequenceEnded");
		}
	}

	inline FString LexReason(EACFCameraSequenceEndReason Reason)
	{
		FString Name = UEnum::GetValueAsString(Reason);
		int32 Colon = INDEX_NONE;
		return Name.FindLastChar(TEXT(':'), Colon) ? Name.Mid(Colon + 1) : Name;
	}

	struct FRec
	{
		FACFCameraSequenceHandle Handle;
		EACFCameraSequenceEventType Type;
		int32 Shot;
		EACFCameraSequenceEndReason Reason;
		bool bInCameraUpdate;
	};

	/** Latent steps, one per frame (each returns true when done). */
	class FSteps : public IAutomationLatentCommand
	{
	public:
		FSteps(FAutomationTestBase* InTest, TArray<TFunction<bool()>> InSteps, TFunction<void()> InCleanup, double InLimit = 30.0)
			: Test(InTest), Steps(MoveTemp(InSteps)), Cleanup(MoveTemp(InCleanup)), Limit(InLimit) {}
		virtual bool Update() override
		{
			if (GetCurrentRunTime() > Limit)
			{
				Test->AddError(FString::Printf(TEXT("Timed out in step %d of %d."), Index + 1, Steps.Num()));
				Finish();
				return true;
			}
			if (Index < Steps.Num() && Steps[Index]())
			{
				++Index;
			}
			if (Index >= Steps.Num())
			{
				Finish();
				return true;
			}
			return false;
		}
	private:
		void Finish()
		{
			if (Cleanup)
			{
				Cleanup();
				Cleanup = nullptr;
			}
		}
		FAutomationTestBase* Test;
		TArray<TFunction<bool()>> Steps;
		TFunction<void()> Cleanup;
		double Limit;
		int32 Index = 0;
	};

	struct FCtx
	{
		FAutomationTestBase* Test = nullptr;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UACFCameraSubsystem> Acf;
		TWeakObjectPtr<APlayerCameraManager> PCM;
		TStrongObjectPtr<UACFCameraSequenceTestListener> Listener;
		TStrongObjectPtr<UACFCameraSequenceTestListener> Second;
		TArray<TStrongObjectPtr<UObject>> Keep;
		TArray<FRec> Events;
		int32 SecondCount = 0;
		TFunction<void(const FACFCameraSequenceEvent&)> React;
		ACFTestEnvironment::FGlobalStateGuard Guard;
		TArray<FACFCameraSequenceHandle> All;
		FACFCameraSequenceHandle H[12];
		TArray<FACFCameraSequenceHandle> Extra;
		bool R[6] = {};
		bool bReacted = false;
		int32 Frames = 0;

		UACFCameraSubsystem* S() const { return Acf.Get(); }

		/** Inside the camera manager's update the camera cache still holds the previous frame (it is written at its end; its
		 *  time stamp is the World time as a float). */
		bool InCameraUpdate() const
		{
			const APlayerCameraManager* M = PCM.Get();
			const UWorld* W = World.Get();
			return M && W && M->GetCameraCacheTime() != static_cast<float>(W->GetTimeSeconds());
		}

		void Record(const FACFCameraSequenceEvent& Event)
		{
			Events.Add({ Event.Sequence, Event.Type, Event.ShotIndex, Event.Reason, InCameraUpdate() });
			if (React)
			{
				React(Event);
			}
		}

		UACFCameraSequence* Seq(int32 Shots, float FirstDuration, float OtherDuration = 5.f)
		{
			UACFCameraSequence* Sequence = NewObject<UACFCameraSequence>(GetTransientPackage(), NAME_None, RF_Transient);
			for (int32 I = 0; I < Shots; ++I)
			{
				FACFCameraShot& Shot = Sequence->Shots.AddDefaulted_GetRef();
				Shot.Name = *FString::Printf(TEXT("Shot%d"), I);
				Shot.Preset = ACFTests::MakeFixedPreset(FVector(300.0 * I, 200.0, 300.0), 70.f);
				Keep.Emplace(Shot.Preset);
				Shot.Duration = I == 0 ? FirstDuration : OtherDuration;
			}
			Keep.Emplace(Sequence);
			return Sequence;
		}

		FACFCameraSequenceHandle Play(UACFCameraSequence* Sequence, bool bStopOthers = true)
		{
			FACFCameraSequenceParams Params;
			Params.bStopOtherSequences = bStopOthers;
			const FACFCameraSequenceHandle Handle = S()->PlayCameraSequence(Sequence, Params);
			All.Add(Handle);
			return Handle;
		}

		bool Cancel(FACFCameraSequenceHandle Handle) { return S()->CancelCameraSequence(Handle); }
		bool Finish(FACFCameraSequenceHandle Handle) { return S()->FinishCameraSequence(Handle); }

		FString Of(FACFCameraSequenceHandle Handle) const
		{
			FString Out;
			for (const FRec& E : Events)
			{
				if (E.Handle != Handle)
				{
					continue;
				}
				FString One = LexType(E.Type);
				if (E.Type != EACFCameraSequenceEventType::SequenceEnded)
				{
					One += FString::FromInt(E.Shot);
				}
				if (E.Type != EACFCameraSequenceEventType::ShotStarted)
				{
					One += TEXT(":") + LexReason(E.Reason);
				}
				Out += (Out.IsEmpty() ? TEXT("") : TEXT(",")) + One;
			}
			return Out;
		}

		bool Has(FACFCameraSequenceHandle Handle, EACFCameraSequenceEventType Type) const
		{
			return Events.ContainsByPredicate([&](const FRec& E) { return E.Handle == Handle && E.Type == Type; });
		}
		bool Ended(FACFCameraSequenceHandle Handle) const { return Has(Handle, EACFCameraSequenceEventType::SequenceEnded); }

		/** Every handle: shots end after they start and once, one SequenceEnded and nothing after it; never inside the camera update. */
		void CheckContract(const TCHAR* What, bool bAllEnded)
		{
			int32 Problems = 0;
			for (const FACFCameraSequenceHandle& Handle : All)
			{
				TSet<int32> Open;
				TSet<int32> Closed;
				bool bEnded = false;
				for (const FRec& E : Events)
				{
					if (E.Handle != Handle)
					{
						continue;
					}
					const bool bBad = bEnded
						|| (E.Type == EACFCameraSequenceEventType::ShotStarted && (Open.Contains(E.Shot) || Closed.Contains(E.Shot)))
						|| (E.Type == EACFCameraSequenceEventType::ShotEnded && !Open.Contains(E.Shot))
						|| (E.Type == EACFCameraSequenceEventType::SequenceEnded && !Open.IsEmpty());
					if (bBad)
					{
						++Problems;
						Test->AddError(FString::Printf(TEXT("%s: %s: %s%d out of order (%s)."), What, *Handle.ToString(), LexType(E.Type), E.Shot, *Of(Handle)));
					}
					if (E.Type == EACFCameraSequenceEventType::ShotStarted) { Open.Add(E.Shot); }
					if (E.Type == EACFCameraSequenceEventType::ShotEnded) { Open.Remove(E.Shot); Closed.Add(E.Shot); }
					if (E.Type == EACFCameraSequenceEventType::SequenceEnded) { bEnded = true; }
				}
				if (bAllEnded && !bEnded)
				{
					++Problems;
					Test->AddError(FString::Printf(TEXT("%s: %s never ended (%s)."), What, *Handle.ToString(), *Of(Handle)));
				}
			}
			int32 InUpdate = 0;
			for (const FRec& E : Events)
			{
				InUpdate += E.bInCameraUpdate ? 1 : 0;
			}
			Test->TestEqual(*FString::Printf(TEXT("%s: events broadcast inside the camera manager's update"), What), InUpdate, 0);
			Test->TestEqual(*FString::Printf(TEXT("%s: event contract"), What), Problems, 0);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequenceReentryListenersTest, "ActionCameraFramework.Sequence.Reentry.ListenerCommands", ACFSequenceReentry::Flags)
bool FACFSequenceReentryListenersTest::RunTest(const FString& Parameters)
{
	using namespace ACFSequenceReentry;
	UWorld* World = ACFTestEnvironment::FindGameWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
	UACFCameraSubsystem* Acf = Player ? Player->GetSubsystem<UACFCameraSubsystem>() : nullptr;
	if (!World || !World->HasBegunPlay() || !Acf || !PC->PlayerCameraManager)
	{
		AddWarning(TEXT("Skipped: needs a game World with the first player's camera (run with -game)."));
		return true;
	}
	TSharedPtr<FCtx> Ctx = MakeShared<FCtx>();
	Ctx->Test = this;
	Ctx->World = World;
	Ctx->Acf = Acf;
	Ctx->PCM = PC->PlayerCameraManager;
	Ctx->Guard.Capture(World, {});
	ACFTestEnvironment::BeginScenario(*Acf);
	Ctx->Listener.Reset(NewObject<UACFCameraSequenceTestListener>());
	Ctx->Second.Reset(NewObject<UACFCameraSequenceTestListener>());
	FCtx* Raw = Ctx.Get();
	Ctx->Listener->Callback = [Raw](const FACFCameraSequenceEvent& Event) { Raw->Record(Event); };
	Ctx->Second->Callback = [Raw](const FACFCameraSequenceEvent&) { ++Raw->SecondCount; };
	Acf->OnCameraSequenceEvent.AddDynamic(Ctx->Listener.Get(), &UACFCameraSequenceTestListener::OnSequenceEvent);
	Acf->OnCameraSequenceEvent.AddDynamic(Ctx->Second.Get(), &UACFCameraSequenceTestListener::OnSequenceEvent);

	TArray<TFunction<bool()>> Steps;
	// 1. The ShotStarted listener cancels the same sequence.
	Steps.Add([Raw]()
	{
		Raw->H[0] = Raw->Play(Raw->Seq(1, 5.f));
		Raw->React = [Raw](const FACFCameraSequenceEvent& E)
		{
			if (E.Sequence == Raw->H[0] && E.Type == EACFCameraSequenceEventType::ShotStarted)
			{
				Raw->R[0] = Raw->Cancel(Raw->H[0]);
			}
		};
		return true;
	});
	Steps.Add([this, Raw]()
	{
		if (!Raw->Ended(Raw->H[0])) { return false; }
		TestTrue(TEXT("1: Cancel from ShotStarted"), Raw->R[0]);
		TestEqual(TEXT("1: events"), Raw->Of(Raw->H[0]), FString(TEXT("ShotStarted0,ShotEnded0:Cancelled,SequenceEnded:Cancelled")));
		TestFalse(TEXT("1: not playing"), Raw->S()->IsCameraSequencePlaying(Raw->H[0]));
		// 2. The ShotEnded listener of shot 0 plays a new sequence (it replaces this one, whose shot 1 has started).
		Raw->H[1] = Raw->Play(Raw->Seq(2, 0.05f));
		Raw->React = [Raw](const FACFCameraSequenceEvent& E)
		{
			if (E.Sequence == Raw->H[1] && E.Type == EACFCameraSequenceEventType::ShotEnded && E.ShotIndex == 0)
			{
				Raw->H[2] = Raw->Play(Raw->Seq(1, 5.f));
			}
		};
		return true;
	});
	Steps.Add([this, Raw]()
	{
		if (!Raw->Ended(Raw->H[1]) || !Raw->Has(Raw->H[2], EACFCameraSequenceEventType::ShotStarted)) { return false; }
		TestEqual(TEXT("2: replaced sequence: its events in the order they happened"), Raw->Of(Raw->H[1]),
			FString(TEXT("ShotStarted0,ShotEnded0:Completed,ShotStarted1,ShotEnded1:Replaced,SequenceEnded:Replaced")));
		TestEqual(TEXT("2: the new sequence started"), Raw->Of(Raw->H[2]), FString(TEXT("ShotStarted0")));
		Raw->React = nullptr;
		TestTrue(TEXT("2: Cancel from gameplay code"), Raw->Cancel(Raw->H[2]));
		TestEqual(TEXT("2: gameplay Cancel is sent at once"), Raw->Of(Raw->H[2]), FString(TEXT("ShotStarted0,ShotEnded0:Cancelled,SequenceEnded:Cancelled")));
		// 3. The SequenceEnded listener plays another sequence.
		Raw->H[3] = Raw->Play(Raw->Seq(1, 0.05f));
		Raw->React = [Raw](const FACFCameraSequenceEvent& E)
		{
			if (E.Sequence == Raw->H[3] && E.Type == EACFCameraSequenceEventType::SequenceEnded)
			{
				Raw->H[4] = Raw->Play(Raw->Seq(1, 5.f));
			}
		};
		return true;
	});
	Steps.Add([this, Raw]()
	{
		if (!Raw->Ended(Raw->H[3]) || !Raw->Has(Raw->H[4], EACFCameraSequenceEventType::ShotStarted)) { return false; }
		TestEqual(TEXT("3: ended sequence"), Raw->Of(Raw->H[3]), FString(TEXT("ShotStarted0,ShotEnded0:Completed,SequenceEnded:Completed")));
		TestEqual(TEXT("3: the next sequence started once"), Raw->Of(Raw->H[4]), FString(TEXT("ShotStarted0")));
		Raw->React = nullptr;
		Raw->Cancel(Raw->H[4]);
		// 4. Several Finish / Cancel / Play in one listener, and a second end of the same playbacks.
		Raw->H[5] = Raw->Play(Raw->Seq(1, 5.f), false);
		Raw->H[6] = Raw->Play(Raw->Seq(1, 5.f), false);
		Raw->bReacted = false;
		Raw->React = [Raw](const FACFCameraSequenceEvent& E)
		{
			if (Raw->bReacted || E.Sequence != Raw->H[5] || E.Type != EACFCameraSequenceEventType::ShotStarted)
			{
				return;
			}
			Raw->bReacted = true;
			Raw->R[0] = Raw->Finish(Raw->H[5]);
			Raw->R[1] = Raw->Cancel(Raw->H[6]);
			Raw->R[2] = Raw->Cancel(Raw->H[5]);
			Raw->R[3] = Raw->Finish(Raw->H[6]);
			Raw->H[7] = Raw->Play(Raw->Seq(1, 5.f), false);
			Raw->H[8] = Raw->Play(Raw->Seq(1, 5.f), false);
			Raw->R[4] = Raw->S()->SendCameraSequenceCommand(Raw->H[7], EACFCameraSequenceCommand::Cancel, NAME_None);
		};
		return true;
	});
	Steps.Add([this, Raw]()
	{
		if (!Raw->Ended(Raw->H[5]) || !Raw->Ended(Raw->H[6]) || !Raw->Ended(Raw->H[7]) || !Raw->Has(Raw->H[8], EACFCameraSequenceEventType::ShotStarted)) { return false; }
		TestTrue(TEXT("4: Finish"), Raw->R[0]);
		TestTrue(TEXT("4: Cancel of the other one"), Raw->R[1]);
		TestFalse(TEXT("4: second Cancel of a finished sequence"), Raw->R[2]);
		TestFalse(TEXT("4: second Finish of a cancelled sequence"), Raw->R[3]);
		TestTrue(TEXT("4: Cancel of a sequence that has not started"), Raw->R[4]);
		TestEqual(TEXT("4: finished"), Raw->Of(Raw->H[5]), FString(TEXT("ShotStarted0,ShotEnded0:Advanced,SequenceEnded:Advanced")));
		TestEqual(TEXT("4: cancelled"), Raw->Of(Raw->H[6]), FString(TEXT("ShotStarted0,ShotEnded0:Cancelled,SequenceEnded:Cancelled")));
		TestEqual(TEXT("4: cancelled before its first shot"), Raw->Of(Raw->H[7]), FString(TEXT("SequenceEnded:Cancelled")));
		TestEqual(TEXT("4: played from the listener"), Raw->Of(Raw->H[8]), FString(TEXT("ShotStarted0")));
		Raw->React = nullptr;
		Raw->Cancel(Raw->H[8]);
		// 5. The listener plays 64 sequences (each replaces the previous one): the queue grows (reallocates) while sent.
		Raw->H[9] = Raw->Play(Raw->Seq(1, 5.f));
		Raw->bReacted = false;
		Raw->React = [Raw](const FACFCameraSequenceEvent& E)
		{
			if (Raw->bReacted || E.Sequence != Raw->H[9] || E.Type != EACFCameraSequenceEventType::ShotStarted)
			{
				return;
			}
			Raw->bReacted = true;
			UACFCameraSequence* Shared = Raw->Seq(1, 5.f);
			for (int32 I = 0; I < 64; ++I)
			{
				Raw->Extra.Add(Raw->Play(Shared));
			}
		};
		return true;
	});
	Steps.Add([this, Raw]()
	{
		if (Raw->Extra.Num() < 64 || !Raw->Has(Raw->Extra.Last(), EACFCameraSequenceEventType::ShotStarted)) { return false; }
		TestEqual(TEXT("5: replaced by the listener's first Play"), Raw->Of(Raw->H[9]), FString(TEXT("ShotStarted0,ShotEnded0:Replaced,SequenceEnded:Replaced")));
		int32 Wrong = 0;
		for (int32 I = 0; I + 1 < Raw->Extra.Num(); ++I)
		{
			Wrong += Raw->Of(Raw->Extra[I]) == TEXT("SequenceEnded:Replaced") ? 0 : 1;
		}
		TestEqual(TEXT("5: each replaced playback ended once (before its first shot)"), Wrong, 0);
		TestEqual(TEXT("5: the last one plays"), Raw->Of(Raw->Extra.Last()), FString(TEXT("ShotStarted0")));
		int32 Order = 0;
		int32 Last = INDEX_NONE;
		for (int32 I = 0; I < Raw->Extra.Num(); ++I)
		{
			const int32 At = Raw->Events.IndexOfByPredicate([&](const FRec& E) { return E.Handle == Raw->Extra[I]; });
			Order += At > Last ? 0 : 1;
			Last = At;
		}
		TestEqual(TEXT("5: sent in the order they happened"), Order, 0);
		Raw->React = nullptr;
		Raw->Cancel(Raw->Extra.Last());
		return true;
	});
	Steps.Add([this, Raw]()
	{
		Raw->CheckContract(TEXT("Sequence reentry"), true);
		TestEqual(TEXT("A second listener got every event once"), Raw->SecondCount, Raw->Events.Num());
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(this, MoveTemp(Steps), [this, Ctx]()
	{
		Ctx->React = nullptr;
		if (UACFCameraSubsystem* S = Ctx->S())
		{
			S->OnCameraSequenceEvent.RemoveAll(Ctx->Listener.Get());
			S->OnCameraSequenceEvent.RemoveAll(Ctx->Second.Get());
			if (UACFCameraDirector* Director = S->GetDirector())
			{
				Director->StopAllSequences(true);
			}
		}
		ACFTestEnvironment::EndScenario(*this, Ctx->S(), Ctx->PCM.Get(), Ctx->Guard);
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequenceReentryCameraUpdateTest, "ActionCameraFramework.Sequence.Reentry.CameraUpdateAndTeardown", ACFSequenceReentry::Flags)
bool FACFSequenceReentryCameraUpdateTest::RunTest(const FString& Parameters)
{
	using namespace ACFSequenceReentry;
	UWorld* World = ACFTestEnvironment::FindGameWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	UACFCameraSubsystem* Acf = PC && PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UACFCameraSubsystem>() : nullptr;
	if (!World || !World->HasBegunPlay() || !Acf)
	{
		AddWarning(TEXT("Skipped: needs a game World with the first player's camera (run with -game)."));
		return true;
	}
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this);
	TSharedPtr<FCtx> Ctx = MakeShared<FCtx>();
	Ctx->Test = this;
	Ctx->World = World;
	Ctx->Guard.Capture(World, {});
	// A second local player: its camera subsystem is torn down while it has events of a camera update to send.
	APlayerController* PC2 = UGameplayStatics::CreatePlayer(World, -1, true);
	ULocalPlayer* Player2 = PC2 ? PC2->GetLocalPlayer() : nullptr;
	UACFCameraSubsystem* Acf2 = Player2 ? Player2->GetSubsystem<UACFCameraSubsystem>() : nullptr;
	if (!Acf2)
	{
		AddWarning(TEXT("Skipped: no second local player here."));
		Ctx->Guard.Restore();
		return true;
	}
	Ctx->Acf = Acf2;
	Ctx->Listener.Reset(NewObject<UACFCameraSequenceTestListener>());
	FCtx* Raw = Ctx.Get();
	Ctx->Listener->Callback = [Raw](const FACFCameraSequenceEvent& Event) { Raw->Events.Add({ Event.Sequence, Event.Type, Event.ShotIndex, Event.Reason, false }); };
	Acf2->OnCameraSequenceEvent.AddDynamic(Ctx->Listener.Get(), &UACFCameraSequenceTestListener::OnSequenceEvent);
	const FACFCameraSequenceHandle Handle = Ctx->Play(Ctx->Seq(1, 5.f));
	// What the output modifier calls inside the camera manager's update: the shot starts here, its event waits.
	FMinimalViewInfo View;
	View.Location = FVector(0.0, 0.0, 200.0);
	View.FOV = 90.f;
	Acf2->EvaluateForOutput(1.f / 60.f, View);
	TestTrue(TEXT("The shot started in the evaluation"), Acf2->IsCameraSequencePlaying(Handle) && Acf2->GetCameraSequenceState(Handle).ShotIndex == 0);
	TestEqual(TEXT("No event is broadcast inside the camera update"), Ctx->Events.Num(), 0);
	// The player goes (its subsystem deinitializes): the waiting events are dropped, never sent later.
	TWeakObjectPtr<UACFCameraSubsystem> Weak2 = Acf2;
	ACFTestEnvironment::RemoveLocalPlayer(Player2);
	TArray<TFunction<bool()>> Steps;
	Steps.Add([Raw]() { return ++Raw->Frames >= 4; }); // a few frames: world ticks, post actor ticks
	Steps.Add([this, Raw]()
	{
		TestEqual(TEXT("No event after the subsystem was deinitialized"), Raw->Events.Num(), 0);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(this, MoveTemp(Steps), [this, Ctx, Weak2]()
	{
		if (UACFCameraSubsystem* S = Weak2.Get())
		{
			S->OnCameraSequenceEvent.RemoveAll(Ctx->Listener.Get());
		}
		for (const FString& Leak : Ctx->Guard.Restore())
		{
			AddInfo(FString::Printf(TEXT("Restored: %s"), *Leak));
		}
	}));
	return true;
}


// Phase D-3: split screen. Each local player has its own camera subsystem, queue and listeners; a listener of player 1
// cancels player 2's sequence and plays another one there (a call into the other player's subsystem while player 1's
// events are being sent).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequenceReentrySplitScreenTest, "ActionCameraFramework.Sequence.Reentry.SplitScreenPlayers", ACFSequenceReentry::Flags)
bool FACFSequenceReentrySplitScreenTest::RunTest(const FString& Parameters)
{
	using namespace ACFSequenceReentry;
	UWorld* World = ACFTestEnvironment::FindGameWorld();
	APlayerController* PC1 = World ? World->GetFirstPlayerController() : nullptr;
	UACFCameraSubsystem* Acf1 = PC1 && PC1->GetLocalPlayer() ? PC1->GetLocalPlayer()->GetSubsystem<UACFCameraSubsystem>() : nullptr;
	if (!World || !World->HasBegunPlay() || !Acf1 || !PC1->PlayerCameraManager)
	{
		AddWarning(TEXT("Skipped: needs a game World with the first player's camera (run with -game)."));
		return true;
	}
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this);
	TSharedPtr<FCtx> C1 = MakeShared<FCtx>();
	TSharedPtr<FCtx> C2 = MakeShared<FCtx>();
	C1->Test = this;
	C1->World = World;
	C1->Guard.Capture(World, {});
	APlayerController* PC2 = UGameplayStatics::CreatePlayer(World, -1, true);
	UACFCameraSubsystem* Acf2 = PC2 && PC2->GetLocalPlayer() ? PC2->GetLocalPlayer()->GetSubsystem<UACFCameraSubsystem>() : nullptr;
	if (!Acf2 || !PC2->PlayerCameraManager)
	{
		AddWarning(TEXT("Skipped: no second local player with a camera here."));
		C1->Guard.Restore();
		return true;
	}
	C1->Acf = Acf1;
	C1->PCM = PC1->PlayerCameraManager;
	C2->Test = this;
	C2->World = World;
	C2->Acf = Acf2;
	C2->PCM = PC2->PlayerCameraManager;
	FCtx* R1 = C1.Get();
	FCtx* R2 = C2.Get();
	for (FCtx* R : { R1, R2 })
	{
		R->Listener.Reset(NewObject<UACFCameraSequenceTestListener>());
		R->Listener->Callback = [R](const FACFCameraSequenceEvent& Event) { R->Record(Event); };
		R->S()->OnCameraSequenceEvent.AddDynamic(R->Listener.Get(), &UACFCameraSequenceTestListener::OnSequenceEvent);
	}

	TArray<TFunction<bool()>> Steps;
	Steps.Add([R1, R2]()
	{
		R1->H[0] = R1->Play(R1->Seq(2, 0.05f, 0.05f));
		R2->H[0] = R2->Play(R2->Seq(1, 5.f));
		// Player 1's first shot: cancel player 2's sequence and start another one there.
		R1->React = [R1, R2](const FACFCameraSequenceEvent& E)
		{
			if (E.Sequence == R1->H[0] && E.Type == EACFCameraSequenceEventType::ShotStarted && E.ShotIndex == 0)
			{
				R1->R[0] = R2->Cancel(R2->H[0]);
				R2->H[1] = R2->Play(R2->Seq(1, 0.05f));
			}
		};
		return true;
	});
	Steps.Add([this, R1, R2]()
	{
		if (!R1->Ended(R1->H[0]) || !R2->Ended(R2->H[1])) { return false; }
		TestTrue(TEXT("Player 1's listener cancelled player 2's sequence"), R1->R[0]);
		TestEqual(TEXT("Player 1: its own sequence, in order"), R1->Of(R1->H[0]),
			FString(TEXT("ShotStarted0,ShotEnded0:Completed,ShotStarted1,ShotEnded1:Completed,SequenceEnded:Completed")));
		TestEqual(TEXT("Player 2: cancelled from player 1's listener"), R2->Of(R2->H[0]), FString(TEXT("ShotStarted0,ShotEnded0:Cancelled,SequenceEnded:Cancelled")));
		TestEqual(TEXT("Player 2: the sequence started from player 1's listener"), R2->Of(R2->H[1]), FString(TEXT("ShotStarted0,ShotEnded0:Completed,SequenceEnded:Completed")));
		int32 Crossed = 0;
		for (const FRec& E : R1->Events) { Crossed += (E.Handle == R2->H[0] || E.Handle == R2->H[1]) ? 1 : 0; }
		for (const FRec& E : R2->Events) { Crossed += E.Handle == R1->H[0] ? 1 : 0; }
		TestEqual(TEXT("Each player's listener gets only its own player's events"), Crossed, 0);
		R1->CheckContract(TEXT("Split screen player 1"), true);
		R2->CheckContract(TEXT("Split screen player 2"), true);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(this, MoveTemp(Steps), [this, C1, C2]()
	{
		for (FCtx* R : { C1.Get(), C2.Get() })
		{
			R->React = nullptr;
			if (UACFCameraSubsystem* S = R->S())
			{
				S->OnCameraSequenceEvent.RemoveAll(R->Listener.Get());
				if (UACFCameraDirector* Director = S->GetDirector())
				{
					Director->StopAllSequences(true);
				}
			}
		}
		for (const FString& Restored : C1->Guard.Restore())
		{
			AddInfo(FString::Printf(TEXT("Restored: %s"), *Restored));
		}
	}));
	return true;
}

#endif
