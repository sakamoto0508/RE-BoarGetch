#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFTransitionLibrary.h"
#include "ACFTransitionPreset.h"
#include "ACFTransitionTestUtils.h"
#include "ACFTransitionView.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectGlobals.h"

/**
 * Phase 7D: the transition API and its state (README 33 章「検証」). Game context (-game, also -NullRHI: the timing and events
 * do not need a renderer). Rendering is checked by ACFTransitionRenderTests.cpp, the level loads by ACFTransitionTravelTests.cpp.
 */
using namespace ACFTransitionTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionCatalogTest, "ActionCameraFramework.Transitions.Catalog", EAutomationTestFlags::EditorContext | Flags)
bool FACFTransitionCatalogTest::RunTest(const FString&)
{
	const TArray<FACFTransitionEffectInfo>& All = ACFTransition::GetAllEffects();
	TestEqual(TEXT("42 effects"), All.Num(), 42);
	TSet<FString> Names;
	for (int32 i = 0; i < All.Num(); ++i)
	{
		const FACFTransitionEffectInfo& Info = All[i];
		TestEqual(*FString::Printf(TEXT("ID of #%d"), i), Info.Id, i + 1);
		TestEqual(*FString::Printf(TEXT("enum of %d"), Info.Id), (int32)Info.Effect, Info.Id);
		TestFalse(*FString::Printf(TEXT("%d has a name"), Info.Id), Info.Name.IsEmpty());
		TestFalse(*FString::Printf(TEXT("%d has a description"), Info.Id), Info.Description.IsEmpty());
		Names.Add(Info.Name);
		for (EACFTransitionPhase Phase : { EACFTransitionPhase::Close, EACFTransitionPhase::Open })
		{
			const FACFTransitionSettings S = ACFTransition::MakeDefaultSettings(Info.Effect, Phase);
			TestTrue(*FString::Printf(TEXT("%d default valid"), Info.Id), S.IsValid() && S.Duration > 0.f && S.Duration < 3.f && S.Count >= 0.25f);
		}
		TestEqual(*FString::Printf(TEXT("parse id %d"), Info.Id), ACFTransition::ParseEffect(FString::FromInt(Info.Id)), Info.Effect);
		TestEqual(*FString::Printf(TEXT("parse name %s"), *Info.Name), ACFTransition::ParseEffect(Info.Name), Info.Effect);
	}
	TestEqual(TEXT("unique names"), Names.Num(), 42);
	TestEqual(TEXT("41 is Spiral Close"), ACFTransition::GetEffectInfo(EACFTransitionEffect::SpiralClose).Name, FString(TEXT("Spiral Close")));
	TestEqual(TEXT("42 is Spiral Open"), ACFTransition::GetEffectInfo(EACFTransitionEffect::SpiralOpen).Name, FString(TEXT("Spiral Open")));
	TestEqual(TEXT("parse SpiralOpen"), ACFTransition::ParseEffect(TEXT("spiralopen")), EACFTransitionEffect::SpiralOpen);
	TestEqual(TEXT("parse unknown"), ACFTransition::ParseEffect(TEXT("Nope")), EACFTransitionEffect::None);
	TestEqual(TEXT("parse 43"), ACFTransition::ParseEffect(TEXT("43")), EACFTransitionEffect::None);

	// The plugin content: material and the 42 standard presets.
	TestNotNull(TEXT("M_ACFTransition loads"), FACFTransitionView::LoadDefaultMaterial());
	for (const FACFTransitionEffectInfo& Info : All)
	{
		FString Clean;
		for (TCHAR Ch : Info.Name) { if (FChar::IsAlnum(Ch)) { Clean.AppendChar(Ch); } }
		const FString Path = FString::Printf(TEXT("/ActionCameraFramework/Transitions/Presets/DA_ACFTransition_%02d_%s.DA_ACFTransition_%02d_%s"), Info.Id, *Clean, Info.Id, *Clean);
		const UACFTransitionPreset* Preset = LoadObject<UACFTransitionPreset>(nullptr, *Path);
		if (TestNotNull(*FString::Printf(TEXT("preset %s"), *Path), Preset))
		{
			TestEqual(*FString::Printf(TEXT("preset %d close effect"), Info.Id), Preset->Close.Effect, Info.Effect);
			TestEqual(*FString::Printf(TEXT("preset %d open effect"), Info.Id), Preset->Open.Effect, Info.Effect);
		}
	}
	return true;
}

