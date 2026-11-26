#include "ACFTransitionLibrary.h"

#include "ACFTransitionPreset.h"
#include "ACFTransitionSubsystem.h"
#include "Misc/DataValidation.h"

UACFTransitionSubsystem* UACFTransitionLibrary::GetTransitionSubsystem(const UObject* WorldContextObject)
{
	return UACFTransitionSubsystem::Get(WorldContextObject);
}

FACFTransitionHandle UACFTransitionLibrary::PlayTransitionEffect(const UObject* WorldContextObject, EACFTransitionEffect Effect, EACFTransitionPhase Phase, float Duration, bool bLockInput)
{
	UACFTransitionSubsystem* Subsystem = UACFTransitionSubsystem::Get(WorldContextObject);
	if (!Subsystem)
	{
		return FACFTransitionHandle();
	}
	FACFTransitionSettings Settings = ACFTransition::MakeDefaultSettings(Effect, Phase);
	if (Duration >= 0.f)
	{
		Settings.Duration = Duration;
	}
	FACFTransitionOptions Options;
	Options.bLockInput = bLockInput;
	return Phase == EACFTransitionPhase::Open ? Subsystem->PlayOpen(Settings, Options) : Subsystem->PlayClose(Settings, Options);
}

FACFTransitionHandle UACFTransitionLibrary::PlayTransitionById(const UObject* WorldContextObject, int32 EffectId, EACFTransitionPhase Phase, float Duration)
{
	return PlayTransitionEffect(WorldContextObject, ACFTransition::FromId(EffectId), Phase, Duration, false);
}

FACFTransitionHandle UACFTransitionLibrary::PreviewTransitionPreset(const UObject* WorldContextObject, const UACFTransitionPreset* Preset, float HoldSeconds)
{
	UACFTransitionSubsystem* Subsystem = UACFTransitionSubsystem::Get(WorldContextObject);
	return Subsystem && IsValid(Preset) ? Subsystem->PlayPreview(Preset->Close, Preset->Open, HoldSeconds) : FACFTransitionHandle();
}

FACFTransitionSettings UACFTransitionLibrary::MakeDefaultTransitionSettings(EACFTransitionEffect Effect, EACFTransitionPhase Phase)
{
	return ACFTransition::MakeDefaultSettings(Effect, Phase);
}

TArray<FACFTransitionEffectInfo> UACFTransitionLibrary::GetAllTransitionEffects()
{
	return ACFTransition::GetAllEffects();
}

FACFTransitionEffectInfo UACFTransitionLibrary::GetTransitionEffectInfo(EACFTransitionEffect Effect)
{
	return ACFTransition::GetEffectInfo(Effect);
}

EACFTransitionEffect UACFTransitionLibrary::TransitionEffectFromId(int32 EffectId)
{
	return ACFTransition::FromId(EffectId);
}

EACFTransitionEffect UACFTransitionLibrary::ParseTransitionEffect(const FString& Text)
{
	return ACFTransition::ParseEffect(Text);
}

// ------------------------------------------------------------------------------------------------ async action

