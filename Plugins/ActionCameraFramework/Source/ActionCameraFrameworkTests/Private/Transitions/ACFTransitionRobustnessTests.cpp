#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFTransitionSettings.h"
#include "ACFTransitionTestListener.h"
#include "ACFTransitionTestUtils.h"
#include "ACFTransitionView.h"
#include "Curves/CurveFloat.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

/**
 * Phase 7D review fixes (README 33 章「イベントと再入」「GC」「入力ロック」「キャプチャ」): the cases the review found, each written
 * so that it fails on the code before the fix:
 *  - Reentry.*: Play* / Cancel / ClearScreen from event listeners (native and Blueprint delegates). Before the fix an
 *    Interrupt request made by the Interrupted listener was overwritten by the outer request (Started, never ended).
 *  - GC.*: transient curve / texture / material referenced only by active, queued, waiting and held requests survive a GC.
 *  - InputLock.*: controllers spawned or restarted while a locked transition waits are locked; other locks survive.
 *  - Capture.Skip* / Capture.RenderThreadLifetime: Skip while the GPU capture is pending, cancels, leaks.
 * Time is driven with AdvanceForTest (synchronous, -NullRHI works) except where frames must be presented.
 */
namespace ACFTransitionRobust
{
	using namespace ACFTransitionTest;

	/** Every event of every handle; checks the terminal-event contract. */
	struct FAudit
	{
		TWeakObjectPtr<UACFTransitionSubsystem> S;
		FDelegateHandle Binding;
		TArray<TPair<FACFTransitionHandle, EACFTransitionEvent>> Events;
		TFunction<void(FACFTransitionHandle, EACFTransitionEvent)> React; // the listener under test (native delegate)

		explicit FAudit(UACFTransitionSubsystem* Sub) : S(Sub)
		{
			Binding = Sub->OnTransitionEventNative.AddLambda([this](FACFTransitionHandle H, EACFTransitionEvent E)
			{
				Events.Emplace(H, E);
				if (React)
				{
					React(H, E);
				}
			});
		}
		~FAudit()
		{
			if (UACFTransitionSubsystem* Sub = S.Get())
			{
				Sub->OnTransitionEventNative.Remove(Binding);
			}
		}
		FString Of(FACFTransitionHandle H) const
		{
			FString Out;
			for (const auto& P : Events)
			{
				if (P.Key == H)
				{
					Out += (Out.IsEmpty() ? TEXT("") : TEXT(",")) + FString(ACFTransition::LexEvent(P.Value));
				}
			}
			return Out;
		}
		int32 IndexOf(FACFTransitionHandle H, EACFTransitionEvent E) const
		{
			return Events.IndexOfByPredicate([&](const auto& P) { return P.Key == H && P.Value == E; });
		}
		/** At most one terminal event per handle and nothing after it; with bAllEnded every started handle has one. */
		void Check(FAutomationTestBase& Test, const TCHAR* What, bool bAllEnded) const
		{
			TMap<int32, int32> Terminals;
			TSet<int32> Started;
			TSet<int32> Ended;
			int32 Problems = 0;
			for (const auto& P : Events)
			{
				const int32 Id = P.Key.GetId();
				if (Ended.Contains(Id))
				{
					++Problems;
					Test.AddError(FString::Printf(TEXT("%s: %s sent %s after its terminal event."), What, *P.Key.ToString(), ACFTransition::LexEvent(P.Value)));
				}
				if (P.Value == EACFTransitionEvent::Started)
				{
					Started.Add(Id);
				}
				if (ACFTransition::IsTerminal(P.Value))
				{
					Ended.Add(Id);
				}
			}
			if (bAllEnded)
			{
				for (int32 Id : Started)
				{
					if (!Ended.Contains(Id))
					{
						++Problems;
						Test.AddError(FString::Printf(TEXT("%s: handle %d started but never ended."), What, Id));
					}
				}
			}
			Test.TestEqual(*FString::Printf(TEXT("%s: terminal-event contract"), What), Problems, 0);
		}
	};

	/** Synchronous tests: covered frames are presented frames, which do not advance inside one test call. */
	struct FSyncSettings
	{
		int32 OldCovered = 0;
		bool bOldCapture = true;
		FSyncSettings()
		{
			UACFTransitionProjectSettings* P = GetMutableDefault<UACFTransitionProjectSettings>();
			OldCovered = P->CoveredFrames;
			bOldCapture = P->bCaptureScreen;
			P->CoveredFrames = 0;
			P->bCaptureScreen = false;
		}
		~FSyncSettings()
		{
			UACFTransitionProjectSettings* P = GetMutableDefault<UACFTransitionProjectSettings>();
			P->CoveredFrames = OldCovered;
			P->bCaptureScreen = bOldCapture;
		}
	};

	static void Run(UACFTransitionSubsystem* S, TFunctionRef<bool()> Until, int32 MaxTicks = 400, float Dt = 0.02f)
	{
		for (int32 i = 0; i < MaxTicks && !Until(); ++i)
		{
			S->AdvanceForTest(Dt);
		}
	}

	static bool Ended(UACFTransitionSubsystem* S, FACFTransitionHandle H)
	{
		const FACFTransitionInfo Info = S->GetTransitionInfo(H);
		return !Info.bActive && !Info.bQueued && ACFTransition::IsTerminal(Info.LastEvent);
	}

	static FACFTransitionOptions Opt(EACFTransitionConcurrency C = EACFTransitionConcurrency::Interrupt, bool bLock = false)
	{
		FACFTransitionOptions O;
		O.Concurrency = C;
		O.bLockInput = bLock;
		return O;
	}

	static UACFTransitionSubsystem* Begin(FAutomationTestBase& Test)
	{
		UACFTransitionSubsystem* S = FindSubsystem();
		if (!S)
		{
			Test.AddWarning(TEXT("Skipped: no game instance (run with -game)."));
			return nullptr;
		}
		S->ClearScreen();
		S->AdvanceForTest(0.f); // deferred events of earlier tests
		return S;
	}

