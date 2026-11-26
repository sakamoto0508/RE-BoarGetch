#include "ACFTransitionSubsystem.h"

#include "ACFTransitionCatalog.h"
#include "ACFTransitionPreset.h"
#include "ACFTransitionSettings.h"
#include "ACFTransitionView.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CoreDelegates.h"
#include "MoviePlayer.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

UACFTransitionSubsystem::UACFTransitionSubsystem()
{
}

UACFTransitionSubsystem* UACFTransitionSubsystem::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UACFTransitionSubsystem>() : nullptr;
}

bool UACFTransitionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UACFTransitionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UACFTransitionSubsystem::HandlePreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UACFTransitionSubsystem::HandlePostLoadMap);
	if (GEngine)
	{
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UACFTransitionSubsystem::HandleTravelFailure);
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UACFTransitionSubsystem::HandleNetworkFailure);
	}
	bInitialized = true;
}

void UACFTransitionSubsystem::Deinitialize()
{
	bInitialized = false;
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	if (GEngine)
	{
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
	}
	// No events at shutdown (listeners may be gone); just give the input back and drop the overlay. Sounds that belong to
	// a transition stop now (the PlayToEnd ones finish on their own).
	for (const FTransitionSound& Tracked : Sounds)
	{
		if (UAudioComponent* Component = Tracked.Component.Get())
		{
			Component->Stop();
		}
	}
	Sounds.Reset();
	ApplyInputLock(false);
	Active.Reset();
	Queue.Reset();
	Incoming.Reset();
	Starting.Reset();
	PendingEvents.Reset();
	Stage = EStage::None;
	Coverage = 0.f;
	bStill = false;
	DetachOverlay();
	Super::Deinitialize();
}

void UACFTransitionSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	// GC path: GameInstance -> its subsystem collection -> this subsystem -> here -> the properties of each request's
	// FACFTransitionSettings (Curve, OverlayTexture, MaterialOverride, Sound). DrawSettings and Capture are UPROPERTYs.
	UACFTransitionSubsystem* This = CastChecked<UACFTransitionSubsystem>(InThis);
	UScriptStruct* SettingsStruct = FACFTransitionSettings::StaticStruct();
	auto AddRequest = [&Collector, This, SettingsStruct](FRequest& Request)
	{
		Collector.AddPropertyReferencesWithStructARO(SettingsStruct, &Request.Close, This);
		Collector.AddPropertyReferencesWithStructARO(SettingsStruct, &Request.Open, This);
	};
	if (This->Active.IsSet())
	{
		AddRequest(*This->Active);
	}
	if (This->Starting.IsSet())
	{
		AddRequest(*This->Starting);
	}
	for (FRequest& Request : This->Incoming)
	{
		AddRequest(Request);
	}
	for (FRequest& Request : This->Queue)
	{
		AddRequest(Request);
	}
}

// ------------------------------------------------------------------------------------------------ requests

FACFTransitionHandle UACFTransitionSubsystem::PlayClose(const FACFTransitionSettings& Settings, const FACFTransitionOptions& Options)
{
	FRequest Request;
	Request.Kind = EACFTransitionKind::Close;
	Request.Close = Settings;
	Request.Options = Options;
	return Submit(MoveTemp(Request));
}

FACFTransitionHandle UACFTransitionSubsystem::PlayOpen(const FACFTransitionSettings& Settings, const FACFTransitionOptions& Options)
{
	FRequest Request;
	Request.Kind = EACFTransitionKind::Open;
	Request.Open = Settings;
	Request.Options = Options;
	return Submit(MoveTemp(Request));
}

FACFTransitionHandle UACFTransitionSubsystem::PlayPreset(const UACFTransitionPreset* Preset, EACFTransitionPhase Phase, const FACFTransitionOptions& Options)
{
	if (!Preset)
	{
		UE_LOG(LogACFTransition, Warning, TEXT("ACF Transition: PlayPreset without a preset; Black Fade is used."));
		const FACFTransitionSettings Fallback = ACFTransition::MakeDefaultSettings(EACFTransitionEffect::BlackFade, Phase);
		return Phase == EACFTransitionPhase::Open ? PlayOpen(Fallback, Options) : PlayClose(Fallback, Options);
	}
	return Phase == EACFTransitionPhase::Open ? PlayOpen(Preset->Open, Options) : PlayClose(Preset->Close, Options);
}

FACFTransitionHandle UACFTransitionSubsystem::PlayLevelTransition(const FACFTransitionSettings& Close, const FACFTransitionSettings& Open, const FACFTransitionOptions& Options, FName LevelName, const FString& TravelOptions)
{
	FRequest Request;
	Request.Kind = EACFTransitionKind::Level;
	Request.Close = Close;
	Request.Open = Open;
	Request.Options = Options;
	Request.LevelName = LevelName;
	Request.TravelOptions = TravelOptions;
	if (Options.bManualLoad && !LevelName.IsNone())
	{
		UE_LOG(LogACFTransition, Log, TEXT("ACF Transition: Level Name %s is not opened by ACF (Manual Load: the game loads and calls NotifyLoadCompleted)."), *LevelName.ToString());
	}
	return Submit(MoveTemp(Request));
}

FACFTransitionHandle UACFTransitionSubsystem::PlayLevelTransitionPreset(const UACFTransitionPreset* Preset, const FACFTransitionOptions& Options, FName LevelName, const FString& TravelOptions)
{
	const FACFTransitionSettings Close = Preset ? Preset->Close : ACFTransition::MakeDefaultSettings(EACFTransitionEffect::BlackFade, EACFTransitionPhase::Close);
	const FACFTransitionSettings Open = Preset ? Preset->Open : ACFTransition::MakeDefaultSettings(EACFTransitionEffect::BlackFade, EACFTransitionPhase::Open);
	return PlayLevelTransition(Close, Open, Options, LevelName, TravelOptions);
}

