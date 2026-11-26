// Phase 7D: the ACF.Transition.* console commands (README 33 章「コマンド」). Not in Shipping.

#if !UE_BUILD_SHIPPING

#include "ACFTransitionCatalog.h"
#include "ACFTransitionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace ACFTransitionCommands
{
	static UACFTransitionSubsystem* Get(UWorld* World)
	{
		UACFTransitionSubsystem* S = UACFTransitionSubsystem::Get(World);
		if (!S)
		{
			UE_LOG(LogACFTransition, Warning, TEXT("ACF.Transition: no game instance (run it in PIE or the game)."));
		}
		return S;
	}

	static EACFTransitionEffect ParseOrWarn(const FString& Text)
	{
		const EACFTransitionEffect Effect = ACFTransition::ParseEffect(Text);
		if (Effect == EACFTransitionEffect::None)
		{
			UE_LOG(LogACFTransition, Warning, TEXT("ACF.Transition: unknown effect '%s' (ID or name, see ACF.Transition.List)."), *Text);
		}
		return Effect;
	}

	/** ACF.Transition.Play <ID|Name> [Both|Close|Open] [Duration] [Hold seconds] */
	static void Play(const TArray<FString>& Args, UWorld* World)
	{
		UACFTransitionSubsystem* S = Get(World);
		if (!S || Args.IsEmpty())
		{
			UE_LOG(LogACFTransition, Display, TEXT("Usage: ACF.Transition.Play <ID|Name> [Both|Close|Open] [Duration] [HoldSeconds]"));
			return;
		}
		const EACFTransitionEffect Effect = ParseOrWarn(Args[0]);
		if (Effect == EACFTransitionEffect::None)
		{
			return;
		}
		const FString Mode = Args.IsValidIndex(1) ? Args[1] : TEXT("Both");
		const float Duration = Args.IsValidIndex(2) ? FCString::Atof(*Args[2]) : -1.f;
		FACFTransitionSettings Close = ACFTransition::MakeDefaultSettings(Effect, EACFTransitionPhase::Close);
		FACFTransitionSettings Open = ACFTransition::MakeDefaultSettings(Effect, EACFTransitionPhase::Open);
		if (Duration >= 0.f)
		{
			Close.Duration = Open.Duration = Duration;
		}
		FACFTransitionOptions Options;
		if (Mode.Equals(TEXT("Open"), ESearchCase::IgnoreCase))
		{
			S->PlayOpen(Open, Options);
		}
		else if (Mode.Equals(TEXT("Close"), ESearchCase::IgnoreCase))
		{
			S->PlayClose(Close, Options);
		}
		else
		{
			S->PlayClose(Close, Options);
			Options.Concurrency = EACFTransitionConcurrency::Queue;
			S->PlayOpen(Open, Options);
		}
		UE_LOG(LogACFTransition, Display, TEXT("ACF.Transition.Play %02d %s (%s)"), (int32)Effect, *ACFTransition::GetEffectInfo(Effect).Name, *Mode);
	}

	/** ACF.Transition.PreviewAll [Duration] [FirstId] [LastId]: close + open of each effect, queued. */
	static void PreviewAll(const TArray<FString>& Args, UWorld* World)
	{
		UACFTransitionSubsystem* S = Get(World);
		if (!S)
		{
			return;
		}
		const float Duration = Args.IsValidIndex(0) ? FCString::Atof(*Args[0]) : -1.f;
		const int32 First = Args.IsValidIndex(1) ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, ACFTransition::NumEffects) : 1;
		const int32 Last = Args.IsValidIndex(2) ? FMath::Clamp(FCString::Atoi(*Args[2]), First, ACFTransition::NumEffects) : ACFTransition::NumEffects;
		S->ClearScreen();
		FACFTransitionOptions Options;
		Options.Concurrency = EACFTransitionConcurrency::Queue;
		for (int32 Id = First; Id <= Last; ++Id)
		{
			const EACFTransitionEffect Effect = ACFTransition::FromId(Id);
			FACFTransitionSettings Close = ACFTransition::MakeDefaultSettings(Effect, EACFTransitionPhase::Close);
			FACFTransitionSettings Open = ACFTransition::MakeDefaultSettings(Effect, EACFTransitionPhase::Open);
			if (Duration >= 0.f)
			{
				Close.Duration = Open.Duration = Duration;
			}
			S->PlayClose(Close, Options);
			S->PlayOpen(Open, Options);
		}
		UE_LOG(LogACFTransition, Display, TEXT("ACF.Transition.PreviewAll: %d effects queued (%d..%d)."), Last - First + 1, First, Last);
	}

	/** ACF.Transition.Travel <Map> [CloseId] [OpenId] */
	static void Travel(const TArray<FString>& Args, UWorld* World)
	{
		UACFTransitionSubsystem* S = Get(World);
		if (!S || Args.IsEmpty())
		{
			UE_LOG(LogACFTransition, Display, TEXT("Usage: ACF.Transition.Travel <Map> [Close ID] [Open ID]   e.g. ACF.Transition.Travel /ActionCameraFramework/Transitions/Samples/L_ACFTransitionB 41 42"));
			return;
		}
		const EACFTransitionEffect C = Args.IsValidIndex(1) ? ParseOrWarn(Args[1]) : EACFTransitionEffect::BlackFade;
		const EACFTransitionEffect O = Args.IsValidIndex(2) ? ParseOrWarn(Args[2]) : C;
		if (C == EACFTransitionEffect::None || O == EACFTransitionEffect::None)
		{
			return;
		}
		S->PlayLevelTransition(ACFTransition::MakeDefaultSettings(C, EACFTransitionPhase::Close), ACFTransition::MakeDefaultSettings(O, EACFTransitionPhase::Open), FACFTransitionOptions(), FName(*Args[0]), FString());
	}

	/** ACF.Transition.Still <ID|Name> <Coverage 0..1> [Close|Open]: one frozen frame (screenshots, GPU cost). */
	static void Still(const TArray<FString>& Args, UWorld* World)
	{
		UACFTransitionSubsystem* S = Get(World);
		if (!S || Args.Num() < 2)
		{
			UE_LOG(LogACFTransition, Display, TEXT("Usage: ACF.Transition.Still <ID|Name> <Coverage 0..1> [Close|Open]"));
			return;
		}
		const EACFTransitionEffect Effect = ParseOrWarn(Args[0]);
		if (Effect == EACFTransitionEffect::None)
		{
			return;
		}
		const EACFTransitionPhase Phase = Args.IsValidIndex(2) && Args[2].Equals(TEXT("Open"), ESearchCase::IgnoreCase) ? EACFTransitionPhase::Open : EACFTransitionPhase::Close;
		S->ClearScreen();
		S->ShowStill(ACFTransition::MakeDefaultSettings(Effect, Phase), Phase, FCString::Atof(*Args[1]));
	}

	static void List(const TArray<FString>&, UWorld*)
	{
		for (const FACFTransitionEffectInfo& Info : ACFTransition::GetAllEffects())
		{
			UE_LOG(LogACFTransition, Display, TEXT("%02d  %-22s %-16s %s%s"), Info.Id, *Info.Name, *UEnum::GetDisplayValueAsText(Info.Category).ToString(), Info.bUsesSnapshot ? TEXT("[capture] ") : TEXT(""), *Info.Description);
		}
	}

	static void Status(const TArray<FString>&, UWorld* World)
	{
		if (UACFTransitionSubsystem* S = Get(World))
		{
			const FACFTransitionInfo Info = S->GetTransitionInfo(S->GetActiveHandle());
			UE_LOG(LogACFTransition, Display, TEXT("ACF Transition: screen %s, coverage %.2f, active %s (%s, %s, progress %.2f), queue %d, overlay %d, capture %d, last tick %.3f ms"),
				*UEnum::GetValueAsString(S->GetScreenState()), S->GetCoverage(), *S->GetActiveHandle().ToString(), *ACFTransition::GetEffectInfo(Info.Effect).Name,
				*UEnum::GetValueAsString(Info.Phase), Info.Progress, S->GetQueueLength(), S->IsOverlayAttached() ? 1 : 0, S->HasCapture() ? 1 : 0, S->GetLastTickSeconds() * 1000.0);
		}
	}

	static void Clear(const TArray<FString>&, UWorld* World)
	{
		if (UACFTransitionSubsystem* S = Get(World))
		{
			S->ClearScreen();
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs PlayCmd(TEXT("ACF.Transition.Play"), TEXT("Plays a transition: ACF.Transition.Play <ID|Name> [Both|Close|Open] [Duration]"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Play));
	static FAutoConsoleCommandWithWorldAndArgs PreviewAllCmd(TEXT("ACF.Transition.PreviewAll"), TEXT("Plays all transitions (close + open) in order: ACF.Transition.PreviewAll [Duration] [First] [Last]"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PreviewAll));
	static FAutoConsoleCommandWithWorldAndArgs TravelCmd(TEXT("ACF.Transition.Travel"), TEXT("Level transition: ACF.Transition.Travel <Map> [CloseId] [OpenId]"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Travel));
	static FAutoConsoleCommandWithWorldAndArgs StillCmd(TEXT("ACF.Transition.Still"), TEXT("Shows one effect frozen: ACF.Transition.Still <ID|Name> <Coverage> [Close|Open]"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Still));
	static FAutoConsoleCommandWithWorldAndArgs ListCmd(TEXT("ACF.Transition.List"), TEXT("Lists the transitions."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&List));
	static FAutoConsoleCommandWithWorldAndArgs StatusCmd(TEXT("ACF.Transition.Status"), TEXT("Prints the transition state."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Status));
	static FAutoConsoleCommandWithWorldAndArgs ClearCmd(TEXT("ACF.Transition.Clear"), TEXT("Cancels all transitions and clears the screen."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Clear));
}

#endif
