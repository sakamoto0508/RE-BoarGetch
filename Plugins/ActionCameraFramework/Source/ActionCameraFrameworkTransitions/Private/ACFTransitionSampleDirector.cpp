#include "ACFTransitionSampleDirector.h"

#include "ACFTransitionCatalog.h"
#include "ACFTransitionSettings.h"
#include "ACFTransitionSubsystem.h"
#include "ACFTransitionView.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

namespace ACFTransitionDemo
{
	// The demo survives map loads (the actors do not), so its state is static.
	static bool bParsed = false;
	static FString Mode;
	static int32 TravelsLeft = 0;
	static int32 Trip = 0;
	static int32 Failures = 0;
	static FACFTransitionHandle Current;
	static bool bAwaitingStart = false; // Started is sent before PlayLevelTransition / PlayClose return the handle
	static FString TripEvents;
	static double TripStart = 0.0;
	static bool bHooks = false;
	static bool bFinished = false;
	static TWeakObjectPtr<UACFTransitionSubsystem> Subsystem;
	static EACFTransitionEffect CloseOverride = EACFTransitionEffect::None;
	static EACFTransitionEffect OpenOverride = EACFTransitionEffect::None;

	static void Parse()
	{
		if (bParsed)
		{
			return;
		}
		bParsed = true;
		FString Value;
		if (FParse::Value(FCommandLine::Get(), TEXT("ACFTransitionDemo="), Value))
		{
			if (Value.StartsWith(TEXT("Travel")))
			{
				Mode = TEXT("Travel");
				FString Count;
				TravelsLeft = Value.Split(TEXT(":"), nullptr, &Count) ? FMath::Max(1, FCString::Atoi(*Count)) : 2;
			}
			else
			{
				Mode = Value;
			}
		}
		int32 Id = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("ACFTransitionClose="), Id)) { CloseOverride = ACFTransition::FromId(Id); }
		if (FParse::Value(FCommandLine::Get(), TEXT("ACFTransitionOpen="), Id)) { OpenOverride = ACFTransition::FromId(Id); }
	}

	static void OnEvent(FACFTransitionHandle Handle, EACFTransitionEvent Event)
	{
		if (bAwaitingStart && Event == EACFTransitionEvent::Started)
		{
			Current = Handle;
			bAwaitingStart = false;
		}
		if (Handle != Current)
		{
			return;
		}
		TripEvents += (TripEvents.IsEmpty() ? TEXT("") : TEXT(",")) + FString(ACFTransition::LexEvent(Event));
		if (ACFTransition::IsTerminal(Event))
		{
			const bool bOk = Event == EACFTransitionEvent::Finished;
			Failures += bOk ? 0 : 1;
			AACFTransitionSampleDirector::WriteDemoLog(FString::Printf(TEXT("TRIP %d END %s events=[%s] %.0f ms"), Trip, bOk ? TEXT("OK") : TEXT("FAIL"), *TripEvents, (FPlatformTime::Seconds() - TripStart) * 1000.0));
		}
	}

	static void OnPreLoadMap(const FString& Map)
	{
		if (Mode != TEXT("Travel") || Trip == 0)
		{
			return;
		}
		UACFTransitionSubsystem* S = Subsystem.Get();
		const bool bOk = S && S->GetCoverage() >= 1.f && S->IsOverlayAttached();
		Failures += bOk ? 0 : 1;
		AACFTransitionSampleDirector::WriteDemoLog(FString::Printf(TEXT("TRIP %d PreLoadMap %s coverage=%.3f overlay=%d %s"), Trip, *Map, S ? S->GetCoverage() : -1.f, S && S->IsOverlayAttached() ? 1 : 0, bOk ? TEXT("OK") : TEXT("FAIL")));
	}

	static void OnPostLoadMap(UWorld* World)
	{
		if (Mode != TEXT("Travel") || Trip == 0)
		{
			return;
		}
		UACFTransitionSubsystem* S = Subsystem.Get();
		const bool bOk = World && S && S->GetCoverage() >= 1.f && S->IsOverlayAttached();
		Failures += bOk ? 0 : 1;
		AACFTransitionSampleDirector::WriteDemoLog(FString::Printf(TEXT("TRIP %d PostLoadMap %s coverage=%.3f overlay=%d %s"), Trip, *GetNameSafe(World), S ? S->GetCoverage() : -1.f, S && S->IsOverlayAttached() ? 1 : 0, bOk ? TEXT("OK") : TEXT("FAIL")));
	}

	static void EnsureHooks(UACFTransitionSubsystem* S)
	{
		Subsystem = S;
		if (bHooks || !S)
		{
			return;
		}
		bHooks = true;
		S->OnTransitionEventNative.AddStatic(&OnEvent);
		FCoreUObjectDelegates::PreLoadMap.AddStatic(&OnPreLoadMap);
		FCoreUObjectDelegates::PostLoadMapWithWorld.AddStatic(&OnPostLoadMap);
	}
}