FACFTransitionHandle UACFTransitionSubsystem::PlayPreview(const FACFTransitionSettings& Close, const FACFTransitionSettings& Open, float HoldSeconds)
{
	const UWorld* World = GetGameWorld();
	if (!World || !World->GetGameViewport() || IsTransitionActive() || bStill
		|| GetScreenState() != EACFTransitionScreenState::Clear || !Close.IsValid() || !Open.IsValid())
	{
		return FACFTransitionHandle();
	}

	FRequest Request;
	Request.Kind = EACFTransitionKind::Preview;
	Request.Close = Close;
	Request.Open = Open;
	Request.PreviewHoldSeconds = FMath::IsFinite(HoldSeconds) ? FMath::Max(0.f, HoldSeconds) : 0.f;
	Request.Options.Concurrency = EACFTransitionConcurrency::Reject;
	return Submit(MoveTemp(Request));
}

FACFTransitionHandle UACFTransitionSubsystem::Submit(FRequest&& Request)
{
	Request.Handle = FACFTransitionHandle(NextHandleId++);
	const FACFTransitionHandle Handle = Request.Handle;
	FACFTransitionSettings& First = Request.Kind == EACFTransitionKind::Open ? Request.Open : Request.Close;
	if (!First.IsValid() || (Request.Kind == EACFTransitionKind::Level && !Request.Open.IsValid()))
	{
		UE_LOG(LogACFTransition, Warning, TEXT("ACF Transition: %s has no valid effect; Black Fade is used."), *Handle.ToString());
		if (!Request.Close.IsValid()) { Request.Close.Effect = EACFTransitionEffect::BlackFade; }
		if (!Request.Open.IsValid()) { Request.Open.Effect = EACFTransitionEffect::BlackFade; }
	}
	// Every request goes through Incoming. Outside an event it is applied before Submit returns; inside an event (a
	// listener calls Play*) it waits until the operation that sends the event has finished its own state change, so that
	// operation never overwrites a transition a listener started (README 33 章「イベントと再入」).
	Incoming.Add(MoveTemp(Request));
	Settle(true);
	return Handle;
}

void UACFTransitionSubsystem::Settle(bool bStartQueued)
{
	if (DispatchDepth > 0 || bSettling)
	{
		return; // the outermost operation settles when its listeners have returned
	}
	TGuardValue<bool> Guard(bSettling, true);
	// Bounded: listeners that answer every event with a new request cannot hang the game thread (the rest: next tick).
	for (int32 Step = 0; Step < 256; ++Step)
	{
		if (!Incoming.IsEmpty())
		{
			FRequest Next = MoveTemp(Incoming[0]);
			Incoming.RemoveAt(0);
			Admit(MoveTemp(Next));
			continue;
		}
		if (bStartQueued && !Active.IsSet() && !Queue.IsEmpty())
		{
			StartNextQueued();
			continue;
		}
		return;
	}
	UE_LOG(LogACFTransition, Warning, TEXT("ACF Transition: listeners keep requesting transitions; %d request(s) wait for the next tick."), Incoming.Num());
}

void UACFTransitionSubsystem::Admit(FRequest&& Request)
{
	const FACFTransitionHandle Handle = Request.Handle;
	if (bStill)
	{
		bStill = false; // a real request replaces a still frame (it keeps the coverage for continuity)
	}
	if (Active.IsSet() || !Queue.IsEmpty())
	{
		switch (Request.Options.Concurrency)
		{
		case EACFTransitionConcurrency::Reject:
			UE_LOG(LogACFTransition, Log, TEXT("ACF Transition: %s rejected (%s is playing)."), *Handle.ToString(), *GetActiveHandle().ToString());
			FinishedEvents.Add(Handle, EACFTransitionEvent::Failed);
			EmitDeferred(Handle, EACFTransitionEvent::Failed);
			return;
		case EACFTransitionConcurrency::Queue:
			Queue.Add(MoveTemp(Request));
			return;
		default:
			if (Active.IsSet())
			{
				// The new request stays visible (Starting) while the old handle sends Interrupted: a listener can Cancel
				// it or ClearScreen; its own Play* requests wait in Incoming and come after this one.
				Starting.Emplace(MoveTemp(Request));
				bStartingCancelled = false;
				EndActive(EACFTransitionEvent::Interrupted);
				FRequest Next = MoveTemp(*Starting);
				Starting.Reset();
				if (bStartingCancelled)
				{
					Emit(Next, EACFTransitionEvent::Cancelled);
					return;
				}
				if (!ensureMsgf(!Active.IsSet(), TEXT("ACF Transition: a transition became active while %s was interrupted."), *Handle.ToString()))
				{
					EndActive(EACFTransitionEvent::Interrupted);
				}
				Start(MoveTemp(Next));
				return;
			}
			break;
		}
	}
	Start(MoveTemp(Request));
}