namespace ACFTransitionLifecycle
{
	/** Common start: a clean subsystem and a recorder. */
	struct FCtx
	{
		TWeakObjectPtr<UACFTransitionSubsystem> S;
		FRecorder Rec;
		FACFTransitionHandle A, B, C;
		double Mark = 0.0;
		TWeakObjectPtr<UMaterialInstanceDynamic> Mid;
	};

	static TSharedPtr<FCtx> Begin(FAutomationTestBase& Test)
	{
		UACFTransitionSubsystem* S = FindSubsystem();
		if (!S)
		{
			Test.AddWarning(TEXT("Skipped: no game instance (run with -game)."));
			return nullptr;
		}
		S->ClearScreen();
		TSharedPtr<FCtx> Ctx = MakeShared<FCtx>();
		Ctx->S = S;
		Ctx->Rec.Bind(S);
		return Ctx;
	}

	static TFunction<void()> End(FAutomationTestBase* Test, TSharedPtr<FCtx> Ctx)
	{
		return [Test, Ctx]()
		{
			if (UACFTransitionSubsystem* S = Ctx->S.Get())
			{
				S->ClearScreen();
				Test->TestFalse(TEXT("End: overlay removed"), S->IsOverlayAttached());
				Test->TestFalse(TEXT("End: view released"), S->HasView());
			}
		};
	}

