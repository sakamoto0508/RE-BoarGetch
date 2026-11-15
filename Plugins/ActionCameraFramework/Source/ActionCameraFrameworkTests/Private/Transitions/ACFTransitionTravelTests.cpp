#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFTransitionTestUtils.h"
#include "ACFTransitionView.h"
#include "Engine/GameViewportClient.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

/**
 * Phase 7D: level transitions through real OpenLevel loads (README 33 章「Level Travel」): start map -> L_ACFTransitionA ->
 * L_ACFTransitionB -> L_ACFTransitionA -> start map. At every PreLoadMap the screen must be fully covered and the overlay in
 * the viewport, after every load the overlay must still be there and covered, the events must come in order, the screen
 * must be clear at the end and the old worlds must be garbage collected. Game context (-game; -NullRHI works too).
 */
namespace ACFTransitionTravel
{
	static const TCHAR* MapA = TEXT("/ActionCameraFramework/Transitions/Samples/L_ACFTransitionA");
	static const TCHAR* MapB = TEXT("/ActionCameraFramework/Transitions/Samples/L_ACFTransitionB");

	struct FCtx
	{
		TWeakObjectPtr<UACFTransitionSubsystem> S;
		ACFTransitionTest::FRecorder Rec;
		FACFTransitionHandle Handle;
		FString StartMap;
		TArray<FString> Log;
		TArray<TWeakObjectPtr<UWorld>> OldWorlds;
		FDelegateHandle Pre, Post;
		int32 Loads = 0;
		bool bHasViewport = false;

		~FCtx()
		{
			FCoreUObjectDelegates::PreLoadMap.Remove(Pre);
			FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(Post);
		}
	};