void AACFTransitionSampleDirector::RegisterCommandLineBootstrap()
{
	// Shipping ignores a map on the command line (UE_ALLOW_MAP_OVERRIDE_IN_SHIPPING = 0): the game starts on its default
	// map (which may itself travel on, e.g. title -> lobby). With -ACFTransitionDemo a core ticker waits until a game
	// world that is not a sample map has been playing for a moment, opens L_ACFTransitionA, and retries a few times if
	// the game travels elsewhere first. It stops once a sample map is loaded.
	if (!FParse::Param(FCommandLine::Get(), TEXT("ACFTransitionDemo")) && !FCString::Strifind(FCommandLine::Get(), TEXT("ACFTransitionDemo=")))
	{
		return;
	}
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) -> bool
	{
		static int32 Attempts = 0;
		static double LastAttempt = -100.0;
		static double FirstSeen = -1.0;
		UWorld* Game = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Game && Context.World() && Context.World()->HasBegunPlay())
				{
					Game = Context.World();
					break;
				}
			}
		}
		if (!Game)
		{
			FirstSeen = -1.0;
			return true;
		}
		if (Game->GetMapName().Contains(TEXT("L_ACFTransition")))
		{
			return false; // done
		}
		const double Now = FPlatformTime::Seconds();
		FirstSeen = FirstSeen < 0.0 ? Now : FirstSeen;
		if (Now - FirstSeen < 1.0 || Now - LastAttempt < 5.0)
		{
			return true;
		}
		if (++Attempts > 5)
		{
			WriteDemoLog(TEXT("DEMO BOOT FAIL: could not open L_ACFTransitionA"));
			return false;
		}
		LastAttempt = Now;
		WriteDemoLog(FString::Printf(TEXT("DEMO BOOT on %s (attempt %d); opening L_ACFTransitionA"), *Game->GetMapName(), Attempts));
		UGameplayStatics::OpenLevel(Game, FName(TEXT("/ActionCameraFramework/Transitions/Samples/L_ACFTransitionA")), true, TEXT("game=/Script/Engine.GameModeBase"));
		return true;
	}), 0.25f);
}

AACFTransitionSampleDirector::AACFTransitionSampleDirector()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AACFTransitionSampleDirector::WriteDemoLog(const FString& Line)
{
	UE_LOG(LogACFTransition, Display, TEXT("ACFTransitionDemo: %s"), *Line);
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ACFTransitionDemo.log"));
	FFileHelper::SaveStringToFile(Line + LINE_TERMINATOR, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

void AACFTransitionSampleDirector::BeginPlay()
{
	Super::BeginPlay();
	using namespace ACFTransitionDemo;
	Parse();
	UACFTransitionSubsystem* S = UACFTransitionSubsystem::Get(this);
	if (Mode.IsEmpty() || !S || bFinished)
	{
		return;
	}
	EnsureHooks(S);
	if (Mode == TEXT("Travel"))
	{
		if (Trip == 0)
		{
			WriteDemoLog(FString::Printf(TEXT("DEMO START Travel x%d map=%s close=%d open=%d"), TravelsLeft, *GetWorld()->GetMapName(),
				(int32)(CloseOverride != EACFTransitionEffect::None ? CloseOverride : CloseEffect), (int32)(OpenOverride != EACFTransitionEffect::None ? OpenOverride : OpenEffect)));
		}
		GetWorldTimerManager().SetTimer(Timer, this, TravelsLeft > 0 ? &AACFTransitionSampleDirector::TravelToNextLevel : &AACFTransitionSampleDirector::FinishDemo, StayTime, false);
	}
	else if (Mode == TEXT("All"))
	{
		WriteDemoLog(TEXT("DEMO START All"));
		EventBinding = S->OnTransitionEventNative.AddUObject(this, &AACFTransitionSampleDirector::HandleEvent);
		GetWorldTimerManager().SetTimer(Timer, this, &AACFTransitionSampleDirector::PlayNextEffect, 0.5f, false);
	}
}

void AACFTransitionSampleDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UACFTransitionSubsystem* S = UACFTransitionSubsystem::Get(this))
	{
		S->OnTransitionEventNative.Remove(EventBinding);
	}
	Super::EndPlay(Reason);
}