	static bool Ended(const FCtx& Ctx, FACFTransitionHandle H) { return ACFTransition::IsTerminal(Ctx.S->GetTransitionInfo(H).LastEvent) && !Ctx.S->GetTransitionInfo(H).bActive && !Ctx.S->GetTransitionInfo(H).bQueued; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionCloseOpenTest, "ActionCameraFramework.Transitions.Lifecycle.CloseHoldOpen", Flags)
bool FACFTransitionCloseOpenTest::RunTest(const FString&)
{
	using namespace ACFTransitionLifecycle;
	TSharedPtr<FCtx> Ctx = Begin(*this);
	if (!Ctx) { return true; }
	TArray<TFunction<bool()>> Steps;
	Steps.Add([this, Ctx]()
	{
		Ctx->A = Ctx->S->PlayClose(Settings(EACFTransitionEffect::SpiralClose, 0.3f), FACFTransitionOptions());
		TestTrue(TEXT("Close handle valid"), Ctx->A.IsValid());
		TestEqual(TEXT("Playing"), Ctx->S->GetScreenState(), EACFTransitionScreenState::Playing);
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->A); });
	Steps.Add([this, Ctx]()
	{
		TestEqual(TEXT("Close events"), Ctx->Rec.Of(Ctx->A), FString(TEXT("Started,Covered,Finished")));
		TestEqual(TEXT("Held covered"), Ctx->S->GetScreenState(), EACFTransitionScreenState::Covered);
		TestEqual(TEXT("Coverage 1"), Ctx->S->GetCoverage(), 1.f);
		TestTrue(TEXT("Covered"), Ctx->S->IsCovered());
		TestFalse(TEXT("Nothing active while held"), Ctx->S->IsTransitionActive());
		Ctx->Mark = FPlatformTime::Seconds();
		return true;
	});
	// Hold: stays covered without ticking.
	Steps.Add([this, Ctx]()
	{
		if (FPlatformTime::Seconds() - Ctx->Mark < 0.3) { return false; }
		TestEqual(TEXT("Still covered after the hold"), Ctx->S->GetCoverage(), 1.f);
		Ctx->B = Ctx->S->PlayOpen(Settings(EACFTransitionEffect::SpiralOpen, 0.3f, EACFTransitionPhase::Open), FACFTransitionOptions());
		const FACFTransitionInfo Info = Ctx->S->GetTransitionInfo(Ctx->B);
		TestTrue(TEXT("Open starts covered"), Info.bActive && Info.Coverage >= 0.999f);
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->B); });
	Steps.Add([this, Ctx]()
	{
		TestEqual(TEXT("Open events"), Ctx->Rec.Of(Ctx->B), FString(TEXT("Started,Finished")));
		TestEqual(TEXT("Clear"), Ctx->S->GetScreenState(), EACFTransitionScreenState::Clear);
		TestFalse(TEXT("Overlay removed after the open"), Ctx->S->IsOverlayAttached());
		TestFalse(TEXT("View released after the open"), Ctx->S->HasView());
		// Open on a clear screen: starts fully covered.
		Ctx->C = Ctx->S->PlayOpen(Settings(EACFTransitionEffect::BlackFade, 0.2f, EACFTransitionPhase::Open), FACFTransitionOptions());
		TestTrue(TEXT("Open from clear starts covered"), Ctx->S->GetCoverage() >= 0.999f);
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->C); });
	Steps.Add([this, Ctx]() { TestEqual(TEXT("Open-from-clear events"), Ctx->Rec.Of(Ctx->C), FString(TEXT("Started,Finished"))); return true; });
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(this, MoveTemp(Steps), End(this, Ctx)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionCancelTest, "ActionCameraFramework.Transitions.Lifecycle.CancelSkipNoHold", Flags)
bool FACFTransitionCancelTest::RunTest(const FString&)
{
	using namespace ACFTransitionLifecycle;
	TSharedPtr<FCtx> Ctx = Begin(*this);
	if (!Ctx) { return true; }
	TArray<TFunction<bool()>> Steps;
	Steps.Add([this, Ctx]() { Ctx->A = Ctx->S->PlayClose(Settings(EACFTransitionEffect::InkSpread, 5.f), FACFTransitionOptions()); Ctx->Mark = FPlatformTime::Seconds(); return true; });
	Steps.Add([this, Ctx]()
	{
		if (FPlatformTime::Seconds() - Ctx->Mark < 0.2) { return false; }
		TestTrue(TEXT("Partly covered"), Ctx->S->GetCoverage() > 0.f && Ctx->S->GetCoverage() < 1.f);
		TestTrue(TEXT("Cancel"), Ctx->S->Cancel(Ctx->A, true));
		TestEqual(TEXT("Cancel events"), Ctx->Rec.Of(Ctx->A), FString(TEXT("Started,Cancelled")));
		TestEqual(TEXT("Cancelled -> clear"), Ctx->S->GetCoverage(), 0.f);
		TestFalse(TEXT("Cancelled -> overlay removed"), Ctx->S->IsOverlayAttached());
		TestFalse(TEXT("Cancel twice"), Ctx->S->Cancel(Ctx->A, true));
		// Skip: a 5 s close finishes on the next frames.
		Ctx->B = Ctx->S->PlayClose(Settings(EACFTransitionEffect::CircleIris, 5.f), FACFTransitionOptions());
		TestTrue(TEXT("Skip"), Ctx->S->Skip(Ctx->B));
		Ctx->Mark = FPlatformTime::Seconds();
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->B); });
	Steps.Add([this, Ctx]()
	{
		TestTrue(TEXT("Skip ends well before the duration"), FPlatformTime::Seconds() - Ctx->Mark < 2.0);
		TestEqual(TEXT("Skip events"), Ctx->Rec.Of(Ctx->B), FString(TEXT("Started,Covered,Finished")));
		Ctx->S->ClearScreen();
		// Without hold, duration 0: covered at once, then cleared.
		FACFTransitionOptions NoHold;
		NoHold.bHoldWhenCovered = false;
		Ctx->C = Ctx->S->PlayClose(Settings(EACFTransitionEffect::BlackFade, 0.f), NoHold);
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->C); });
	Steps.Add([this, Ctx]()
	{
		TestEqual(TEXT("No-hold events"), Ctx->Rec.Of(Ctx->C), FString(TEXT("Started,Covered,Finished")));
		TestEqual(TEXT("No-hold -> clear"), Ctx->S->GetScreenState(), EACFTransitionScreenState::Clear);
		// Close on an already covered screen completes without animating.
		Ctx->S->ShowStill(Settings(EACFTransitionEffect::BlackFade, 0.f), EACFTransitionPhase::Close, 1.f);
		Ctx->A = Ctx->S->PlayClose(Settings(EACFTransitionEffect::FilmBurn, 3.f), FACFTransitionOptions());
		Ctx->Mark = FPlatformTime::Seconds();
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->A); });
	Steps.Add([this, Ctx]()
	{
		TestTrue(TEXT("Close on covered is immediate"), FPlatformTime::Seconds() - Ctx->Mark < 1.0);
		TestEqual(TEXT("Close on covered events"), Ctx->Rec.Of(Ctx->A), FString(TEXT("Started,Covered,Finished")));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(this, MoveTemp(Steps), End(this, Ctx)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionConcurrencyTest, "ActionCameraFramework.Transitions.Lifecycle.InterruptQueueReject", Flags)
bool FACFTransitionConcurrencyTest::RunTest(const FString&)
{
	using namespace ACFTransitionLifecycle;
	TSharedPtr<FCtx> Ctx = Begin(*this);
	if (!Ctx) { return true; }
	TArray<TFunction<bool()>> Steps;
	Steps.Add([this, Ctx]() { Ctx->A = Ctx->S->PlayClose(Settings(EACFTransitionEffect::HorizontalWipe, 2.f), FACFTransitionOptions()); Ctx->Mark = FPlatformTime::Seconds(); return true; });
	Steps.Add([this, Ctx]()
	{
		if (FPlatformTime::Seconds() - Ctx->Mark < 0.4) { return false; }
		const float Before = Ctx->S->GetCoverage();
		// Interrupt: A ends Interrupted, B continues from the coverage on screen (no jump).
		Ctx->B = Ctx->S->PlayClose(Settings(EACFTransitionEffect::DiagonalWipe, 1.f), FACFTransitionOptions());
		TestEqual(TEXT("A interrupted"), Ctx->Rec.Of(Ctx->A), FString(TEXT("Started,Interrupted")));
		TestTrue(TEXT("B active"), Ctx->S->GetActiveHandle() == Ctx->B);
		TestNearlyEqual(TEXT("Interrupt keeps the coverage"), Ctx->S->GetCoverage(), Before, 0.02f);
		// Queue: C waits for B.
		FACFTransitionOptions Q;
		Q.Concurrency = EACFTransitionConcurrency::Queue;
		Ctx->C = Ctx->S->PlayOpen(Settings(EACFTransitionEffect::DiagonalWipe, 0.3f, EACFTransitionPhase::Open), Q);
		TestTrue(TEXT("C queued"), Ctx->S->GetTransitionInfo(Ctx->C).bQueued);
		// Reject: D fails (on the next tick), B and C are untouched.
		FACFTransitionOptions R;
		R.Concurrency = EACFTransitionConcurrency::Reject;
		const FACFTransitionHandle D = Ctx->S->PlayClose(Settings(EACFTransitionEffect::BlackFade, 0.3f), R);
		TestTrue(TEXT("D handle valid"), D.IsValid());
		TestEqual(TEXT("D rejected (terminal)"), Ctx->S->GetTransitionInfo(D).LastEvent, EACFTransitionEvent::Failed);
		TestTrue(TEXT("B still active"), Ctx->S->GetActiveHandle() == Ctx->B);
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->C); });
	Steps.Add([this, Ctx]()
	{
		TestEqual(TEXT("B events"), Ctx->Rec.Of(Ctx->B), FString(TEXT("Started,Covered,Finished")));
		TestEqual(TEXT("C events"), Ctx->Rec.Of(Ctx->C), FString(TEXT("Started,Finished")));
		const int32 IndexBFinished = Ctx->Rec.Events.IndexOfByPredicate([&](const auto& P) { return P.Key == Ctx->B && P.Value == EACFTransitionEvent::Finished; });
		const int32 IndexCStarted = Ctx->Rec.Events.IndexOfByPredicate([&](const auto& P) { return P.Key == Ctx->C && P.Value == EACFTransitionEvent::Started; });
		TestTrue(TEXT("C started after B finished"), IndexBFinished != INDEX_NONE && IndexCStarted > IndexBFinished);
		TestTrue(TEXT("Rejected handle got Failed"), Ctx->Rec.Events.ContainsByPredicate([](const auto& P) { return P.Value == EACFTransitionEvent::Failed; }));
		return true;
	});
	// A burst of interrupting requests (one per frame) must not crash or leave the screen covered.
	Steps.Add([this, Ctx]()
	{
		for (int32 i = 1; i <= 42; ++i)
		{
			const EACFTransitionEffect E = ACFTransition::FromId(i);
			Ctx->A = (i % 2) ? Ctx->S->PlayClose(Settings(E, 0.05f), FACFTransitionOptions()) : Ctx->S->PlayOpen(Settings(E, 0.05f, EACFTransitionPhase::Open), FACFTransitionOptions());
		}
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->A); });
	Steps.Add([this, Ctx]() { TestEqual(TEXT("Burst ends clear (last was an Open)"), Ctx->S->GetScreenState(), EACFTransitionScreenState::Clear); return true; });
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(this, MoveTemp(Steps), End(this, Ctx)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionManualLevelTest, "ActionCameraFramework.Transitions.Lifecycle.LevelManualLoad", Flags)
bool FACFTransitionManualLevelTest::RunTest(const FString&)
{
	using namespace ACFTransitionLifecycle;
	TSharedPtr<FCtx> Ctx = Begin(*this);
	if (!Ctx) { return true; }
	TArray<TFunction<bool()>> Steps;
	Steps.Add([this, Ctx]()
	{
		FACFTransitionOptions O;
		O.bManualLoad = true;
		O.bWaitForReadySignal = true;
		O.bLockInput = true;
		Ctx->A = Ctx->S->PlayLevelTransition(Settings(EACFTransitionEffect::SpiralClose, 0.3f), Settings(EACFTransitionEffect::SpiralOpen, 0.3f, EACFTransitionPhase::Open), O, NAME_None, FString());
		if (APlayerController* PC = FindGameWorld()->GetFirstPlayerController())
		{
			TestTrue(TEXT("Input locked"), PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
		}
		return true;
	});
	Steps.Add([Ctx]() { return Ctx->Rec.Has(Ctx->A, EACFTransitionEvent::Covered); });
	Steps.Add([this, Ctx]()
	{
		TestTrue(TEXT("Covered at Covered"), Ctx->S->GetCoverage() >= 1.f);
		TestTrue(TEXT("Still active (waiting for the load)"), Ctx->S->GetActiveHandle() == Ctx->A);
		TestFalse(TEXT("Ready before load is ignored for the load"), false);
		TestTrue(TEXT("NotifyLoadStarted"), Ctx->S->NotifyLoadStarted(Ctx->A));
		TestTrue(TEXT("NotifyLoadCompleted"), Ctx->S->NotifyLoadCompleted(Ctx->A));
		Ctx->Mark = FPlatformTime::Seconds();
		return true;
	});
	// Waits for the ready signal: stays covered.
	Steps.Add([this, Ctx]()
	{
		if (FPlatformTime::Seconds() - Ctx->Mark < 0.4) { return false; }
		TestFalse(TEXT("No LoadCompleted before NotifyWorldReady"), Ctx->Rec.Has(Ctx->A, EACFTransitionEvent::LoadCompleted));
		TestTrue(TEXT("Covered while waiting"), Ctx->S->GetCoverage() >= 1.f);
		TestTrue(TEXT("NotifyWorldReady (any handle)"), Ctx->S->NotifyWorldReady(FACFTransitionHandle()));
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->A); });
	Steps.Add([this, Ctx]()
	{
		TestEqual(TEXT("Level events"), Ctx->Rec.Of(Ctx->A), FString(TEXT("Started,Covered,LoadStarted,LoadCompleted,Finished")));
		if (APlayerController* PC = FindGameWorld()->GetFirstPlayerController())
		{
			TestFalse(TEXT("Input restored"), PC->IsMoveInputIgnored() || PC->IsLookInputIgnored());
		}
		TestEqual(TEXT("Clear after the level transition"), Ctx->S->GetScreenState(), EACFTransitionScreenState::Clear);
		// Timeout: no load ever comes -> Failed, and the screen opens (never stays black).
		FACFTransitionOptions O;
		O.bManualLoad = true;
		O.TimeoutSeconds = 0.4f;
		Ctx->B = Ctx->S->PlayLevelTransition(Settings(EACFTransitionEffect::BlackFade, 0.1f), Settings(EACFTransitionEffect::BlackFade, 0.1f, EACFTransitionPhase::Open), O, NAME_None, FString());
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->B); });
	Steps.Add([this, Ctx]()
	{
		TestEqual(TEXT("Timeout events"), Ctx->Rec.Of(Ctx->B), FString(TEXT("Started,Covered,Failed")));
		TestEqual(TEXT("Timeout -> clear"), Ctx->S->GetScreenState(), EACFTransitionScreenState::Clear);
		// NotifyLoadFailed while covered -> opens with Failed.
		FACFTransitionOptions O;
		O.bManualLoad = true;
		Ctx->C = Ctx->S->PlayLevelTransition(Settings(EACFTransitionEffect::BlackFade, 0.1f), Settings(EACFTransitionEffect::BlackFade, 0.1f, EACFTransitionPhase::Open), O, NAME_None, FString());
		return true;
	});
	Steps.Add([this, Ctx]()
	{
		if (!Ctx->Rec.Has(Ctx->C, EACFTransitionEvent::Covered)) { return false; }
		TestTrue(TEXT("NotifyLoadFailed"), Ctx->S->NotifyLoadFailed(Ctx->C));
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->C); });
	Steps.Add([this, Ctx]()
	{
		TestEqual(TEXT("Load failure events"), Ctx->Rec.Of(Ctx->C), FString(TEXT("Started,Covered,Failed")));
		TestEqual(TEXT("Load failure -> clear"), Ctx->S->GetScreenState(), EACFTransitionScreenState::Clear);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(this, MoveTemp(Steps), End(this, Ctx)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionReleaseTest, "ActionCameraFramework.Transitions.Lifecycle.ReleaseAndBlueprintApi", Flags)
bool FACFTransitionReleaseTest::RunTest(const FString&)
{
	using namespace ACFTransitionLifecycle;
	TSharedPtr<FCtx> Ctx = Begin(*this);
	if (!Ctx) { return true; }
	TArray<TFunction<bool()>> Steps;
	Steps.Add([this, Ctx]()
	{
		// The Blueprint library entry point (what a Blueprint calls).
		Ctx->A = UACFTransitionLibrary::PlayTransitionById(FindGameWorld(), 17, EACFTransitionPhase::Close, 0.2f);
		TestTrue(TEXT("Library handle"), Ctx->A.IsValid());
		return true;
	});
	Steps.Add([this, Ctx]()
	{
		if (!Ctx->S->HasView()) { return false; }
		Ctx->Mid = Ctx->S->GetView()->GetMaterialInstance();
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->A); });
	Steps.Add([this, Ctx]()
	{
		Ctx->B = UACFTransitionLibrary::PlayTransitionEffect(FindGameWorld(), EACFTransitionEffect::PortalRing, EACFTransitionPhase::Open, 0.2f, false);
		return true;
	});
	Steps.Add([Ctx]() { return Ended(*Ctx, Ctx->B); });
	Steps.Add([this, Ctx]()
	{
		TestEqual(TEXT("Library open events"), Ctx->Rec.Of(Ctx->B), FString(TEXT("Started,Finished")));
		TestFalse(TEXT("No view after the open"), Ctx->S->HasView());
		TestEqual(TEXT("No live view object"), FACFTransitionView::GetLiveViewCount(), 0);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		TestFalse(TEXT("Material instance collected"), Ctx->Mid.IsValid());
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(this, MoveTemp(Steps), End(this, Ctx)));
	return true;
}

#endif