UACFPlayTransitionAction* UACFPlayTransitionAction::PlayTransitionAsync(const UObject* WorldContextObject, const FACFTransitionSettings& Settings, EACFTransitionPhase Phase, const FACFTransitionOptions& Options)
{
	UACFPlayTransitionAction* Action = NewObject<UACFPlayTransitionAction>();
	Action->Subsystem = UACFTransitionSubsystem::Get(WorldContextObject);
	Action->Starter = [Settings, Phase, Options](UACFTransitionSubsystem& S)
	{
		return Phase == EACFTransitionPhase::Open ? S.PlayOpen(Settings, Options) : S.PlayClose(Settings, Options);
	};
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

UACFPlayTransitionAction* UACFPlayTransitionAction::PlayTransitionPresetAsync(const UObject* WorldContextObject, const UACFTransitionPreset* Preset, EACFTransitionPhase Phase, const FACFTransitionOptions& Options)
{
	UACFPlayTransitionAction* Action = NewObject<UACFPlayTransitionAction>();
	Action->Subsystem = UACFTransitionSubsystem::Get(WorldContextObject);
	TWeakObjectPtr<const UACFTransitionPreset> WeakPreset = Preset;
	Action->Starter = [WeakPreset, Phase, Options](UACFTransitionSubsystem& S) { return S.PlayPreset(WeakPreset.Get(), Phase, Options); };
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

UACFPlayTransitionAction* UACFPlayTransitionAction::PlayLevelTransitionAsync(const UObject* WorldContextObject, const UACFTransitionPreset* Preset, FName LevelName, const FACFTransitionOptions& Options)
{
	UACFPlayTransitionAction* Action = NewObject<UACFPlayTransitionAction>();
	Action->Subsystem = UACFTransitionSubsystem::Get(WorldContextObject);
	TWeakObjectPtr<const UACFTransitionPreset> WeakPreset = Preset;
	Action->Starter = [WeakPreset, LevelName, Options](UACFTransitionSubsystem& S) { return S.PlayLevelTransitionPreset(WeakPreset.Get(), Options, LevelName, FString()); };
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UACFPlayTransitionAction::Activate()
{
	UACFTransitionSubsystem* S = Subsystem.Get();
	if (!S || !Starter)
	{
		Failed.Broadcast(FACFTransitionHandle());
		SetReadyToDestroy();
		return;
	}
	// Bind first: the events of the new handle can be sent while it starts.
	Binding = S->OnTransitionEventNative.AddUObject(this, &UACFPlayTransitionAction::HandleEvent);
	MyHandle = FACFTransitionHandle(); // unknown until Starter returns; events before that are matched below
	const FACFTransitionHandle Handle = Starter(*S);
	if (!MyHandle.IsValid())
	{
		MyHandle = Handle;
	}
	// A request that already ended inside Starter (e.g. Close on a covered screen is immediate next tick) is followed
	// through the normal events; GetTransitionInfo covers one that ended synchronously.
	const FACFTransitionInfo Info = S->GetTransitionInfo(Handle);
	if (!Info.bActive && !Info.bQueued && ACFTransition::IsTerminal(Info.LastEvent) && Binding.IsValid())
	{
		HandleEvent(Handle, Info.LastEvent);
	}
}

void UACFPlayTransitionAction::HandleEvent(FACFTransitionHandle Handle, EACFTransitionEvent Event)
{
	if (!MyHandle.IsValid() && Event == EACFTransitionEvent::Started)
	{
		MyHandle = Handle; // the first Started after binding is ours (Starter runs synchronously)
	}
	if (Handle != MyHandle || !Binding.IsValid())
	{
		return;
	}
	switch (Event)
	{
	case EACFTransitionEvent::Covered: Covered.Broadcast(Handle); break;
	case EACFTransitionEvent::LoadCompleted: LoadCompleted.Broadcast(Handle); break;
	case EACFTransitionEvent::Finished: Completed.Broadcast(Handle); break;
	case EACFTransitionEvent::Failed:
	case EACFTransitionEvent::Cancelled:
	case EACFTransitionEvent::Interrupted: Failed.Broadcast(Handle); break;
	default: break;
	}
	if (ACFTransition::IsTerminal(Event))
	{
		if (UACFTransitionSubsystem* S = Subsystem.Get())
		{
			S->OnTransitionEventNative.Remove(Binding);
		}
		Binding.Reset();
		SetReadyToDestroy();
	}
}

// ------------------------------------------------------------------------------------------------ preset

FPrimaryAssetId UACFTransitionPreset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("ACFTransitionPreset"), GetFName());
}

#if WITH_EDITOR
EDataValidationResult UACFTransitionPreset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto Check = [&](const FACFTransitionSettings& S, const TCHAR* Which)
	{
		if (!S.IsValid())
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("%s: no effect selected."), Which)));
			Result = EDataValidationResult::Invalid;
		}
		if (S.Duration < 0.f || S.Duration > 30.f)
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("%s: Duration %.2f is outside 0..30 s."), Which, S.Duration)));
			Result = EDataValidationResult::Invalid;
		}
		if (S.Easing == EACFTransitionEasing::Curve && !S.Curve)
		{
			Context.AddWarning(FText::FromString(FString::Printf(TEXT("%s: Easing is Curve but no curve is set (linear is used)."), Which)));
		}
	};
	Check(Close, TEXT("Close"));
	Check(Open, TEXT("Open"));
	return Result;
}
#endif