void UACFTransitionSubsystem::Start(FRequest&& Request)
{
	const UACFTransitionProjectSettings* Settings = GetDefault<UACFTransitionProjectSettings>();
	Active.Emplace(MoveTemp(Request));
	FRequest& R = *Active;
	const FACFTransitionHandle Handle = R.Handle;
	EffectTime = 0.f;
	PhaseTime = 0.f;
	StageWait = 0.f;
	bReadySignal = false;
	bLoadFailed = false;
	bOpeningAfterFailure = false;
	bLevelOpened = false;
	WorldFramesSinceLoad = 0;

	if (R.Options.bLockInput)
	{
		ApplyInputLock(true);
	}

	if (R.Kind == EACFTransitionKind::Open)
	{
		DrawSettings = R.Open;
		DrawPhase = EACFTransitionPhase::Open;
		// Continue from what is on screen: a clear screen starts fully covered.
		PhaseStartAlpha = (bOverlayAttached || Coverage > 0.f) ? FMath::Clamp(1.f - Coverage, 0.f, 1.f) : 0.f;
		if (!ACFTransition::GetEffectInfo(R.Open.Effect).bOpenUsesSnapshot)
		{
			ReleaseCapture();
		}
		Coverage = 1.f - ACFTransition::Ease(DrawSettings, PhaseStartAlpha);
		SetStage(EStage::Opening);
	}
	else
	{
		DrawSettings = R.Close;
		DrawPhase = EACFTransitionPhase::Close;
		const bool bAlreadyCovered = Coverage >= 1.f;
		const bool bClear = !bOverlayAttached && Coverage <= 0.f;
		PhaseStartAlpha = bClear ? 0.f : FMath::Clamp(Coverage, 0.f, 1.f);
		if (bClear)
		{
			ReleaseCapture();
			if (Settings->bCaptureScreen && ACFTransition::GetEffectInfo(R.Close.Effect).bUsesSnapshot)
			{
				StartCapture();
			}
		}
		if (bAlreadyCovered)
		{
			Coverage = 1.f;
			SetStage(EStage::CoveredWait);
			CoveredFrame = 0; // already presented covered
		}
		else
		{
			SetStage(EStage::Closing);
		}
	}

	PlaySound(Handle, DrawSettings);

	UE_LOG(LogACFTransition, Verbose, TEXT("ACF Transition: %s started (%s %s)."), *Handle.ToString(), *UEnum::GetValueAsString(R.Kind), *ACFTransition::GetEffectInfo(DrawSettings.Effect).Name);
	Emit(R, EACFTransitionEvent::Started);
	if (Active.IsSet() && Active->Handle == Handle)
	{
		UpdateView();
	}
}

// ------------------------------------------------------------------------------------------------ control

bool UACFTransitionSubsystem::Cancel(FACFTransitionHandle Handle, bool bClearScreen)
{
	if (Active.IsSet() && Active->Handle == Handle)
	{
		EndActive(EACFTransitionEvent::Cancelled);
		Settle(false); // what the Cancelled listeners requested starts from the screen as it is (e.g. an Open)
		if (bClearScreen && !Active.IsSet())
		{
			Coverage = 0.f;
			DetachOverlay();
		}
		Settle(true);
		return true;
	}
	if (Starting.IsSet() && Starting->Handle == Handle)
	{
		if (bStartingCancelled)
		{
			return false;
		}
		bStartingCancelled = true; // ends with Cancelled (without Started) once the interrupted handle's event returns
		return true;
	}
	for (TArray<FRequest>* List : { &Queue, &Incoming })
	{
		const int32 Index = List->IndexOfByPredicate([Handle](const FRequest& R) { return R.Handle == Handle; });
		if (Index != INDEX_NONE)
		{
			FRequest Removed = MoveTemp((*List)[Index]);
			List->RemoveAt(Index);
			Emit(Removed, EACFTransitionEvent::Cancelled);
			Settle(true);
			return true;
		}
	}
	return false;
}

bool UACFTransitionSubsystem::Skip(FACFTransitionHandle Handle)
{
	if (!Active.IsSet() || Active->Handle != Handle)
	{
		return false;
	}
	if (Stage == EStage::Closing || Stage == EStage::Opening)
	{
		if (PendingCapture.IsValid())
		{
			SkipPendingCapture(); // a Close waiting for its GPU capture must not wait any longer
		}
		PhaseTime = FMath::Max(PhaseTime, 1.0e6f);
		return true;
	}
	return false;
}

void UACFTransitionSubsystem::ClearScreen()
{
	// Everything requested before this call ends with Cancelled: the active handle, an Interrupt request being started,
	// the queue and the requests waiting in Incoming. What the listeners of these events request starts afterwards, from
	// the clear screen.
	TArray<FRequest> Dropped = MoveTemp(Queue);
	Queue.Reset();
	Dropped.Append(MoveTemp(Incoming));
	Incoming.Reset();
	if (Starting.IsSet())
	{
		bStartingCancelled = true;
	}
	if (Active.IsSet())
	{
		EndActive(EACFTransitionEvent::Cancelled);
	}
	for (FRequest& R : Dropped)
	{
		Emit(R, EACFTransitionEvent::Cancelled);
	}
	if (!Active.IsSet())
	{
		bStill = false;
		Coverage = 0.f;
		DetachOverlay();
	}
	Settle(true);
}

bool UACFTransitionSubsystem::NotifyLoadStarted(FACFTransitionHandle Handle)
{
	if (!Active.IsSet() || Active->Handle != Handle || Active->Kind != EACFTransitionKind::Level || Stage != EStage::WaitingForLoad)
	{
		return false;
	}
	SetStage(EStage::Loading);
	Emit(*Active, EACFTransitionEvent::LoadStarted);
	Settle(true);
	return true;
}

bool UACFTransitionSubsystem::NotifyLoadCompleted(FACFTransitionHandle Handle)
{
	if (!Active.IsSet() || Active->Handle != Handle || Active->Kind != EACFTransitionKind::Level || !(Stage == EStage::WaitingForLoad || Stage == EStage::Loading))
	{
		return false;
	}
	if (Stage == EStage::WaitingForLoad)
	{
		NotifyLoadStarted(Handle);
	}
	if (Active.IsSet() && Active->Handle == Handle)
	{
		SetStage(EStage::WaitingWorld);
	}
	return true;
}

bool UACFTransitionSubsystem::NotifyWorldReady(FACFTransitionHandle Handle)
{
	if (!Active.IsSet() || Active->Kind != EACFTransitionKind::Level || (Handle.IsValid() && Active->Handle != Handle))
	{
		return false;
	}
	bReadySignal = true;
	return true;
}