void AACFTransitionSampleDirector::TravelToNextLevel()
{
	using namespace ACFTransitionDemo;
	UACFTransitionSubsystem* S = UACFTransitionSubsystem::Get(this);
	if (!S || NextLevel.IsNone())
	{
		return;
	}
	if (S->IsTransitionActive())
	{
		GetWorldTimerManager().SetTimer(Timer, this, &AACFTransitionSampleDirector::TravelToNextLevel, 0.25f, false);
		return;
	}
	const EACFTransitionEffect C = CloseOverride != EACFTransitionEffect::None ? CloseOverride : CloseEffect;
	const EACFTransitionEffect O = OpenOverride != EACFTransitionEffect::None ? OpenOverride : OpenEffect;
	FACFTransitionOptions Options;
	Options.bLockInput = true;
	TravelsLeft = FMath::Max(0, TravelsLeft - 1);
	++Trip;
	TripEvents.Reset();
	TripStart = FPlatformTime::Seconds();
	WriteDemoLog(FString::Printf(TEXT("TRIP %d BEGIN %s -> %s"), Trip, *GetWorld()->GetMapName(), *NextLevel.ToString()));
	bAwaitingStart = true;
	Current = S->PlayLevelTransition(ACFTransition::MakeDefaultSettings(C, EACFTransitionPhase::Close), ACFTransition::MakeDefaultSettings(O, EACFTransitionPhase::Open), Options, NextLevel, FString());
}

void AACFTransitionSampleDirector::PlayNextEffect()
{
	UACFTransitionSubsystem* S = UACFTransitionSubsystem::Get(this);
	if (!S)
	{
		return;
	}
	if (EffectCursor >= ACFTransition::NumEffects)
	{
		FinishDemo();
		return;
	}
	const EACFTransitionEffect Effect = ACFTransition::FromId(EffectCursor + 1);
	const EACFTransitionPhase Phase = bEffectOpenNext ? EACFTransitionPhase::Open : EACFTransitionPhase::Close;
	ACFTransitionDemo::TripEvents.Reset();
	ACFTransitionDemo::TripStart = FPlatformTime::Seconds();
	ACFTransitionDemo::Trip = EffectCursor + 1;
	ACFTransitionDemo::bAwaitingStart = true;
	ACFTransitionDemo::Current = Phase == EACFTransitionPhase::Open
		? S->PlayOpen(ACFTransition::MakeDefaultSettings(Effect, Phase), FACFTransitionOptions())
		: S->PlayClose(ACFTransition::MakeDefaultSettings(Effect, Phase), FACFTransitionOptions());
}

void AACFTransitionSampleDirector::HandleEvent(FACFTransitionHandle Handle, EACFTransitionEvent Event)
{
	if (Handle != ACFTransitionDemo::Current || !ACFTransition::IsTerminal(Event))
	{
		return;
	}
	if (!bEffectOpenNext)
	{
		// A Close ended: how the screen capture of the capture-based effects was taken (README 33 章「キャプチャ」).
		const UACFTransitionSubsystem* S = UACFTransitionSubsystem::Get(this);
		const EACFTransitionEffect Effect = ACFTransition::FromId(EffectCursor + 1);
		if (S && ACFTransition::GetEffectInfo(Effect).bUsesSnapshot)
		{
			WriteDemoLog(FString::Printf(TEXT("CAPTURE %02d captured=%d gpu=%d waited=%d frames fallback=%d"), EffectCursor + 1, S->HasCapture() ? 1 : 0,
				GetDefault<UACFTransitionProjectSettings>()->bCaptureOnGpu && !S->DidLastCaptureFallBack() ? 1 : 0, S->GetLastCaptureWaitFrames(), S->DidLastCaptureFallBack() ? 1 : 0));
		}
	}
	if (bEffectOpenNext)
	{
		++EffectCursor;
	}
	bEffectOpenNext = !bEffectOpenNext;
	GetWorldTimerManager().SetTimer(Timer, this, &AACFTransitionSampleDirector::PlayNextEffect, bEffectOpenNext ? 0.15f : 0.4f, false);
}

void AACFTransitionSampleDirector::FinishDemo()
{
	using namespace ACFTransitionDemo;
	UACFTransitionSubsystem* S = UACFTransitionSubsystem::Get(this);
	if (S && S->IsTransitionActive())
	{
		GetWorldTimerManager().SetTimer(Timer, this, &AACFTransitionSampleDirector::FinishDemo, 0.25f, false);
		return;
	}
	bFinished = true;
	const bool bClean = S && !S->IsOverlayAttached() && !S->HasView() && FACFTransitionView::GetLiveViewCount() == 0;
	Failures += bClean ? 0 : 1;
	WriteDemoLog(FString::Printf(TEXT("DEMO DONE mode=%s trips=%d failures=%d overlay=%d views=%d %s"), *Mode, Trip, Failures, S && S->IsOverlayAttached() ? 1 : 0, FACFTransitionView::GetLiveViewCount(), Failures == 0 ? TEXT("PASS") : TEXT("FAIL")));
	if (FParse::Param(FCommandLine::Get(), TEXT("ACFTransitionDemoExit")))
	{
		FPlatformMisc::RequestExit(false);
	}
}