	static FString MapOf(UWorld* World) { return World ? World->GetOutermost()->GetName() : FString(); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionTravelTest, "ActionCameraFramework.Transitions.Travel.OpenLevelABA", ACFTransitionTest::Flags)
bool FACFTransitionTravelTest::RunTest(const FString&)
{
	using namespace ACFTransitionTravel;
	using namespace ACFTransitionTest;
	UACFTransitionSubsystem* S = FindSubsystem();
	if (!S)
	{
		AddWarning(TEXT("Skipped: no game instance (run with -game)."));
		return true;
	}
	S->ClearScreen();
	TSharedPtr<FCtx> Ctx = MakeShared<FCtx>();
	Ctx->S = S;
	Ctx->Rec.Bind(S);
	Ctx->StartMap = MapOf(FindGameWorld());
	Ctx->bHasViewport = FindGameWorld()->GetGameViewport() != nullptr;
	FCtx* Raw = Ctx.Get();
	Ctx->Pre = FCoreUObjectDelegates::PreLoadMap.AddLambda([this, Raw](const FString& Map)
	{
		UACFTransitionSubsystem* Sub = Raw->S.Get();
		Raw->OldWorlds.Add(FindGameWorld());
		Raw->Log.Add(FString::Printf(TEXT("Pre %s cov %.3f overlay %d"), *Map, Sub ? Sub->GetCoverage() : -1.f, Sub && Sub->IsOverlayAttached() ? 1 : 0));
		TestTrue(*FString::Printf(TEXT("Fully covered when the load of %s starts"), *Map), Sub && Sub->GetCoverage() >= 1.f);
		if (Raw->bHasViewport)
		{
			TestTrue(*FString::Printf(TEXT("Overlay in the viewport when the load of %s starts"), *Map), Sub && Sub->IsOverlayAttached());
		}
	});
	Ctx->Post = FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda([this, Raw](UWorld* World)
	{
		UACFTransitionSubsystem* Sub = Raw->S.Get();
		++Raw->Loads;
		Raw->Log.Add(FString::Printf(TEXT("Post %s cov %.3f overlay %d"), *MapOf(World), Sub ? Sub->GetCoverage() : -1.f, Sub && Sub->IsOverlayAttached() ? 1 : 0));
		TestNotNull(TEXT("Loaded world"), World);
		TestTrue(TEXT("Still covered after the load"), Sub && Sub->GetCoverage() >= 1.f);
		if (Raw->bHasViewport)
		{
			TestTrue(TEXT("Overlay survived the load"), Sub && Sub->IsOverlayAttached() && Sub->GetView() && Sub->GetView()->GetWidget()->GetParentWidget().IsValid());
		}
	});

	auto Go = [this, Ctx](const FString& Map, EACFTransitionEffect Close, EACFTransitionEffect Open, const FString& TravelOptions = FString())
	{
		return [this, Ctx, Map, Close, Open, TravelOptions]()
		{
			FACFTransitionOptions O;
			O.bLockInput = true;
			Ctx->Handle = Ctx->S->PlayLevelTransition(Settings(Close, 0.35f), Settings(Open, 0.35f, EACFTransitionPhase::Open), O, FName(*Map), TravelOptions);
			return true;
		};
	};
	auto Arrive = [this, Ctx](const FString& Map)
	{
		return [this, Ctx, Map]()
		{
			const FACFTransitionInfo Info = Ctx->S->GetTransitionInfo(Ctx->Handle);
			if (Info.bActive || !ACFTransition::IsTerminal(Info.LastEvent))
			{
				return false;
			}
			TestEqual(*FString::Printf(TEXT("Events of the travel to %s"), *Map), Ctx->Rec.Of(Ctx->Handle), FString(TEXT("Started,Covered,LoadStarted,LoadCompleted,Finished")));
			TestEqual(TEXT("Arrived"), MapOf(FindGameWorld()), Map);
			TestEqual(TEXT("Clear after arriving"), Ctx->S->GetScreenState(), EACFTransitionScreenState::Clear);
			TestFalse(TEXT("Overlay removed after arriving"), Ctx->S->IsOverlayAttached());
			if (APlayerController* PC = FindGameWorld()->GetFirstPlayerController())
			{
				TestFalse(TEXT("Input of the new controller restored"), PC->IsMoveInputIgnored());
			}
			return true;
		};
	};

	TArray<TFunction<bool()>> Steps;
	Steps.Add(Go(MapA, EACFTransitionEffect::SpiralClose, EACFTransitionEffect::SpiralOpen));
	Steps.Add(Arrive(MapA));
	Steps.Add(Go(MapB, EACFTransitionEffect::BlackFade, EACFTransitionEffect::BlackFade));
	Steps.Add(Arrive(MapB));
	Steps.Add(Go(MapA, EACFTransitionEffect::CrossDissolve, EACFTransitionEffect::CrossDissolve));
	Steps.Add(Arrive(MapA));
	Steps.Add([this, Ctx, Go]()
	{
		// Back to the start map with the engine game mode of the test environment (not the project's).
		return Go(Ctx->StartMap, EACFTransitionEffect::BlackFade, EACFTransitionEffect::BlackFade, TEXT("game=/Script/Engine.GameModeBase"))();
	});
	Steps.Add([this, Ctx, Arrive]() { return Arrive(Ctx->StartMap)(); });
	Steps.Add([this, Ctx]()
	{
		TestEqual(TEXT("4 loads"), Ctx->Loads, 4);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		int32 Alive = 0;
		for (const TWeakObjectPtr<UWorld>& W : Ctx->OldWorlds)
		{
			Alive += (W.IsValid() && W.Get() != FindGameWorld()) ? 1 : 0;
		}
		TestEqual(TEXT("Old worlds collected (no reference kept by the transition)"), Alive, 0);
		TestEqual(TEXT("No view object left"), FACFTransitionView::GetLiveViewCount(), 0);
		for (const FString& Line : Ctx->Log)
		{
			AddInfo(Line);
		}
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FSteps(this, MoveTemp(Steps), [Ctx]() { if (Ctx->S.IsValid()) { Ctx->S->ClearScreen(); } }, 120.0));
	return true;
}

#endif