bool UACFTransitionSubsystem::NotifyLoadFailed(FACFTransitionHandle Handle)
{
	if (!Active.IsSet() || Active->Kind != EACFTransitionKind::Level || (Handle.IsValid() && Active->Handle != Handle))
	{
		return false;
	}
	if (Stage == EStage::Opening)
	{
		return false;
	}
	UE_LOG(LogACFTransition, Warning, TEXT("ACF Transition: %s: load failed; opening the screen."), *Active->Handle.ToString());
	bLoadFailed = true;
	BeginOpening(true);
	return true;
}

bool UACFTransitionSubsystem::ShowStill(const FACFTransitionSettings& Settings, EACFTransitionPhase Phase, float InCoverage)
{
	if (IsTransitionActive())
	{
		return false;
	}
	const bool bWasClear = !bOverlayAttached;
	DrawSettings = Settings;
	DrawPhase = Phase;
	if (bWasClear && Phase == EACFTransitionPhase::Close && ACFTransition::GetEffectInfo(Settings.Effect).bUsesSnapshot && GetDefault<UACFTransitionProjectSettings>()->bCaptureScreen)
	{
		UGameInstance* GI = GetGameInstance();
		ReleaseCapture();
		Capture = FACFTransitionView::CaptureViewport(GI ? GI->GetGameViewportClient() : nullptr, GetDefault<UACFTransitionProjectSettings>()->MaxCaptureWidth);
	}
	Coverage = FMath::Clamp(InCoverage, 0.f, 1.f);
	bStill = true;
	UpdateView();
	return true;
}

// ------------------------------------------------------------------------------------------------ query

EACFTransitionScreenState UACFTransitionSubsystem::GetScreenState() const
{
	if (Active.IsSet() && (Stage == EStage::Closing || Stage == EStage::Opening))
	{
		return EACFTransitionScreenState::Playing;
	}
	if (Coverage >= 1.f)
	{
		return EACFTransitionScreenState::Covered;
	}
	return Coverage > 0.f ? EACFTransitionScreenState::Playing : EACFTransitionScreenState::Clear;
}

bool UACFTransitionSubsystem::IsTransitionActive() const
{
	return Active.IsSet() || !Queue.IsEmpty() || !Incoming.IsEmpty() || Starting.IsSet();
}

FACFTransitionHandle UACFTransitionSubsystem::GetActiveHandle() const
{
	return Active.IsSet() ? Active->Handle : FACFTransitionHandle();
}

FACFTransitionInfo UACFTransitionSubsystem::GetTransitionInfo(FACFTransitionHandle Handle) const
{
	FACFTransitionInfo Info;
	Info.Handle = Handle;
	if (Active.IsSet() && Active->Handle == Handle)
	{
		Info.bActive = true;
		Info.Kind = Active->Kind;
		Info.Effect = DrawSettings.Effect;
		Info.Phase = DrawPhase;
		const float Duration = DrawSettings.Duration;
		Info.Progress = (Stage == EStage::Closing || Stage == EStage::Opening) ? (Duration > 0.f ? FMath::Clamp(PhaseStartAlpha + PhaseTime / Duration, 0.f, 1.f) : 1.f) : 1.f;
		Info.Coverage = Coverage;
		Info.LastEvent = Active->LastEvent;
		return Info;
	}
	// Queued, waiting in Incoming or being started: not on screen yet.
	auto Waiting = [&Info](const FRequest& R)
	{
		Info.bQueued = true;
		Info.Kind = R.Kind;
		Info.Effect = R.Kind == EACFTransitionKind::Open ? R.Open.Effect : R.Close.Effect;
		Info.Phase = R.Kind == EACFTransitionKind::Open ? EACFTransitionPhase::Open : EACFTransitionPhase::Close;
	};
	for (const TArray<FRequest>* List : { &Queue, &Incoming })
	{
		if (const FRequest* R = List->FindByPredicate([Handle](const FRequest& Q) { return Q.Handle == Handle; }))
		{
			Waiting(*R);
			return Info;
		}
	}
	if (Starting.IsSet() && Starting->Handle == Handle)
	{
		Waiting(*Starting);
		return Info;
	}
	if (const EACFTransitionEvent* Ended = FinishedEvents.Find(Handle))
	{
		Info.LastEvent = *Ended;
		Info.Progress = 1.f;
	}
	return Info;
}

// ------------------------------------------------------------------------------------------------ tick

ETickableTickType UACFTransitionSubsystem::GetTickableTickType() const
{
	return HasAnyFlags(RF_ClassDefaultObject) ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UACFTransitionSubsystem::IsTickable() const
{
	return bInitialized && (IsTransitionActive() || !PendingEvents.IsEmpty());
}

TStatId UACFTransitionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UACFTransitionSubsystem, STATGROUP_Tickables);
}

UWorld* UACFTransitionSubsystem::GetTickableGameObjectWorld() const
{
	return nullptr; // ticks across map changes
}

void UACFTransitionSubsystem::Tick(float DeltaTime)
{
	const double Start = FPlatformTime::Seconds();
	TickTransitions(DeltaTime);
	LastTickSeconds = FPlatformTime::Seconds() - Start;
}

