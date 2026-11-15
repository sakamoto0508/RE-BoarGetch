#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFTransitionTestUtils.h"
#include "ACFTransitionView.h"
#include "Curves/CurveFloat.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

/**
 * Phase 7D: level transitions through real OpenLevel loads (README 33 章「Level Travel」): start map -> L_ACFTransitionA ->
 * L_ACFTransitionB -> L_ACFTransitionA -> start map. At every PreLoadMap the screen must be fully covered and the overlay in
 * the viewport, after every load the overlay must still be there and covered, the events must come in order, the screen
 * must be clear at the end and the old worlds must be garbage collected. Game context (-game; -NullRHI works too).
 * Review fixes: the second leg is skipped right after it starts (a capture effect: the GPU copy may be pending) and must still
 * be covered before the load; the third leg waits for NotifyWorldReady, and while it waits after the real load the new
 * world's controller must be locked and the Open settings' transient curve / texture / material must survive a GC.
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
		TWeakObjectPtr<UCurveFloat> Curve;
		TWeakObjectPtr<UTexture2D> Texture;
		TWeakObjectPtr<UMaterialInterface> Material;
		double Mark = 0.0;
		bool bHasMaterial = false;

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

	enum class ELeg : uint8 { Plain, Skip, WaitReady };
	auto Go = [this, Ctx](const FString& Map, EACFTransitionEffect Close, EACFTransitionEffect Open, const FString& TravelOptions = FString(), ELeg Leg = ELeg::Plain)
	{
		return [this, Ctx, Map, Close, Open, TravelOptions, Leg]()
		{
			FACFTransitionOptions O;
			O.bLockInput = true;
			FACFTransitionSettings OpenSettings = Settings(Open, 0.35f, EACFTransitionPhase::Open);
			if (Leg == ELeg::WaitReady)
			{
				O.bWaitForReadySignal = true;
				// Referenced only by this request: LoadMap's own GC and the GC below must not collect them.
				UCurveFloat* Curve = NewObject<UCurveFloat>(GetTransientPackage(), NAME_None, RF_Transient);
				Curve->FloatCurve.AddKey(0.f, 0.f);
				Curve->FloatCurve.AddKey(1.f, 1.f);
				UTexture2D* Texture = UTexture2D::CreateTransient(4, 4);
				UMaterialInterface* Base = FACFTransitionView::LoadDefaultMaterial();
				OpenSettings.Easing = EACFTransitionEasing::Curve;
				OpenSettings.Curve = Curve;
				OpenSettings.OverlayTexture = Texture;
#if WITH_EDITOR
				if (Base)
				{
					UMaterialInstanceConstant* Mic = NewObject<UMaterialInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transient);
					Mic->SetParentEditorOnly(Base);
					Mic->PostEditChange();
					OpenSettings.MaterialOverride = Mic;
				}
#endif
				Ctx->Curve = Curve;
				Ctx->Texture = Texture;
				Ctx->Material = OpenSettings.MaterialOverride.Get();
				Ctx->bHasMaterial = OpenSettings.MaterialOverride != nullptr;
			}
			Ctx->Handle = Ctx->S->PlayLevelTransition(Settings(Close, 0.35f), OpenSettings, O, FName(*Map), TravelOptions);
			if (Leg == ELeg::Skip)
			{
				TestTrue(TEXT("Skip right after the start"), Ctx->S->Skip(Ctx->Handle));
				TestFalse(TEXT("Skip drops a pending capture"), Ctx->S->IsCapturePending());
			}
			return true;
		};
	};
	auto WaitReady = [this, Ctx](int32 Loads)
	{
		return [this, Ctx, Loads]()
		{
			if (Ctx->Loads < Loads) { Ctx->Mark = FPlatformTime::Seconds(); return false; }
			if (FPlatformTime::Seconds() - Ctx->Mark < 0.5) { return false; } // the new world runs while the transition waits
			TestTrue(TEXT("Waiting for NotifyWorldReady after the load"), Ctx->S->GetActiveHandle() == Ctx->Handle && !Ctx->Rec.Has(Ctx->Handle, EACFTransitionEvent::LoadCompleted));
			if (APlayerController* PC = FindGameWorld()->GetFirstPlayerController())
			{
				TestTrue(TEXT("The new world's controller is locked while waiting"), PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			TestTrue(TEXT("Open settings' curve / texture / material survive the load and a GC"), Ctx->Curve.IsValid() && Ctx->Texture.IsValid() && (Ctx->Material.IsValid() || !Ctx->bHasMaterial));
			TestTrue(TEXT("NotifyWorldReady"), Ctx->S->NotifyWorldReady(Ctx->Handle));
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
	Steps.Add(Go(MapB, EACFTransitionEffect::ZoomRush, EACFTransitionEffect::BlackFade, FString(), ELeg::Skip));
	Steps.Add(Arrive(MapB));
	Steps.Add(Go(MapA, EACFTransitionEffect::CrossDissolve, EACFTransitionEffect::CrossDissolve, FString(), ELeg::WaitReady));
	Steps.Add(WaitReady(3));
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
		TestFalse(TEXT("Transient Open settings released after the travel"), Ctx->Curve.IsValid() || Ctx->Texture.IsValid() || Ctx->Material.IsValid());
		TestEqual(TEXT("No input lock left"), Ctx->S->GetLockedControllerCount(), 0);
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
