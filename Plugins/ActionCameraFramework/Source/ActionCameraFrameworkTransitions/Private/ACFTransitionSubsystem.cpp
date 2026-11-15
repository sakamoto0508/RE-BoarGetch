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
	// No events at shutdown (listeners may be gone); just give the input back and drop the overlay.
	ApplyInputLock(false);
	Active.Reset();
	Queue.Reset();
	PendingEvents.Reset();
	Stage = EStage::None;
	Coverage = 0.f;
	bStill = false;
	DetachOverlay();
	Super::Deinitialize();
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
	return Submit(MoveTemp(Request));
}

FACFTransitionHandle UACFTransitionSubsystem::PlayLevelTransitionPreset(const UACFTransitionPreset* Preset, const FACFTransitionOptions& Options, FName LevelName, const FString& TravelOptions)
{
	const FACFTransitionSettings Close = Preset ? Preset->Close : ACFTransition::MakeDefaultSettings(EACFTransitionEffect::BlackFade, EACFTransitionPhase::Close);
	const FACFTransitionSettings Open = Preset ? Preset->Open : ACFTransition::MakeDefaultSettings(EACFTransitionEffect::BlackFade, EACFTransitionPhase::Open);
	return PlayLevelTransition(Close, Open, Options, LevelName, TravelOptions);
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
			return Handle;
		case EACFTransitionConcurrency::Queue:
			Queue.Add(MoveTemp(Request));
			return Handle;
		default:
			if (Active.IsSet())
			{
				EndActive(EACFTransitionEvent::Interrupted);
			}
			break;
		}
	}
	Start(MoveTemp(Request));
	return Handle;
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

	if (USoundBase* Sound = DrawSettings.Sound)
	{
		if (UWorld* World = GetGameWorld())
		{
			UGameplayStatics::SpawnSound2D(World, Sound, 1.f, 1.f, 0.f, nullptr, /*bPersistAcrossLevelTransition*/ true, /*bAutoDestroy*/ true);
		}
	}

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
		if (bClearScreen && !Active.IsSet())
		{
			Coverage = 0.f;
			DetachOverlay();
		}
		StartNextQueued();
		return true;
	}
	const int32 Index = Queue.IndexOfByPredicate([Handle](const FRequest& R) { return R.Handle == Handle; });
	if (Index != INDEX_NONE)
	{
		FRequest Removed = MoveTemp(Queue[Index]);
		Queue.RemoveAt(Index);
		Emit(Removed, EACFTransitionEvent::Cancelled);
		return true;
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
		PhaseTime = FMath::Max(PhaseTime, 1.0e6f);
		return true;
	}
	return false;
}

void UACFTransitionSubsystem::ClearScreen()
{
	TArray<FRequest> Dropped = MoveTemp(Queue);
	Queue.Reset();
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
}