void UACFTransitionSubsystem::TickTransitions(float DeltaSeconds)
{
	if (!PendingEvents.IsEmpty())
	{
		TArray<TPair<FACFTransitionHandle, EACFTransitionEvent>> Events = MoveTemp(PendingEvents);
		PendingEvents.Reset();
		for (const TPair<FACFTransitionHandle, EACFTransitionEvent>& Event : Events)
		{
			Broadcast(Event.Key, Event.Value);
		}
	}
	// Requests of listeners and queued transitions; and once more on every way out of this tick.
	Settle(true);
	ON_SCOPE_EXIT { Settle(true); };
	if (!Active.IsSet())
	{
		return;
	}
	const UACFTransitionProjectSettings* Settings = GetDefault<UACFTransitionProjectSettings>();
	if (Active->Options.bLockInput)
	{
		ApplyInputLock(true); // controllers spawned / restarted since the last tick (also while waiting for a load)
	}
	if (!Active->Options.bPlayWhenPaused)
	{
		if (UWorld* World = GetGameWorld(); World && World->IsPaused())
		{
			return;
		}
	}
	const float RawDelta = FMath::Max(DeltaSeconds, 0.f);
	const float Delta = FMath::Min(RawDelta, Settings->MaxDeltaSeconds);
	EffectTime += Delta;
	// A capture-based Close starts drawing once its capture is ready (the screen is unchanged until then).
	const bool bWaitingForCapture = PendingCapture.IsValid() && !ResolvePendingCapture();
	const FACFTransitionHandle Handle = Active->Handle;
	auto StillMine = [this, Handle]() { return Active.IsSet() && Active->Handle == Handle; };

	switch (Stage)
	{
	case EStage::Closing:
	{
		if (bWaitingForCapture)
		{
			break;
		}
		PhaseTime += Delta;
		const float Duration = DrawSettings.Duration;
		const float Alpha = Duration > 0.f ? FMath::Clamp(PhaseStartAlpha + PhaseTime / Duration, 0.f, 1.f) : 1.f;
		Coverage = ACFTransition::Ease(DrawSettings, Alpha);
		if (Alpha >= 1.f)
		{
			EnterCovered();
		}
		break;
	}
	case EStage::CoveredWait:
		if (GFrameCounter >= CoveredFrame + static_cast<uint64>(Settings->CoveredFrames))
		{
			if (Active->Kind == EACFTransitionKind::Close)
			{
				// The handle ends before its events are sent, so a listener can start the next transition at once.
				FRequest Done = MoveTemp(*Active);
				Active.Reset();
				SetStage(EStage::None);
				ApplyInputLock(false);
				EndSounds(Done.Handle, EACFTransitionEvent::Finished);
				if (!Done.Options.bHoldWhenCovered)
				{
					Coverage = 0.f;
					DetachOverlay();
				}
				Emit(Done, EACFTransitionEvent::Covered);
				Emit(Done, EACFTransitionEvent::Finished);
				return; // then Settle: what the listeners requested, then the queue
			}
			if (Active->Kind == EACFTransitionKind::Preview)
			{
				SetStage(EStage::PreviewHold);
				Emit(*Active, EACFTransitionEvent::Covered);
				break;
			}
			SetStage(EStage::WaitingForLoad);
			if (Settings->bUseMoviePlayerDuringLoad && !GIsEditor && IsMoviePlayerEnabled() && !Active->Options.bManualLoad && GetMoviePlayer())
			{
				FLoadingScreenAttributes Attributes;
				Attributes.bAutoCompleteWhenLoadingCompletes = true;
				Attributes.MinimumLoadingScreenDisplayTime = 0.f;
				Attributes.WidgetLoadingScreen = SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(DrawSettings.Color)
					.HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(0.f, 0.f, 96.f, 64.f))
					[
						SNew(STextBlock).Text(DrawSettings.LoadingText.IsEmpty() ? Active->Open.LoadingText : DrawSettings.LoadingText).Font(FCoreStyle::GetDefaultFontStyle("Bold", 30))
					];
				GetMoviePlayer()->SetupLoadingScreen(Attributes);
			}
			Emit(*Active, EACFTransitionEvent::Covered);
			// What the Covered listeners requested is applied before the load starts: an Interrupt / Cancel / ClearScreen
			// there means this transition does not load (the map must not load under another transition).
			Settle(false);
			if (!StillMine())
			{
				return;
			}
			// Who loads (README 33 章「ロードの担当」): with bManualLoad the game does, also when a LevelName is given (it is
			// then only information); otherwise ACF opens the LevelName, or the game loads when there is none.
			if (!Active->Options.bManualLoad && !Active->LevelName.IsNone() && !bLevelOpened)
			{
				bLevelOpened = true;
				if (UWorld* World = GetGameWorld())
				{
					UE_LOG(LogACFTransition, Log, TEXT("ACF Transition: %s: OpenLevel %s."), *Handle.ToString(), *Active->LevelName.ToString());
					UGameplayStatics::OpenLevel(World, Active->LevelName, true, Active->TravelOptions);
				}
				else
				{
					NotifyLoadFailed(Handle);
				}
			}
		}
		break;
	case EStage::PreviewHold:
		StageWait += Delta;
		if (StageWait >= Active->PreviewHoldSeconds)
		{
			BeginOpening(false);
		}
		break;
	case EStage::WaitingForLoad:
	case EStage::Loading:
		StageWait += RawDelta;
		if (StageWait > GetTimeoutSeconds())
		{
			UE_LOG(LogACFTransition, Warning, TEXT("ACF Transition: %s: no load after %.1f s; opening the screen."), *Handle.ToString(), StageWait);
			NotifyLoadFailed(Handle);
		}
		break;
	case EStage::WaitingWorld:
	{
		StageWait += RawDelta;
		++WorldFramesSinceLoad;
		UWorld* World = GetGameWorld();
		const bool bWorldReady = Active->Options.bManualLoad || (World && World->HasBegunPlay());
		const bool bSignal = !Active->Options.bWaitForReadySignal || bReadySignal;
		if (bWorldReady && bSignal && WorldFramesSinceLoad >= Settings->WorldReadyFrames)
		{
			Emit(*Active, EACFTransitionEvent::LoadCompleted);
			if (!StillMine())
			{
				return;
			}
			if (Active->Options.bLockInput)
			{
				ApplyInputLock(true); // the new world's controllers
			}
			BeginOpening(false);
		}
		else if (StageWait > GetTimeoutSeconds())
		{
			UE_LOG(LogACFTransition, Warning, TEXT("ACF Transition: %s: the world was not ready after %.1f s (%s); opening the screen."), *Handle.ToString(), StageWait, bSignal ? TEXT("world") : TEXT("no NotifyWorldReady"));
			BeginOpening(true);
		}
		break;
	}
	case EStage::Opening:
	{
		PhaseTime += Delta;
		const float Duration = DrawSettings.Duration;
		const float Alpha = Duration > 0.f ? FMath::Clamp(PhaseStartAlpha + PhaseTime / Duration, 0.f, 1.f) : 1.f;
		Coverage = 1.f - ACFTransition::Ease(DrawSettings, Alpha);
		if (Alpha >= 1.f)
		{
			Coverage = 0.f;
			EndActive(bOpeningAfterFailure ? EACFTransitionEvent::Failed : EACFTransitionEvent::Finished);
			Settle(false); // a listener's next transition starts on the overlay as it is
			if (!Active.IsSet() && Coverage <= 0.f)
			{
				DetachOverlay();
			}
			return;
		}
		break;
	}
	default:
		break;
	}
	if (StillMine())
	{
		UpdateView();
	}
}