	static void CheckClean(FAutomationTestBase& Test, UACFTransitionSubsystem* S, const TCHAR* What)
	{
		Test.TestFalse(*FString::Printf(TEXT("%s: nothing active"), What), S->IsTransitionActive());
		Test.TestFalse(*FString::Printf(TEXT("%s: overlay removed"), What), S->IsOverlayAttached());
		Test.TestFalse(*FString::Printf(TEXT("%s: no capture"), What), S->HasCapture() || S->IsCapturePending());
		Test.TestEqual(*FString::Printf(TEXT("%s: no input lock"), What), S->GetLockedControllerCount(), 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: no waiting request"), What), S->GetWaitingRequestCount(), 0);
	}
}

// ------------------------------------------------------------------------------------------------ re-entrancy

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionReentryInterruptTest, "ActionCameraFramework.Transitions.Reentry.InterruptListener", ACFTransitionTest::Flags)
bool FACFTransitionReentryInterruptTest::RunTest(const FString&)
{
	using namespace ACFTransitionRobust;
	UACFTransitionSubsystem* S = Begin(*this);
	if (!S) { return true; }
	FSyncSettings Sync;
	FAudit Audit(S);

	// A plays; B interrupts A; A's Interrupted listener requests C (Interrupt).
	const FACFTransitionHandle A = S->PlayClose(Settings(EACFTransitionEffect::HorizontalWipe, 1.f), Opt(EACFTransitionConcurrency::Interrupt, true));
	S->AdvanceForTest(0.2f);
	FACFTransitionHandle C;
	Audit.React = [&](FACFTransitionHandle H, EACFTransitionEvent E)
	{
		if (H == A && E == EACFTransitionEvent::Interrupted)
		{
			TestTrue(TEXT("The listener runs while the event is sent"), S->IsSendingEvent());
			C = S->PlayClose(Settings(EACFTransitionEffect::DiagonalWipe, 1.f), Opt());
			TestTrue(TEXT("C waits (not applied inside the event)"), S->GetTransitionInfo(C).bQueued);
		}
	};
	const FACFTransitionHandle B = S->PlayClose(Settings(EACFTransitionEffect::BlackFade, 1.f), Opt());
	Audit.React = nullptr;
	TestTrue(TEXT("C was requested"), C.IsValid());
	TestEqual(TEXT("A"), Audit.Of(A), FString(TEXT("Started,Interrupted")));
	TestEqual(TEXT("B started, then C (requested after B) interrupted it"), Audit.Of(B), FString(TEXT("Started,Interrupted")));
	TestEqual(TEXT("C started"), Audit.Of(C), FString(TEXT("Started")));
	TestTrue(TEXT("C is the active handle (not overwritten by B)"), S->GetActiveHandle() == C);
	TestTrue(TEXT("Order: A Interrupted < B Started < B Interrupted < C Started"),
		Audit.IndexOf(A, EACFTransitionEvent::Interrupted) < Audit.IndexOf(B, EACFTransitionEvent::Started)
		&& Audit.IndexOf(B, EACFTransitionEvent::Started) < Audit.IndexOf(B, EACFTransitionEvent::Interrupted)
		&& Audit.IndexOf(B, EACFTransitionEvent::Interrupted) < Audit.IndexOf(C, EACFTransitionEvent::Started));
	TestEqual(TEXT("A's input lock given back"), S->GetLockedControllerCount(), 0);
	Run(S, [&]() { return Ended(S, C); });
	TestEqual(TEXT("C ends"), Audit.Of(C), FString(TEXT("Started,Covered,Finished")));

	// Cancel of the request being started, from the Interrupted listener: it ends Cancelled without Started.
	const FACFTransitionHandle D = S->PlayOpen(Settings(EACFTransitionEffect::BlackFade, 1.f, EACFTransitionPhase::Open), Opt());
	FACFTransitionHandle E2;
	Audit.React = [&](FACFTransitionHandle H, EACFTransitionEvent E)
	{
		if (H == D && E == EACFTransitionEvent::Interrupted)
		{
			// E2 is not returned yet: handles are numbered in request order, so it is the next one.
			TestTrue(TEXT("Cancel of the starting request returns true"), S->Cancel(FACFTransitionHandle(D.GetId() + 1), true));
		}
	};
	E2 = S->PlayOpen(Settings(EACFTransitionEffect::CrossDissolve, 1.f, EACFTransitionPhase::Open), Opt());
	Audit.React = nullptr;
	TestEqual(TEXT("E2 cancelled before it started"), Audit.Of(E2), FString(TEXT("Cancelled")));
	TestFalse(TEXT("Nothing active after the cancelled start"), S->IsTransitionActive());

	S->ClearScreen();
	S->AdvanceForTest(0.f);
	Audit.Check(*this, TEXT("InterruptListener"), true);
	CheckClean(*this, S, TEXT("InterruptListener"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionReentryBlueprintTest, "ActionCameraFramework.Transitions.Reentry.BlueprintListener", ACFTransitionTest::Flags)
bool FACFTransitionReentryBlueprintTest::RunTest(const FString&)
{
	using namespace ACFTransitionRobust;
	UACFTransitionSubsystem* S = Begin(*this);
	if (!S) { return true; }
	FSyncSettings Sync;
	FAudit Audit(S);
	UACFTransitionTestListener* Listener = NewObject<UACFTransitionTestListener>();
	Listener->AddToRoot();
	S->OnTransitionEvent.AddDynamic(Listener, &UACFTransitionTestListener::OnTransitionEvent);

	// The same case through the Blueprint (dynamic) delegate, which is sent after the native one.
	const FACFTransitionHandle A = S->PlayClose(Settings(EACFTransitionEffect::HorizontalWipe, 1.f), Opt());
	S->AdvanceForTest(0.2f);
	FACFTransitionHandle C;
	Listener->Callback = [&](FACFTransitionHandle H, EACFTransitionEvent E)
	{
		if (H == A && E == EACFTransitionEvent::Interrupted)
		{
			C = S->PlayClose(Settings(EACFTransitionEffect::DiagonalWipe, 1.f), Opt());
		}
	};
	const FACFTransitionHandle B = S->PlayClose(Settings(EACFTransitionEffect::BlackFade, 1.f), Opt());
	TestEqual(TEXT("B interrupted by C"), Audit.Of(B), FString(TEXT("Started,Interrupted")));
	TestTrue(TEXT("C active"), C.IsValid() && S->GetActiveHandle() == C);

	// Finished -> the next transition from the Blueprint listener (Close finished -> Open).
	FACFTransitionHandle Open;
	Listener->Callback = [&](FACFTransitionHandle H, EACFTransitionEvent E)
	{
		if (H == C && E == EACFTransitionEvent::Finished)
		{
			Open = S->PlayOpen(Settings(EACFTransitionEffect::CrossDissolve, 0.3f, EACFTransitionPhase::Open), Opt());
		}
	};
	Run(S, [&]() { return Open.IsValid(); });
	TestTrue(TEXT("Open requested from Finished"), Open.IsValid());
	TestTrue(TEXT("Open active right after C finished"), S->GetActiveHandle() == Open);
	TestTrue(TEXT("Open starts covered (continues the held cover)"), S->GetCoverage() >= 0.999f);
	TestTrue(TEXT("C Finished before Open Started"), Audit.IndexOf(C, EACFTransitionEvent::Finished) < Audit.IndexOf(Open, EACFTransitionEvent::Started));
	Listener->Callback = nullptr;
	Run(S, [&]() { return Ended(S, Open); });
	TestEqual(TEXT("Open events"), Audit.Of(Open), FString(TEXT("Started,Finished")));

	S->OnTransitionEvent.RemoveDynamic(Listener, &UACFTransitionTestListener::OnTransitionEvent);
	Listener->RemoveFromRoot();
	S->ClearScreen();
	S->AdvanceForTest(0.f);
	Audit.Check(*this, TEXT("BlueprintListener"), true);
	CheckClean(*this, S, TEXT("BlueprintListener"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionReentryCancelClearTest, "ActionCameraFramework.Transitions.Reentry.CancelledAndClearScreen", ACFTransitionTest::Flags)
bool FACFTransitionReentryCancelClearTest::RunTest(const FString&)
{
	using namespace ACFTransitionRobust;
	UACFTransitionSubsystem* S = Begin(*this);
	if (!S) { return true; }
	FSyncSettings Sync;
	FAudit Audit(S);

	// Cancelled -> a new transition: it starts on the screen as it is (an Open continues from the coverage).
	const FACFTransitionHandle A = S->PlayClose(Settings(EACFTransitionEffect::InkSpread, 1.f), Opt());
	S->AdvanceForTest(0.4f);
	const float Before = S->GetCoverage();
	FACFTransitionHandle B;
	Audit.React = [&](FACFTransitionHandle H, EACFTransitionEvent E)
	{
		if (H == A && E == EACFTransitionEvent::Cancelled)
		{
			B = S->PlayOpen(Settings(EACFTransitionEffect::InkSpread, 0.5f, EACFTransitionPhase::Open), Opt());
		}
	};
	TestTrue(TEXT("Cancel A"), S->Cancel(A, true));
	Audit.React = nullptr;
	TestTrue(TEXT("B active after Cancel returned"), B.IsValid() && S->GetActiveHandle() == B);
	TestNearlyEqual(TEXT("B continues from A's coverage (the clear request does not remove B's screen)"), S->GetCoverage(), Before, 0.02f);
	TestTrue(TEXT("Overlay kept for B"), S->IsOverlayAttached() || GUsingNullRHI);
	Run(S, [&]() { return Ended(S, B); });

	// ClearScreen: the listeners of its Cancelled events start after the screen is cleared, from a clear screen.
	const FACFTransitionHandle C = S->PlayClose(Settings(EACFTransitionEffect::InkSpread, 1.f), Opt());
	FACFTransitionOptions Q = Opt(EACFTransitionConcurrency::Queue);
	const FACFTransitionHandle CQ = S->PlayOpen(Settings(EACFTransitionEffect::BlackFade, 0.2f, EACFTransitionPhase::Open), Q);
	S->AdvanceForTest(0.4f);
	FACFTransitionHandle D;
	Audit.React = [&](FACFTransitionHandle H, EACFTransitionEvent E)
	{
		if (H == C && E == EACFTransitionEvent::Cancelled)
		{
			D = S->PlayClose(Settings(EACFTransitionEffect::CircleIris, 1.f), Opt());
		}
	};
	S->ClearScreen();
	Audit.React = nullptr;
	TestEqual(TEXT("Queued request cancelled by ClearScreen"), Audit.Of(CQ), FString(TEXT("Cancelled")));
	TestTrue(TEXT("D (requested by a Cancelled listener) active after ClearScreen"), D.IsValid() && S->GetActiveHandle() == D);
	TestEqual(TEXT("D starts from the clear screen"), S->GetCoverage(), 0.f);
	TestTrue(TEXT("D Started after C Cancelled"), Audit.IndexOf(C, EACFTransitionEvent::Cancelled) < Audit.IndexOf(D, EACFTransitionEvent::Started));

	// ClearScreen from an Interrupted listener cancels the interrupting request too (requested before ClearScreen).
	FACFTransitionHandle F;
	Audit.React = [&](FACFTransitionHandle H, EACFTransitionEvent E)
	{
		if (H == D && E == EACFTransitionEvent::Interrupted)
		{
			S->ClearScreen();
		}
	};
	F = S->PlayClose(Settings(EACFTransitionEffect::BlackFade, 1.f), Opt());
	Audit.React = nullptr;
	TestEqual(TEXT("D interrupted"), Audit.Of(D), FString(TEXT("Started,Interrupted")));
	TestEqual(TEXT("F cancelled by ClearScreen before it started"), Audit.Of(F), FString(TEXT("Cancelled")));
	TestFalse(TEXT("Nothing active after ClearScreen in the listener"), S->IsTransitionActive());
	TestEqual(TEXT("Clear"), S->GetCoverage(), 0.f);

	// Cancel from the Started listener of the handle itself.
	FACFTransitionHandle G; // the handle is not returned yet when Started is sent: cancel the next started handle
	Audit.React = [&](FACFTransitionHandle H, EACFTransitionEvent E)
	{
		if (E == EACFTransitionEvent::Started && !G.IsValid())
		{
			G = H;
			S->Cancel(H, true);
		}
	};
	const FACFTransitionHandle G2 = S->PlayClose(Settings(EACFTransitionEffect::BlackFade, 1.f), Opt(EACFTransitionConcurrency::Interrupt, true));
	Audit.React = nullptr;
	TestEqual(TEXT("Cancelled from its own Started"), Audit.Of(G2), FString(TEXT("Started,Cancelled")));
	TestFalse(TEXT("Not active"), S->IsTransitionActive());

	S->ClearScreen();
	S->AdvanceForTest(0.f);
	Audit.Check(*this, TEXT("CancelledAndClearScreen"), true);
	CheckClean(*this, S, TEXT("CancelledAndClearScreen"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionReentryQueueMixTest, "ActionCameraFramework.Transitions.Reentry.QueueInterruptMix", ACFTransitionTest::Flags)
bool FACFTransitionReentryQueueMixTest::RunTest(const FString&)
{
	using namespace ACFTransitionRobust;
	UACFTransitionSubsystem* S = Begin(*this);
	if (!S) { return true; }
	FSyncSettings Sync;
	FAudit Audit(S);

	const FACFTransitionHandle A = S->PlayClose(Settings(EACFTransitionEffect::HorizontalWipe, 0.5f), Opt());
	const FACFTransitionHandle Q1 = S->PlayOpen(Settings(EACFTransitionEffect::BlackFade, 0.2f, EACFTransitionPhase::Open), Opt(EACFTransitionConcurrency::Queue));
	S->AdvanceForTest(0.1f);
	FACFTransitionHandle Q2, C, R;
	Audit.React = [&](FACFTransitionHandle H, EACFTransitionEvent E)
	{
		if (H == A && E == EACFTransitionEvent::Interrupted)
		{
			Q2 = S->PlayClose(Settings(EACFTransitionEffect::BlackFade, 0.2f), Opt(EACFTransitionConcurrency::Queue));
			C = S->PlayClose(Settings(EACFTransitionEffect::DiagonalWipe, 0.3f), Opt());
			R = S->PlayClose(Settings(EACFTransitionEffect::BlackFade, 0.2f), Opt(EACFTransitionConcurrency::Reject));
		}
	};
	const FACFTransitionHandle B = S->PlayClose(Settings(EACFTransitionEffect::CircleIris, 0.3f), Opt());
	Audit.React = nullptr;
	TestTrue(TEXT("C active"), S->GetActiveHandle() == C);
	TestEqual(TEXT("Queue: Q1 then Q2"), S->GetQueueLength(), 2);
	TestEqual(TEXT("B interrupted by C"), Audit.Of(B), FString(TEXT("Started,Interrupted")));
	TestEqual(TEXT("R rejected"), S->GetTransitionInfo(R).LastEvent, EACFTransitionEvent::Failed);
	Run(S, [&]() { return Ended(S, Q2); });
	TestEqual(TEXT("C"), Audit.Of(C), FString(TEXT("Started,Covered,Finished")));
	TestEqual(TEXT("Q1"), Audit.Of(Q1), FString(TEXT("Started,Finished")));
	TestEqual(TEXT("Q2"), Audit.Of(Q2), FString(TEXT("Started,Covered,Finished")));
	TestEqual(TEXT("R (event sent on the next tick)"), Audit.Of(R), FString(TEXT("Failed")));
	TestTrue(TEXT("Order C -> Q1 -> Q2"),
		Audit.IndexOf(C, EACFTransitionEvent::Finished) < Audit.IndexOf(Q1, EACFTransitionEvent::Started)
		&& Audit.IndexOf(Q1, EACFTransitionEvent::Finished) < Audit.IndexOf(Q2, EACFTransitionEvent::Started));

	S->ClearScreen();
	S->AdvanceForTest(0.f);
	Audit.Check(*this, TEXT("QueueInterruptMix"), true);
	CheckClean(*this, S, TEXT("QueueInterruptMix"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionReentryRandomTest, "ActionCameraFramework.Transitions.Reentry.RandomListeners", ACFTransitionTest::Flags)
bool FACFTransitionReentryRandomTest::RunTest(const FString&)
{
	using namespace ACFTransitionRobust;
	UACFTransitionSubsystem* S = Begin(*this);
	if (!S) { return true; }
	FSyncSettings Sync;
	FAudit Audit(S);
	UACFTransitionTestListener* Listener = NewObject<UACFTransitionTestListener>();
	Listener->AddToRoot();
	S->OnTransitionEvent.AddDynamic(Listener, &UACFTransitionTestListener::OnTransitionEvent);
	APlayerController* PC = FindGameWorld() ? FindGameWorld()->GetFirstPlayerController() : nullptr;
	const bool bMoveIgnoredBefore = PC && PC->IsMoveInputIgnored();

	TArray<EACFTransitionEffect> Effects;
	for (const FACFTransitionEffectInfo& Info : ACFTransition::GetAllEffects())
	{
		Effects.Add(Info.Effect);
	}
	FRandomStream Rng(7101);
	TArray<FACFTransitionHandle> Known;
	int32 Budget = 1500;
	auto Act = [&](bool bFromListener)
	{
		if (--Budget <= 0) { return; }
		const float P = Rng.FRand();
		const float Limit = bFromListener ? 0.45f : 1.f; // listeners act on fewer events
		if (P > Limit) { return; }
		const int32 Kind = Rng.RandRange(0, 9);
		if (Kind <= 4)
		{
			const EACFTransitionPhase Phase = Rng.RandRange(0, 1) ? EACFTransitionPhase::Open : EACFTransitionPhase::Close;
			const FACFTransitionSettings Set = Settings(Effects[Rng.RandRange(0, Effects.Num() - 1)], Rng.FRandRange(0.f, 0.4f), Phase);
			const FACFTransitionOptions O = Opt(static_cast<EACFTransitionConcurrency>(Rng.RandRange(0, 2)), Rng.RandRange(0, 3) == 0);
			Known.Add(Phase == EACFTransitionPhase::Open ? S->PlayOpen(Set, O) : S->PlayClose(Set, O));
		}
		else if (Kind <= 6 && !Known.IsEmpty())
		{
			S->Cancel(Known[Rng.RandRange(0, Known.Num() - 1)], Rng.RandRange(0, 1) == 1);
		}
		else if (Kind == 7)
		{
			S->Skip(S->GetActiveHandle());
		}
		else if (Kind == 8 && Rng.RandRange(0, 3) == 0)
		{
			S->ClearScreen();
		}
	};
	Audit.React = [&](FACFTransitionHandle, EACFTransitionEvent) { Act(true); };
	Listener->Callback = [&](FACFTransitionHandle, EACFTransitionEvent) { Act(true); };
	for (int32 Step = 0; Step < 600 && Budget > 0; ++Step)
	{
		Act(false);
		S->AdvanceForTest(Rng.FRandRange(0.f, 0.08f));
	}
	Audit.React = nullptr;
	Listener->Callback = nullptr;
	S->OnTransitionEvent.RemoveDynamic(Listener, &UACFTransitionTestListener::OnTransitionEvent);
	Listener->RemoveFromRoot();
	S->ClearScreen();
	S->AdvanceForTest(0.f);
	AddInfo(FString::Printf(TEXT("%d events of %d handles."), Audit.Events.Num(), Known.Num()));
	TestTrue(TEXT("Many events"), Audit.Events.Num() > 300);
	Audit.Check(*this, TEXT("RandomListeners"), true);
	CheckClean(*this, S, TEXT("RandomListeners"));
	if (PC)
	{
		TestEqual(TEXT("The player's input is as before"), PC->IsMoveInputIgnored(), bMoveIgnoredBefore);
	}
	return true;
}

// ------------------------------------------------------------------------------------------------ GC

namespace ACFTransitionRobust
{
	/** A transient material instance of M_ACFTransition (a valid MaterialOverride: the view makes its own MID of it). */
	static UMaterialInterface* MakeTransientMaterial(UMaterialInterface* Base)
	{
#if WITH_EDITOR
		if (Base)
		{
			UMaterialInstanceConstant* M = NewObject<UMaterialInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transient);
			M->SetParentEditorOnly(Base);
			M->PostEditChange();
			return M;
		}
#endif
		return nullptr;
	}

	/** Transient objects referenced only by one settings struct. */
	struct FTransientRefs
	{
		TWeakObjectPtr<UCurveFloat> Curve;
		TWeakObjectPtr<UTexture2D> Texture;
		TWeakObjectPtr<UMaterialInterface> Material;

		FACFTransitionSettings Make(EACFTransitionEffect Effect, float Duration, EACFTransitionPhase Phase)
		{
			FACFTransitionSettings Set = Settings(Effect, Duration, Phase);
			UCurveFloat* C = NewObject<UCurveFloat>(GetTransientPackage(), NAME_None, RF_Transient);
			C->FloatCurve.AddKey(0.f, 0.f);
			C->FloatCurve.AddKey(1.f, 1.f);
			UTexture2D* T = UTexture2D::CreateTransient(4, 4);
			UMaterialInterface* Base = FACFTransitionView::LoadDefaultMaterial();
			UMaterialInterface* M = MakeTransientMaterial(Base);
			Set.Easing = EACFTransitionEasing::Curve;
			Set.Curve = C;
			Set.OverlayTexture = T;
			Set.MaterialOverride = M;
			Curve = C;
			Texture = T;
			Material = M;
			bHasMaterial = M != nullptr;
			return Set;
		}
		bool bHasMaterial = false;
		bool AllAlive() const { return Curve.IsValid() && Texture.IsValid() && (Material.IsValid() || !bHasMaterial); }
		bool AllGone() const { return !Curve.IsValid() && !Texture.IsValid() && !Material.IsValid(); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionGCTest, "ActionCameraFramework.Transitions.GC.ActiveQueuedWaitingHeld", ACFTransitionTest::Flags)
bool FACFTransitionGCTest::RunTest(const FString&)
{
	using namespace ACFTransitionRobust;
	UACFTransitionSubsystem* S = Begin(*this);
	if (!S) { return true; }
	FSyncSettings Sync;
	FAudit Audit(S);
	auto GC = []() { CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); };

	// Active Close + a queued Open, each with its own transient objects.
	FTransientRefs ActiveRefs, QueuedRefs;
	const FACFTransitionHandle A = S->PlayClose(ActiveRefs.Make(EACFTransitionEffect::NetSweep, 0.5f, EACFTransitionPhase::Close), Opt());
	const FACFTransitionHandle Q = S->PlayOpen(QueuedRefs.Make(EACFTransitionEffect::NetSweep, 0.3f, EACFTransitionPhase::Open), Opt(EACFTransitionConcurrency::Queue));
	S->AdvanceForTest(0.1f);
	GC();
	TestTrue(TEXT("Active request: curve / texture / material survive a GC"), ActiveRefs.AllAlive());
	TestTrue(TEXT("Queued request: curve / texture / material survive a GC"), QueuedRefs.AllAlive());
	Run(S, [&]() { return S->GetActiveHandle() == Q; });
	GC();
	TestTrue(TEXT("Queued request after it started: alive"), QueuedRefs.AllAlive());
	Run(S, [&]() { return Ended(S, Q); });
	S->AdvanceForTest(0.f);
	GC();
	TestTrue(TEXT("Ended transitions keep nothing (active)"), ActiveRefs.AllGone());
	TestTrue(TEXT("Ended transitions keep nothing (queued)"), QueuedRefs.AllGone());

	// A held cover: the drawn settings stay alive while the screen is covered, and are released by the Open.
	FTransientRefs HeldRefs;
	const FACFTransitionHandle H = S->PlayClose(HeldRefs.Make(EACFTransitionEffect::BlackFade, 0.1f, EACFTransitionPhase::Close), Opt());
	Run(S, [&]() { return Ended(S, H); });
	GC();
	TestTrue(TEXT("Held cover: drawn settings survive a GC"), HeldRefs.AllAlive());
	S->ClearScreen();
	GC();
	TestTrue(TEXT("Held cover cleared: released"), HeldRefs.AllGone());

	// A level transition waiting for the ready signal (after the load): its Open settings survive.
	FTransientRefs OpenRefs;
	FACFTransitionOptions O;
	O.bManualLoad = true;
	O.bWaitForReadySignal = true;
	const FACFTransitionHandle L = S->PlayLevelTransition(Settings(EACFTransitionEffect::BlackFade, 0.1f), OpenRefs.Make(EACFTransitionEffect::NetSweep, 0.2f, EACFTransitionPhase::Open), O, NAME_None, FString());
	Run(S, [&]() { return Audit.IndexOf(L, EACFTransitionEvent::Covered) != INDEX_NONE; });
	TestTrue(TEXT("Load completed"), S->NotifyLoadCompleted(L));
	for (int32 i = 0; i < 10; ++i) { S->AdvanceForTest(0.02f); }
	GC();
	TestTrue(TEXT("Waiting for the ready signal: Open settings survive a GC"), OpenRefs.AllAlive());
	TestTrue(TEXT("Ready"), S->NotifyWorldReady(L));
	Run(S, [&]() { return Ended(S, L); });
	TestEqual(TEXT("Level events"), Audit.Of(L), FString(TEXT("Started,Covered,LoadStarted,LoadCompleted,Finished")));
	S->AdvanceForTest(0.f);
	GC();
	TestTrue(TEXT("Level transition ended: released"), OpenRefs.AllGone());

	Audit.Check(*this, TEXT("GC"), true);
	CheckClean(*this, S, TEXT("GC"));
	return true;
}

// ------------------------------------------------------------------------------------------------ input lock

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionInputLockTest, "ActionCameraFramework.Transitions.InputLock.Lifetime", ACFTransitionTest::Flags)
bool FACFTransitionInputLockTest::RunTest(const FString&)
{
	using namespace ACFTransitionRobust;
	UACFTransitionSubsystem* S = Begin(*this);
	if (!S) { return true; }
	UWorld* World = FindGameWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		AddWarning(TEXT("Skipped: no player controller."));
		return true;
	}
	FSyncSettings Sync;
	FAudit Audit(S);
	auto Locked = [](const APlayerController* P) { return P->IsMoveInputIgnored() && P->IsLookInputIgnored(); };
	auto Free = [](const APlayerController* P) { return !P->IsMoveInputIgnored() && !P->IsLookInputIgnored(); };
	TestTrue(TEXT("Start: the player's input is free"), Free(PC));

	// Another system's lock is not given back by the transition.
	PC->SetIgnoreMoveInput(true);
	const FACFTransitionHandle A = S->PlayClose(Settings(EACFTransitionEffect::BlackFade, 0.1f), Opt(EACFTransitionConcurrency::Interrupt, true));
	TestTrue(TEXT("Locked"), Locked(PC));
	S->AdvanceForTest(0.02f);
	TestEqual(TEXT("One lock per controller (not taken again every tick)"), S->GetLockedControllerCount(), 1);
	Run(S, [&]() { return Ended(S, A); });
	TestTrue(TEXT("The other system's move lock survives"), PC->IsMoveInputIgnored());
	TestFalse(TEXT("Look lock given back"), PC->IsLookInputIgnored());
	PC->SetIgnoreMoveInput(false);
	TestTrue(TEXT("Free after the other system's unlock"), Free(PC));
	S->ClearScreen();

	// A level transition waiting for the ready signal: a controller spawned after the load and a controller whose
	// counters were reset (ClientRestart on a respawn) are locked while it waits; all are free afterwards.
	FACFTransitionOptions O = Opt(EACFTransitionConcurrency::Interrupt, true);
	O.bManualLoad = true;
	O.bWaitForReadySignal = true;
	O.TimeoutSeconds = 60.f;
	const FACFTransitionHandle L = S->PlayLevelTransition(Settings(EACFTransitionEffect::BlackFade, 0.1f), Settings(EACFTransitionEffect::BlackFade, 0.1f, EACFTransitionPhase::Open), O, NAME_None, FString());
	Run(S, [&]() { return Audit.IndexOf(L, EACFTransitionEvent::Covered) != INDEX_NONE; });
	S->NotifyLoadCompleted(L);
	S->AdvanceForTest(0.02f);
	// A second local player joins (its controller is spawned now, after the transition started).
	APlayerController* Added = UGameplayStatics::CreatePlayer(World, -1, true);
	if (!Added)
	{
		AddInfo(TEXT("No second local player here; the added-controller case is not checked."));
	}
	for (int32 i = 0; i < 5; ++i) { S->AdvanceForTest(0.5f); } // a long wait for the ready signal
	if (Added)
	{
		TestTrue(TEXT("Controller added during the wait is locked"), Locked(Added));
	}
	PC->ResetIgnoreInputFlags(); // what ClientRestart does
	S->AdvanceForTest(0.02f);
	TestTrue(TEXT("Controller whose lock was reset is locked again"), Locked(PC));
	TestTrue(TEXT("Still waiting"), S->GetActiveHandle() == L && Audit.IndexOf(L, EACFTransitionEvent::LoadCompleted) == INDEX_NONE);
	S->NotifyWorldReady(L);
	Run(S, [&]() { return Ended(S, L); });
	TestTrue(TEXT("Player free after the level transition"), Free(PC));
	if (Added)
	{
		TestTrue(TEXT("Added controller free after the level transition"), Free(Added));
		UGameplayStatics::RemovePlayer(Added, true);
	}

	// Timeout, Cancel and Interrupt give the lock back.
	FACFTransitionOptions T = Opt(EACFTransitionConcurrency::Interrupt, true);
	T.bManualLoad = true;
	T.TimeoutSeconds = 0.3f;
	const FACFTransitionHandle TL = S->PlayLevelTransition(Settings(EACFTransitionEffect::BlackFade, 0.1f), Settings(EACFTransitionEffect::BlackFade, 0.1f, EACFTransitionPhase::Open), T, NAME_None, FString());
	TestTrue(TEXT("Locked (timeout case)"), Locked(PC));
	Run(S, [&]() { return Ended(S, TL); });
	TestEqual(TEXT("Timeout -> Failed"), Audit.Of(TL), FString(TEXT("Started,Covered,Failed")));
	TestTrue(TEXT("Free after the timeout"), Free(PC));
	const FACFTransitionHandle X = S->PlayClose(Settings(EACFTransitionEffect::BlackFade, 1.f), Opt(EACFTransitionConcurrency::Interrupt, true));
	S->AdvanceForTest(0.1f);
	S->Cancel(X, true);
	TestTrue(TEXT("Free after Cancel"), Free(PC));
	S->PlayClose(Settings(EACFTransitionEffect::BlackFade, 1.f), Opt(EACFTransitionConcurrency::Interrupt, true));
	S->AdvanceForTest(0.1f);
	const FACFTransitionHandle Y = S->PlayOpen(Settings(EACFTransitionEffect::BlackFade, 0.1f, EACFTransitionPhase::Open), Opt());
	TestTrue(TEXT("Free after an Interrupt by an unlocked transition"), Free(PC));
	Run(S, [&]() { return Ended(S, Y); });

	S->ClearScreen();
	S->AdvanceForTest(0.f);
	Audit.Check(*this, TEXT("InputLock"), true);
	CheckClean(*this, S, TEXT("InputLock"));
	TestTrue(TEXT("End: the player's input is free"), Free(PC));
	return true;
}

// ------------------------------------------------------------------------------------------------ capture: Skip

namespace ACFTransitionRobust
{
	static void SetFailCapture(int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("ACF.Transition.Debug.FailCapture")))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionSkipPendingTest, "ActionCameraFramework.Transitions.Capture.SkipWhilePending", ACFTransitionTest::Flags)
bool FACFTransitionSkipPendingTest::RunTest(const FString&)
{
	using namespace ACFTransitionRobust;
	UACFTransitionSubsystem* S = Begin(*this);
	if (!S) { return true; }
	const bool bRendered = !GUsingNullRHI;
	TSharedPtr<FAudit> Audit = MakeShared<FAudit>(S);
	TWeakObjectPtr<UACFTransitionSubsystem> Weak = S;

	// Same frame: PlayClose -> Skip -> one tick. The Close must not wait for the GPU copy (nor read the screen back).
	FACFTransitionHandle H = S->PlayClose(Settings(EACFTransitionEffect::ZoomRush, 0.5f), Opt());
	const bool bWasPending = S->IsCapturePending();
	if (bRendered)
	{
		TestTrue(TEXT("Rendered: the GPU capture is pending after PlayClose"), bWasPending);
	}
	TestTrue(TEXT("Skip"), S->Skip(H));
	TestFalse(TEXT("Skip drops the pending capture"), S->IsCapturePending());
	S->AdvanceForTest(0.016f);
	TestEqual(TEXT("Covered on the next tick after Skip"), S->GetCoverage(), 1.f);
	if (bWasPending)
	{
		TestTrue(TEXT("No read back for Skip"), S->DidLastCaptureFallBack() && !S->HasCapture());
	}

	// Skip -> Cancel, Skip -> Interrupt, Skip with a failing capture: all clean.
	S->ClearScreen();
	H = S->PlayClose(Settings(EACFTransitionEffect::ZoomRush, 0.5f), Opt(EACFTransitionConcurrency::Interrupt, true));
	S->Skip(H);
	TestTrue(TEXT("Cancel after Skip"), S->Cancel(H, true));
	TestEqual(TEXT("Skip then Cancel events"), Audit->Of(H), FString(TEXT("Started,Cancelled")));
	CheckClean(*this, S, TEXT("Skip then Cancel"));
	H = S->PlayClose(Settings(EACFTransitionEffect::MosaicTransition, 0.5f), Opt());
	S->Skip(H);
	const FACFTransitionHandle O = S->PlayOpen(Settings(EACFTransitionEffect::BlackFade, 0.05f, EACFTransitionPhase::Open), Opt());
	TestEqual(TEXT("Skip then Interrupt"), Audit->Of(H), FString(TEXT("Started,Interrupted")));
	Run(S, [&]() { return Ended(S, O); });
	CheckClean(*this, S, TEXT("Skip then Interrupt"));
	SetFailCapture(1);
	H = S->PlayClose(Settings(EACFTransitionEffect::CrossDissolve, 0.5f), Opt());
	S->Skip(H);
	S->AdvanceForTest(0.016f);
	TestEqual(TEXT("Failed capture + Skip: covered"), S->GetCoverage(), 1.f);
	TestFalse(TEXT("Failed capture + Skip: no capture"), S->HasCapture());
	SetFailCapture(0);
	S->ClearScreen();

	// Over real frames: the skipped Close goes through the covered frames and Covered, then Finished.
	TSharedPtr<FACFTransitionHandle> Last = MakeShared<FACFTransitionHandle>();
	TSharedPtr<int32> Tries = MakeShared<int32>(0);
	TSharedPtr<bool> bSawReady = MakeShared<bool>(false);
	TArray<TFunction<bool()>> Steps;
	Steps.Add([=]()
	{
		*Last = Weak->PlayClose(Settings(EACFTransitionEffect::ZoomRush, 0.5f), Opt());
		Weak->Skip(*Last);
		return true;
	});
	Steps.Add([=, this]()
	{
		if (!ACFTransition::IsTerminal(Weak->GetTransitionInfo(*Last).LastEvent)) { return false; }
		TestEqual(TEXT("Skipped Close over frames"), Audit->Of(*Last), FString(TEXT("Started,Covered,Finished")));
		TestEqual(TEXT("Covered at the end"), Weak->GetCoverage(), 1.f);
		Weak->ClearScreen();
		return true;
	});
	// Skip when the copy is already on the GPU but not yet taken by the subsystem: the copy is used.
	Steps.Add([=, this]()
	{
		if (!bRendered) { return true; }
		if (*Tries >= 12)
		{
			AddInfo(TEXT("The Ready-before-resolve moment was not observed (the subsystem took the copy first); covered by the same-frame cases."));
			return true;
		}
		const int32 State = Weak->GetPendingCaptureState();
		if (State < 0)
		{
			Weak->ClearScreen();
			++*Tries;
			*Last = Weak->PlayClose(Settings(EACFTransitionEffect::CrossDissolve, 0.5f), Opt());
			return false;
		}
		FlushRenderingCommands(); // test only: let the render thread present the frame the copy waits for
		if (Weak->GetPendingCaptureState() != static_cast<int32>(FACFTransitionGpuCapture::EState::Ready))
		{
			return false;
		}
		*bSawReady = true;
		Weak->Skip(*Last);
		TestTrue(TEXT("Ready copy used by Skip"), Weak->HasCapture() && !Weak->DidLastCaptureFallBack());
		TestFalse(TEXT("No pending capture after Skip"), Weak->IsCapturePending());
		return true;
	});
	Steps.Add([=, this]()
	{
		if (*bSawReady && !ACFTransition::IsTerminal(Weak->GetTransitionInfo(*Last).LastEvent)) { return false; }
		Weak->ClearScreen();
		CheckClean(*this, Weak.Get(), TEXT("Skip over frames"));
		TestEqual(TEXT("No GPU capture request alive"), FACFTransitionGpuCapture::GetLiveCount(), 0);
		TestEqual(TEXT("No render thread listener left"), FACFTransitionGpuCapture::GetListenerCount(), 0);
		Audit->Check(*this, TEXT("SkipWhilePending"), true);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(ACFTransitionTest::FSteps(this, MoveTemp(Steps), [Weak, Audit]()
	{
		SetFailCapture(0);
		if (Weak.IsValid()) { Weak->ClearScreen(); }
	}, 30.0));
	return true;
}

// ------------------------------------------------------------------------------------------------ capture: render thread

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionCaptureLifetimeTest, "ActionCameraFramework.Transitions.Capture.RenderThreadLifetime", ACFTransitionTest::Flags)
bool FACFTransitionCaptureLifetimeTest::RunTest(const FString&)
{
	using namespace ACFTransitionRobust;
	UACFTransitionSubsystem* S = Begin(*this);
	if (!S) { return true; }
	UWorld* World = FindGameWorld();
	UGameViewportClient* VC = World ? World->GetGameViewport() : nullptr;
	struct FCtx
	{
		TArray<TWeakObjectPtr<UTexture>> Targets;
		TSharedPtr<FACFTransitionGpuCapture> Held;
		FIntRect RectBefore;
		int32 Frames = 0;
		int32 Round = 0;
		int32 ResizeW = 0, ResizeH = 0;
		FACFTransitionHandle Handle;
		bool bResized = false;
	};
	TSharedPtr<FCtx> Ctx = MakeShared<FCtx>();
	TSharedPtr<FAudit> Audit = MakeShared<FAudit>(S);
	TWeakObjectPtr<UACFTransitionSubsystem> Weak = S;

	// Direct requests: cancel right away, several times in the same frame, and drop.
	TSharedPtr<FACFTransitionGpuCapture> Cap = FACFTransitionView::RequestGpuCapture(VC);
	if (!Cap.IsValid())
	{
		AddInfo(TEXT("No GPU capture here (NullRHI / no window): only the bookkeeping is checked."));
		TestEqual(TEXT("No listener"), FACFTransitionGpuCapture::GetListenerCount(), 0);
		return true;
	}
	Ctx->Targets.Add(Cap->GetTexture());
	TestEqual(TEXT("One listener"), FACFTransitionGpuCapture::GetListenerCount(), 1);
	Cap->Cancel();
	Cap->Cancel();
	Cap->Cancel();
	TestEqual(TEXT("Cancel (3x): no listener"), FACFTransitionGpuCapture::GetListenerCount(), 0);
	TestEqual(TEXT("Cancelled request reports Failed"), Cap->GetState(), FACFTransitionGpuCapture::EState::Failed);
	Cap.Reset();
	// A request kept over frames (presented) and one dropped while the render thread may be copying.
	Ctx->Held = FACFTransitionView::RequestGpuCapture(VC);
	Ctx->Targets.Add(Ctx->Held->GetTexture());
	{
		TSharedPtr<FACFTransitionGpuCapture> Dropped = FACFTransitionView::RequestGpuCapture(VC);
		Ctx->Targets.Add(Dropped->GetTexture());
	}
	// Transitions: start + ClearScreen in the same frame, many times.
	for (int32 i = 0; i < 20; ++i)
	{
		S->PlayClose(Settings(i % 2 ? EACFTransitionEffect::ZoomRush : EACFTransitionEffect::CrossDissolve, 0.3f), Opt());
		S->ClearScreen();
	}
	TestEqual(TEXT("Burst: one request alive (the held one)"), FACFTransitionGpuCapture::GetLiveCount(), 1);

	TArray<TFunction<bool()>> Steps;
	Steps.Add([=, this]()
	{
		if (++Ctx->Frames < 4) { return false; }
		TestEqual(TEXT("Held request copied"), Ctx->Held->GetState(), FACFTransitionGpuCapture::EState::Ready);
		Ctx->Held.Reset();
		return true;
	});
	// Start / cancel across frames (the request is cancelled one frame later, while its copy may be in flight).
	Steps.Add([=, this]()
	{
		if (Ctx->Round >= 10) { return true; }
		if (Ctx->Handle.IsValid())
		{
			Weak->Cancel(Ctx->Handle, true);
			Ctx->Handle = FACFTransitionHandle();
			++Ctx->Round;
			return false;
		}
		Ctx->Handle = Weak->PlayClose(Settings(EACFTransitionEffect::ZoomRush, 0.3f), Opt());
		return false;
	});
	// Consecutive captures: each Close takes the copy of its own frame.
	Steps.Add([=, this]()
	{
		const FACFTransitionInfo Info = Weak->GetTransitionInfo(Ctx->Handle);
		if (Ctx->Handle.IsValid() && !ACFTransition::IsTerminal(Info.LastEvent)) { return false; }
		if (Ctx->Handle.IsValid())
		{
			TestFalse(*FString::Printf(TEXT("Consecutive capture %d without read back"), Ctx->Round), Weak->DidLastCaptureFallBack());
			Weak->ClearScreen();
		}
		if (++Ctx->Round > 15) { return true; }
		Ctx->Handle = Weak->PlayClose(Settings(EACFTransitionEffect::CrossDissolve, 0.1f), Opt());
		return false;
	});
	// Window resize while the copy is pending: the stale copy is not used (read back instead); the Close goes on.
	Steps.Add([=, this]()
	{
		UWorld* W = FindGameWorld();
		UGameViewportClient* V = W ? W->GetGameViewport() : nullptr;
		if (!V || !FACFTransitionView::GetViewportPixelRect(V, Ctx->RectBefore) || !V->GetWindow().IsValid() || V->GetWindow()->GetWindowMode() != EWindowMode::Windowed)
		{
			AddInfo(TEXT("Resize case skipped (no windowed game viewport)."));
			return true;
		}
		Ctx->ResizeW = Ctx->RectBefore.Width();
		Ctx->ResizeH = Ctx->RectBefore.Height();
		Ctx->Handle = Weak->PlayClose(Settings(EACFTransitionEffect::CrossDissolve, 0.3f), Opt());
		V->GetWindow()->Resize(FVector2D(Ctx->ResizeW - 96, Ctx->ResizeH - 54));
		Ctx->bResized = true;
		return true;
	});
	Steps.Add([=, this]()
	{
		if (!Ctx->bResized) { return true; }
		if (!ACFTransition::IsTerminal(Weak->GetTransitionInfo(Ctx->Handle).LastEvent)) { return false; }
		TestEqual(TEXT("Close during a resize ends normally"), Audit->Of(Ctx->Handle), FString(TEXT("Started,Covered,Finished")));
		AddInfo(FString::Printf(TEXT("Resize: %s."), Weak->DidLastCaptureFallBack() ? TEXT("the stale GPU copy was replaced by the read back") : TEXT("the copy was taken before the resize")));
		Weak->ClearScreen();
		if (UWorld* W = FindGameWorld(); W && W->GetGameViewport() && W->GetGameViewport()->GetWindow().IsValid())
		{
			W->GetGameViewport()->GetWindow()->Resize(FVector2D(Ctx->ResizeW, Ctx->ResizeH));
		}
		return true;
	});
	Steps.Add([=, this]()
	{
		Weak->ClearScreen();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		int32 Alive = 0;
		for (const TWeakObjectPtr<UTexture>& T : Ctx->Targets) { Alive += T.IsValid() ? 1 : 0; }
		TestEqual(TEXT("Capture render targets collected after use"), Alive, 0);
		TestEqual(TEXT("No GPU capture request alive"), FACFTransitionGpuCapture::GetLiveCount(), 0);
		TestEqual(TEXT("No render thread listener left"), FACFTransitionGpuCapture::GetListenerCount(), 0);
		CheckClean(*this, Weak.Get(), TEXT("RenderThreadLifetime"));
		Audit->Check(*this, TEXT("RenderThreadLifetime"), true);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(ACFTransitionTest::FSteps(this, MoveTemp(Steps), [Weak, Ctx]()
	{
		Ctx->Held.Reset();
		if (Weak.IsValid()) { Weak->ClearScreen(); }
	}, 60.0));
	return true;
}

#endif