bool UACFTransitionSubsystem::NotifyLoadStarted(FACFTransitionHandle Handle)
{
	if (!Active.IsSet() || Active->Handle != Handle || Active->Kind != EACFTransitionKind::Level || Stage != EStage::WaitingForLoad)
	{
		return false;
	}
	SetStage(EStage::Loading);
	Emit(*Active, EACFTransitionEvent::LoadStarted);
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
	if (Active.IsSet() || !Queue.IsEmpty())
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
	return Active.IsSet() || !Queue.IsEmpty();
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
	for (const FRequest& R : Queue)
	{
		if (R.Handle == Handle)
		{
			Info.bQueued = true;
			Info.Kind = R.Kind;
			Info.Effect = R.Kind == EACFTransitionKind::Open ? R.Open.Effect : R.Close.Effect;
			Info.Phase = R.Kind == EACFTransitionKind::Open ? EACFTransitionPhase::Open : EACFTransitionPhase::Close;
			return Info;
		}
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
	return bInitialized && (Active.IsSet() || !Queue.IsEmpty() || !PendingEvents.IsEmpty());
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
			OnTransitionEventNative.Broadcast(Event.Key, Event.Value);
			OnTransitionEvent.Broadcast(Event.Key, Event.Value);
		}
	}
	if (!Active.IsSet())
	{
		StartNextQueued();
		if (!Active.IsSet())
		{
			return;
		}
	}
	const UACFTransitionProjectSettings* Settings = GetDefault<UACFTransitionProjectSettings>();
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
				if (!Done.Options.bHoldWhenCovered)
				{
					Coverage = 0.f;
					DetachOverlay();
				}
				Emit(Done, EACFTransitionEvent::Covered);
				Emit(Done, EACFTransitionEvent::Finished);
				if (!Active.IsSet())
				{
					StartNextQueued();
				}
				return;
			}
			SetStage(Active->Options.bManualLoad || Active->LevelName.IsNone() ? EStage::WaitingForLoad : EStage::WaitingForLoad);
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
			if (!StillMine())
			{
				return;
			}
			if (!Active->LevelName.IsNone() && !bLevelOpened)
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
			if (!Active.IsSet() && Coverage <= 0.f)
			{
				DetachOverlay();
			}
			StartNextQueued();
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
	DrawSettings = Active->Kind == EACFTransitionKind::Open ? Active->Open : (Active->Kind == EACFTransitionKind::Level ? Active->Open : Active->Close);
	DrawPhase = EACFTransitionPhase::Open;
	if (!ACFTransition::GetEffectInfo(DrawSettings.Effect).bOpenUsesSnapshot)
	{
		ReleaseCapture();
	}
	PhaseTime = 0.f;
	PhaseStartAlpha = FMath::Clamp(1.f - Coverage, 0.f, 1.f);
	SetStage(EStage::Opening);
	if (USoundBase* Sound = DrawSettings.Sound)
	{
		if (UWorld* World = GetGameWorld())
		{
			UGameplayStatics::SpawnSound2D(World, Sound, 1.f, 1.f, 0.f, nullptr, true, true);
		}
	}
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
	Emit(Done, Terminal);
}

void UACFTransitionSubsystem::Emit(FRequest& Request, EACFTransitionEvent Event)
{
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
	const FACFTransitionHandle Handle = Request.Handle;
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
	if (State == EState::Ready)
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
	// Failed (HDR / non-BGRA back buffer, window changed) or not presented in time: the synchronous read back, and
	// without it the effect's procedural look. The transition always goes on.
	UE_LOG(LogACFTransition, Log, TEXT("Screen capture on the GPU %s after %d frames; using the read back."), State == EState::Failed ? TEXT("failed") : TEXT("not ready"), LastCaptureWaitFrames);
	if (UTexture* Unused = PendingCapture->GetTexture())
	{
		Unused->MarkAsGarbage();
	}
	PendingCapture.Reset();
	bLastCaptureFellBack = true;
	UGameInstance* GI = GetGameInstance();
	Capture = FACFTransitionView::CaptureViewport(GI ? GI->GetGameViewportClient() : nullptr, Settings->MaxCaptureWidth);
	return true;
}

void UACFTransitionSubsystem::ReleaseCapture()
{
	if (PendingCapture.IsValid())
	{
		PendingCapture->Cancel();
		if (UTexture* Unused = PendingCapture->GetTexture())
		{
			Unused->MarkAsGarbage();
		}
		PendingCapture.Reset();
	}
	if (Capture)
	{
		Capture->MarkAsGarbage();
		Capture = nullptr;
	}
}

void UACFTransitionSubsystem::ApplyInputLock(bool bLock)
{
	if (bLock)
	{
		UWorld* World = GetGameWorld();
		if (!World)
		{
			return;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController() && !LockedControllers.Contains(PC))
			{
				PC->SetIgnoreMoveInput(true);
				PC->SetIgnoreLookInput(true);
				LockedControllers.Add(PC);
			}
		}
		bInputLocked = true;
	}
	else
	{
		for (const TWeakObjectPtr<APlayerController>& Weak : LockedControllers)
		{
			if (APlayerController* PC = Weak.Get())
			{
				PC->SetIgnoreMoveInput(false);
				PC->SetIgnoreLookInput(false);
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