void UACFTransitionSubsystem::EnterCovered()
{
	Coverage = 1.f;
	SetStage(EStage::CoveredWait);
	CoveredFrame = GFrameCounter;
	UpdateView();
}

void UACFTransitionSubsystem::BeginOpening(bool bFailed)
{
	if (!Active.IsSet())
	{
		return;
	}
	bOpeningAfterFailure = bFailed;
	DrawSettings = Active->Open;
	DrawPhase = EACFTransitionPhase::Open;
	if (!ACFTransition::GetEffectInfo(DrawSettings.Effect).bOpenUsesSnapshot)
	{
		ReleaseCapture();
	}
	PhaseTime = 0.f;
	PhaseStartAlpha = FMath::Clamp(1.f - Coverage, 0.f, 1.f);
	SetStage(EStage::Opening);
	PlaySound(Active->Handle, DrawSettings);
	UpdateView();
}

void UACFTransitionSubsystem::SetStage(EStage NewStage)
{
	Stage = NewStage;
	StageWait = 0.f;
	if (NewStage == EStage::WaitingWorld)
	{
		WorldFramesSinceLoad = 0;
	}
}

void UACFTransitionSubsystem::StartNextQueued()
{
	if (!Active.IsSet() && !Queue.IsEmpty())
	{
		FRequest Next = MoveTemp(Queue[0]);
		Queue.RemoveAt(0);
		Start(MoveTemp(Next));
	}
}

void UACFTransitionSubsystem::EndActive(EACFTransitionEvent Terminal)
{
	if (!Active.IsSet())
	{
		return;
	}
	FRequest Done = MoveTemp(*Active);
	Active.Reset();
	SetStage(EStage::None);
	ApplyInputLock(false);
	EndSounds(Done.Handle, Terminal);
	Emit(Done, Terminal);
}

void UACFTransitionSubsystem::PlaySound(FACFTransitionHandle Handle, const FACFTransitionSettings& Settings)
{
	USoundBase* Sound = Settings.Sound;
	UWorld* World = Sound ? GetGameWorld() : nullptr;
	if (!World)
	{
		return;
	}
	// 2D, survives a map load (the Close sound of a level transition goes on while the new map loads), destroyed by the
	// audio device when it has finished. Null without an audio device (-nosound, dedicated server).
	UAudioComponent* Component = UGameplayStatics::SpawnSound2D(World, Sound, 1.f, 1.f, 0.f, nullptr, /*bPersistAcrossLevelTransition*/ true, /*bAutoDestroy*/ true);
	if (Component && Settings.SoundStop != EACFTransitionSoundStop::PlayToEnd)
	{
		Sounds.RemoveAll([](const FTransitionSound& Tracked) { return !Tracked.Component.IsValid() || !Tracked.Component->IsPlaying(); });
		Sounds.Add({ Handle, Component, Settings.SoundStop, FMath::Max(Settings.SoundFadeOutSeconds, 0.f) });
	}
}

void UACFTransitionSubsystem::EndSounds(FACFTransitionHandle Handle, EACFTransitionEvent Terminal)
{
	for (int32 Index = Sounds.Num() - 1; Index >= 0; --Index)
	{
		const FTransitionSound Tracked = Sounds[Index];
		if (Tracked.Handle != Handle)
		{
			continue;
		}
		Sounds.RemoveAt(Index);
		const bool bStop = Tracked.Stop == EACFTransitionSoundStop::StopWithTransition
			|| (Tracked.Stop == EACFTransitionSoundStop::StopIfAborted && Terminal != EACFTransitionEvent::Finished);
		UAudioComponent* Component = Tracked.Component.Get();
		if (!bStop || !Component || !Component->IsPlaying())
		{
			continue;
		}
		if (Tracked.FadeOutSeconds > 0.f)
		{
			Component->FadeOut(Tracked.FadeOutSeconds, 0.f);
		}
		else
		{
			Component->Stop();
		}
	}
}

int32 UACFTransitionSubsystem::GetTrackedSoundCount() const
{
	int32 Count = 0;
	for (const FTransitionSound& Tracked : Sounds)
	{
		Count += Tracked.Component.IsValid() && Tracked.Component->IsPlaying() ? 1 : 0;
	}
	return Count;
}

void UACFTransitionSubsystem::Emit(FRequest& Request, EACFTransitionEvent Event)
{
	if (!ensureMsgf(!ACFTransition::IsTerminal(Request.LastEvent), TEXT("ACF Transition: %s already ended with %s; %s is not sent."),
		*Request.Handle.ToString(), ACFTransition::LexEvent(Request.LastEvent), ACFTransition::LexEvent(Event)))
	{
		return; // at most one terminal event per handle, nothing after it
	}
	Request.LastEvent = Event;
	if (ACFTransition::IsTerminal(Event))
	{
		if (FinishedEvents.Num() > 256)
		{
			FinishedEvents.Reset();
		}
		FinishedEvents.Add(Request.Handle, Event);
	}
	UE_LOG(LogACFTransition, Verbose, TEXT("ACF Transition: %s %s."), *Request.Handle.ToString(), ACFTransition::LexEvent(Event));
	Broadcast(Request.Handle, Event);
}

void UACFTransitionSubsystem::Broadcast(FACFTransitionHandle Handle, EACFTransitionEvent Event)
{
	// While listeners run, Play* requests wait in Incoming (Submit -> Settle returns at once).
	++DispatchDepth;
	ON_SCOPE_EXIT { --DispatchDepth; };
	OnTransitionEventNative.Broadcast(Handle, Event);
	OnTransitionEvent.Broadcast(Handle, Event);
}

void UACFTransitionSubsystem::EmitDeferred(FACFTransitionHandle Handle, EACFTransitionEvent Event)
{
	PendingEvents.Emplace(Handle, Event);
}

// ------------------------------------------------------------------------------------------------ view / input

const FACFTransitionSettings& UACFTransitionSubsystem::CurrentSettings() const
{
	return DrawSettings;
}

void UACFTransitionSubsystem::UpdateView()
{
	if (Coverage <= 0.f && !(Active.IsSet() && Stage == EStage::Opening))
	{
		if (!Active.IsSet())
		{
			DetachOverlay();
		}
		return;
	}
	AttachOverlay();
	if (View.IsValid())
	{
		const FACFTransitionEffectInfo& Info = ACFTransition::GetEffectInfo(DrawSettings.Effect);
		const bool bUseCapture = DrawPhase == EACFTransitionPhase::Close ? Info.bUsesSnapshot : Info.bOpenUsesSnapshot;
		View->Apply(DrawSettings, DrawPhase, Coverage, EffectTime, bUseCapture ? Capture.Get() : nullptr, bInputLocked);
	}
}

void UACFTransitionSubsystem::AttachOverlay()
{
	if (!View.IsValid())
	{
		View = MakeShared<FACFTransitionView>();
	}
	if (bOverlayAttached)
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UGameViewportClient* Viewport = GI ? GI->GetGameViewportClient() : nullptr;
	if (Viewport)
	{
		Viewport->AddViewportWidgetContent(View->GetWidget(), GetDefault<UACFTransitionProjectSettings>()->OverlayZOrder);
		AttachedViewport = Viewport;
		bOverlayAttached = true;
	}
}

void UACFTransitionSubsystem::DetachOverlay()
{
	if (bOverlayAttached && View.IsValid())
	{
		if (UGameViewportClient* Viewport = AttachedViewport.Get())
		{
			Viewport->RemoveViewportWidgetContent(View->GetWidget());
		}
	}
	bOverlayAttached = false;
	AttachedViewport.Reset();
	if (View.IsValid())
	{
		View->Release();
		View.Reset();
	}
	ReleaseCapture();
	if (!Active.IsSet())
	{
		DrawSettings = FACFTransitionSettings(); // nothing is drawn: no curve / texture / material / sound kept
	}
}

void UACFTransitionSubsystem::StartCapture()
{
	const UACFTransitionProjectSettings* Settings = GetDefault<UACFTransitionProjectSettings>();
	UGameInstance* GI = GetGameInstance();
	UGameViewportClient* ViewportClient = GI ? GI->GetGameViewportClient() : nullptr;
	LastCaptureWaitFrames = 0;
	bLastCaptureFellBack = false;
	if (Settings->bCaptureOnGpu)
	{
		PendingCapture = FACFTransitionView::RequestGpuCapture(ViewportClient);
	}
	if (!PendingCapture.IsValid())
	{
		bLastCaptureFellBack = Settings->bCaptureOnGpu;
		Capture = FACFTransitionView::CaptureViewport(ViewportClient, Settings->MaxCaptureWidth);
	}
}

bool UACFTransitionSubsystem::ResolvePendingCapture()
{
	using EState = FACFTransitionGpuCapture::EState;
	const EState State = PendingCapture->GetState();
	LastCaptureWaitFrames = static_cast<int32>(PendingCapture->GetFramesWaited());
	UGameInstance* GI = GetGameInstance();
	UGameViewportClient* ViewportClient = GI ? GI->GetGameViewportClient() : nullptr;
	if (State == EState::Ready && PendingCapture->MatchesViewport(ViewportClient))
	{
		Capture = PendingCapture->GetTexture();
		PendingCapture.Reset();
		return true;
	}
	const UACFTransitionProjectSettings* Settings = GetDefault<UACFTransitionProjectSettings>();
	if (State == EState::Pending && LastCaptureWaitFrames <= Settings->MaxCaptureWaitFrames)
	{
		return false;
	}
	// Failed (HDR / non-BGRA back buffer), the viewport was resized / moved since the request, or not presented in time:
	// the synchronous read back, and without it the effect's procedural look. The transition always goes on.
	UE_LOG(LogACFTransition, Log, TEXT("Screen capture on the GPU %s after %d frames; using the read back."),
		State == EState::Failed ? TEXT("failed") : (State == EState::Ready ? TEXT("is stale (the viewport changed)") : TEXT("not ready")), LastCaptureWaitFrames);
	// Dropping the request removes its render thread listener; the render target is left to the GC (it is never
	// destroyed while a render command that was queued before can still copy into it, see README 33 章「キャプチャ」).
	PendingCapture.Reset();
	bLastCaptureFellBack = true;
	Capture = FACFTransitionView::CaptureViewport(ViewportClient, Settings->MaxCaptureWidth);
	return true;
}

int32 UACFTransitionSubsystem::GetPendingCaptureState() const
{
	return PendingCapture.IsValid() ? static_cast<int32>(PendingCapture->GetState()) : -1;
}

void UACFTransitionSubsystem::SkipPendingCapture()
{
	// Skip never waits for the GPU and never reads the screen back: a copy that is already there is used, otherwise the
	// effect continues with its procedural look (the Close is covered within a few frames anyway).
	using EState = FACFTransitionGpuCapture::EState;
	LastCaptureWaitFrames = static_cast<int32>(PendingCapture->GetFramesWaited());
	UGameInstance* GI = GetGameInstance();
	if (PendingCapture->GetState() == EState::Ready && PendingCapture->MatchesViewport(GI ? GI->GetGameViewportClient() : nullptr))
	{
		Capture = PendingCapture->GetTexture();
	}
	else
	{
		bLastCaptureFellBack = true;
		UE_LOG(LogACFTransition, Verbose, TEXT("ACF Transition: Skip while the screen capture is pending; the effect draws without it."));
	}
	PendingCapture.Reset();
}

void UACFTransitionSubsystem::ReleaseCapture()
{
	// No MarkAsGarbage: the overlay's material instance may still reference the capture (Snapshot parameter) and draw
	// with it on the render thread; the GC frees it once nothing references it.
	PendingCapture.Reset(); // cancels the request
	Capture = nullptr;
}

void UACFTransitionSubsystem::ApplyInputLock(bool bLock)
{
	if (bLock)
	{
		bInputLocked = true;
		LockedControllers.RemoveAll([](const FInputLock& Lock) { return !Lock.Controller.IsValid(); });
		UWorld* World = GetGameWorld();
		if (!World)
		{
			return;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (!PC || !PC->IsLocalController())
			{
				continue;
			}
			FInputLock* Lock = LockedControllers.FindByPredicate([PC](const FInputLock& L) { return L.Controller.Get() == PC; });
			if (!Lock)
			{
				Lock = &LockedControllers.AddDefaulted_GetRef();
				Lock->Controller = PC;
			}
			// One lock per controller. Taken again only when it is gone (ClientRestart / ResetIgnoreInputFlags cleared the
			// counters, e.g. a respawn): otherwise another system's lock would be given back by ours.
			if (!Lock->bMove || !PC->IsMoveInputIgnored())
			{
				PC->SetIgnoreMoveInput(true);
				Lock->bMove = true;
			}
			if (!Lock->bLook || !PC->IsLookInputIgnored())
			{
				PC->SetIgnoreLookInput(true);
				Lock->bLook = true;
			}
		}
	}
	else
	{
		for (const FInputLock& Lock : LockedControllers)
		{
			if (APlayerController* PC = Lock.Controller.Get())
			{
				if (Lock.bMove) { PC->SetIgnoreMoveInput(false); }
				if (Lock.bLook) { PC->SetIgnoreLookInput(false); }
			}
		}
		LockedControllers.Reset();
		bInputLocked = false;
		if (View.IsValid() && bOverlayAttached)
		{
			UpdateView();
		}
	}
}

float UACFTransitionSubsystem::GetTimeoutSeconds() const
{
	const float FromOptions = Active.IsSet() ? Active->Options.TimeoutSeconds : 0.f;
	return FromOptions > 0.f ? FromOptions : GetDefault<UACFTransitionProjectSettings>()->LevelTimeoutSeconds;
}

UWorld* UACFTransitionSubsystem::GetGameWorld() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetWorld() : nullptr;
}

// ------------------------------------------------------------------------------------------------ map loads

void UACFTransitionSubsystem::HandlePreLoadMap(const FString& MapName)
{
	if (!Active.IsSet() || Active->Kind != EACFTransitionKind::Level || Active->Options.bManualLoad)
	{
		if (Coverage > 0.f && Coverage < 1.f)
		{
			UE_LOG(LogACFTransition, Warning, TEXT("ACF Transition: a map load (%s) started while the screen is only %.0f%% covered."), *MapName, Coverage * 100.f);
		}
		return;
	}
	if (Stage == EStage::Closing || Stage == EStage::CoveredWait)
	{
		// The game loaded without waiting for Covered: draw covered from now on and send the late Covered.
		UE_LOG(LogACFTransition, Warning, TEXT("ACF Transition: %s: the map load started before Covered (call OpenLevel after Covered)."), *Active->Handle.ToString());
		Coverage = 1.f;
		UpdateView();
		const FACFTransitionHandle Handle = Active->Handle;
		Emit(*Active, EACFTransitionEvent::Covered);
		if (!Active.IsSet() || Active->Handle != Handle)
		{
			return;
		}
		SetStage(EStage::WaitingForLoad);
	}
	if (Stage == EStage::WaitingForLoad)
	{
		SetStage(EStage::Loading);
		Emit(*Active, EACFTransitionEvent::LoadStarted);
	}
	Settle(true);
}

void UACFTransitionSubsystem::HandlePostLoadMap(UWorld* World)
{
	if (World && World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	if (Active.IsSet() && Active->Kind == EACFTransitionKind::Level && !Active->Options.bManualLoad && (Stage == EStage::Loading || Stage == EStage::WaitingForLoad))
	{
		if (!World)
		{
			NotifyLoadFailed(Active->Handle);
			return;
		}
		SetStage(EStage::WaitingWorld);
	}
	if (Active.IsSet() && Active->Options.bLockInput)
	{
		// The new world's local controllers exist now (spawned by the load): locked before they get a tick, also while
		// the transition waits for the world / NotifyWorldReady.
		ApplyInputLock(true);
	}
	// The viewport keeps its widgets through a map load; re-add the overlay if something removed it.
	if (bOverlayAttached && View.IsValid() && !View->GetWidget()->GetParentWidget().IsValid())
	{
		bOverlayAttached = false;
		AttachOverlay();
		UpdateView();
	}
}

void UACFTransitionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
	if (Active.IsSet() && Active->Kind == EACFTransitionKind::Level && (Stage == EStage::WaitingForLoad || Stage == EStage::Loading))
	{
		UE_LOG(LogACFTransition, Warning, TEXT("ACF Transition: travel failure (%s)."), *Error);
		NotifyLoadFailed(Active->Handle);
	}
}

void UACFTransitionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
	HandleTravelFailure(World, ETravelFailure::ClientTravelFailure, Error);
}
