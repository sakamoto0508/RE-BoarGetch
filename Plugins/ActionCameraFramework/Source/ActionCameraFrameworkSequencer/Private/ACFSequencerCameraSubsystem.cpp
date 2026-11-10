#include "ACFSequencerCameraSubsystem.h"
#include "ACFSequencerSettings.h"
#include "ACFCameraSubsystem.h"
#include "Engine/Engine.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, ActionCameraFrameworkSequencer)

FACFSequencerCameraOptions::FACFSequencerCameraOptions()
{
	// 復帰ブレンドの切替元は常に「Sequencer の最終姿勢のスナップショット」（Live にできる元カメラが無い）。
	ReturnBlend.Curve = EACFCameraBlendCurve::EaseInOut;
	ReturnBlend.Duration = 0.5f;
	ReturnBlend.Source = EACFCameraBlendSource::Snapshot;
}

namespace ACFSequencer
{
	static const TCHAR* PhaseName(EACFSequencerCameraPhase Phase)
	{
		switch (Phase)
		{
		case EACFSequencerCameraPhase::BlendingIn: return TEXT("BlendingIn");
		case EACFSequencerCameraPhase::InControl: return TEXT("InControl");
		case EACFSequencerCameraPhase::BlendingOut: return TEXT("BlendingOut");
		default: return TEXT("None");
		}
	}

	static FACFCameraPose LastRenderedPose(const APlayerCameraManager& CameraManager)
	{
		// カメラ更新の前に読むので、これは「前のフレームに実際に描画された姿勢」。
		const FMinimalViewInfo& View = CameraManager.GetCameraCacheView();
		FACFCameraPose Pose;
		Pose.Location = View.Location;
		Pose.Rotation = View.Rotation;
		Pose.FieldOfView = View.FOV;
		return Pose;
	}
}

// -------------------------------------------------------------------------------------------------
// ライフサイクル

UACFSequencerCameraSubsystem* UACFSequencerCameraSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UACFSequencerCameraSubsystem>() : nullptr;
}

bool UACFSequencerCameraSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// エディタのプレビューでは Camera Cut はビューポートを直接動かす（PCM を使わない）ので対象外。
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UACFSequencerCameraSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UWorld* World = GetWorld())
	{
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::HandleActorSpawned));
	}
	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &ThisClass::HandleLevelAdded);
	PreEvaluateHandle = UACFCameraSubsystem::OnPreEvaluate.AddUObject(this, &ThisClass::HandlePreEvaluate);
}

void UACFSequencerCameraSubsystem::Deinitialize()
{
	// 抑制と外部 ViewTarget の登録は Source（this）単位で持っているので、ここで必ず返す。ワールドの破棄で this が
	// 消えた場合も、ACF 側が失効した Source を自動で無視する（二重の安全策）。
	for (FPlayerState& State : PlayerStates)
	{
		if (const ULocalPlayer* LocalPlayer = State.LocalPlayer.Get())
		{
			if (UACFCameraSubsystem* Acf = LocalPlayer->GetSubsystem<UACFCameraSubsystem>())
			{
				Acf->ReleaseOutputSuppression(this, 0.f);
				Acf->SetExternalViewTargets(this, TConstArrayView<AActor*>());
			}
		}
	}
	PlayerStates.Reset();
	SequenceActors.Reset();
	SequenceOptions.Reset();

	if (UWorld* World = GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	UACFCameraSubsystem::OnPreEvaluate.Remove(PreEvaluateHandle);
	Super::Deinitialize();
}

void UACFSequencerCameraSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// レベルに置かれた Actor はスポーン通知が来ないので、ここで拾う。
	for (TActorIterator<ALevelSequenceActor> It(&InWorld); It; ++It)
	{
		RegisterSequenceActor(*It);
	}
}

TStatId UACFSequencerCameraSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UACFSequencerCameraSubsystem, STATGROUP_Tickables);
}

void UACFSequencerCameraSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RefreshAll();
}

void UACFSequencerCameraSubsystem::RefreshNow()
{
	RefreshAll();
}

void UACFSequencerCameraSubsystem::HandleActorSpawned(AActor* Actor)
{
	if (ALevelSequenceActor* SequenceActor = Cast<ALevelSequenceActor>(Actor))
	{
		RegisterSequenceActor(SequenceActor);
	}
}

void UACFSequencerCameraSubsystem::HandleLevelAdded(ULevel* Level, UWorld* World)
{
	if (!Level || World != GetWorld())
	{
		return;
	}
	for (AActor* Actor : Level->Actors)
	{
		if (ALevelSequenceActor* SequenceActor = Cast<ALevelSequenceActor>(Actor))
		{
			RegisterSequenceActor(SequenceActor);
		}
	}
}

void UACFSequencerCameraSubsystem::HandlePreEvaluate(UACFCameraSubsystem& Subsystem, float DeltaTime)
{
	// ACF がこのフレームのカメラを評価する直前。Tick の後に（他の Tickable やタイマーから）Sequence が止められていても、
	// 同じフレームのうちに復帰ブレンドを予約できる（1 フレームだけ素の ACF カメラが見えてから戻る、を防ぐ）。
	ULocalPlayer* LocalPlayer = Subsystem.GetLocalPlayer();
	if (LocalPlayer && LocalPlayer->GetWorld() == GetWorld())
	{
		RefreshPlayer(LocalPlayer);
	}
}

void UACFSequencerCameraSubsystem::RegisterSequenceActor(ALevelSequenceActor* SequenceActor)
{
	if (SequenceActor && SequenceActor->GetWorld() == GetWorld())
	{
		SequenceActors.AddUnique(SequenceActor);
	}
}

void UACFSequencerCameraSubsystem::LogStateChange(const FString& Message) const
{
	if (GetDefault<UACFSequencerSettings>()->bLogStateChanges)
	{
		UE_LOG(LogActionCamera, Log, TEXT("[Sequencer] %s"), *Message);
	}
	else
	{
		UE_LOG(LogActionCamera, Verbose, TEXT("[Sequencer] %s"), *Message);
	}
}

// -------------------------------------------------------------------------------------------------
// 設定

void UACFSequencerCameraSubsystem::SetPlayerOptions(APlayerController* PlayerController, const FACFSequencerCameraOptions& Options)
{
	if (ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr)
	{
		FPlayerState& State = FindOrAddState(LocalPlayer);
		State.bHasOptionsOverride = true;
		State.OptionsOverride = Options;
	}
}

void UACFSequencerCameraSubsystem::ClearPlayerOptions(APlayerController* PlayerController)
{
	if (FPlayerState* State = FindState(PlayerController ? PlayerController->GetLocalPlayer() : nullptr))
	{
		State->bHasOptionsOverride = false;
	}
}

void UACFSequencerCameraSubsystem::SetSequenceOptions(ALevelSequenceActor* SequenceActor, const FACFSequencerCameraOptions& Options)
{
	if (SequenceActor)
	{
		RegisterSequenceActor(SequenceActor);
		SequenceOptions.Add(SequenceActor, Options);
	}
}

void UACFSequencerCameraSubsystem::ClearSequenceOptions(ALevelSequenceActor* SequenceActor)
{
	SequenceOptions.Remove(SequenceActor);
}

FACFSequencerCameraOptions UACFSequencerCameraSubsystem::ResolveOptions(const FPlayerState* State, const ALevelSequenceActor* SequenceActor) const
{
	if (SequenceActor)
	{
		if (const FACFSequencerCameraOptions* Found = SequenceOptions.Find(SequenceActor))
		{
			return *Found;
		}
	}
	if (State && State->bHasOptionsOverride)
	{
		return State->OptionsOverride;
	}
	return GetDefault<UACFSequencerSettings>()->DefaultOptions;
}

FACFSequencerCameraOptions UACFSequencerCameraSubsystem::GetEffectiveOptions(const APlayerController* PlayerController, const ALevelSequenceActor* SequenceActor) const
{
	return ResolveOptions(FindState(PlayerController), SequenceActor);
}

// -------------------------------------------------------------------------------------------------
// 状態

UACFSequencerCameraSubsystem::FPlayerState* UACFSequencerCameraSubsystem::FindState(const ULocalPlayer* LocalPlayer)
{
	return LocalPlayer ? PlayerStates.FindByPredicate([LocalPlayer](const FPlayerState& State) { return State.LocalPlayer.Get() == LocalPlayer; }) : nullptr;
}

const UACFSequencerCameraSubsystem::FPlayerState* UACFSequencerCameraSubsystem::FindState(const APlayerController* PlayerController) const
{
	const ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	return LocalPlayer ? PlayerStates.FindByPredicate([LocalPlayer](const FPlayerState& State) { return State.LocalPlayer.Get() == LocalPlayer; }) : nullptr;
}

UACFSequencerCameraSubsystem::FPlayerState& UACFSequencerCameraSubsystem::FindOrAddState(ULocalPlayer* LocalPlayer)
{
	if (FPlayerState* Existing = FindState(LocalPlayer))
	{
		return *Existing;
	}
	FPlayerState& State = PlayerStates.AddDefaulted_GetRef();
	State.LocalPlayer = LocalPlayer;
	return State;
}

FACFSequencerCameraState UACFSequencerCameraSubsystem::GetCameraState(const APlayerController* PlayerController) const
{
	FACFSequencerCameraState Out;
	if (const FPlayerState* State = FindState(PlayerController))
	{
		Out.Phase = State->Phase;
		Out.bSequencerControlsCamera = State->Phase != EACFSequencerCameraPhase::None;
		Out.NumActiveSequences = State->NumActiveSequences;
		Out.SequenceActor = State->ControllingSequence.Get();
		Out.SequencerViewTarget = State->SequencerViewTarget.Get();
		Out.GameplayViewTarget = State->GameplayViewTarget.Get();
		Out.bSuppressingOutput = State->bSuppressing;
		if (const ULocalPlayer* LocalPlayer = State->LocalPlayer.Get())
		{
			if (const UACFCameraSubsystem* Acf = LocalPlayer->GetSubsystem<UACFCameraSubsystem>())
			{
				Out.bOutputBypassed = Acf->IsOutputBypassed();
			}
		}
	}
	return Out;
}

bool UACFSequencerCameraSubsystem::IsSequencerControllingCamera(const APlayerController* PlayerController) const
{
	const FPlayerState* State = FindState(PlayerController);
	return State && State->Phase != EACFSequencerCameraPhase::None;
}

APlayerController* UACFSequencerCameraSubsystem::ResolveSequenceController(const ALevelSequenceActor& SequenceActor) const
{
	// UE::MovieScene::GetPlaybackController（MovieSceneCameraCutGameHandler.cpp）と同じ規則:
	// 再生コンテキスト（= Level Sequence Actor）の Owner が PlayerController ならそれ、無ければ最初のローカルプレイヤー。
	if (APlayerController* Owner = Cast<APlayerController>(SequenceActor.GetOwner()))
	{
		return Owner;
	}
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetFirstLocalPlayerController(World) : nullptr;
}

void UACFSequencerCameraSubsystem::RefreshAll()
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		return;
	}
	SequenceActors.RemoveAllSwap([](const TWeakObjectPtr<ALevelSequenceActor>& Actor) { return !Actor.IsValid(); });
	for (auto It = SequenceOptions.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	PlayerStates.RemoveAll([](const FPlayerState& State) { return !State.LocalPlayer.IsValid(); });
	for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
	{
		if (LocalPlayer)
		{
			RefreshPlayer(LocalPlayer);
		}
	}
}

void UACFSequencerCameraSubsystem::ApplySuppression(FPlayerState& State, UACFCameraSubsystem* Acf, bool bWant, bool bForCameraCut, float BlendTime)
{
	if (bWant == State.bSuppressing)
	{
		State.bSuppressingForCameraCut = bWant && bForCameraCut;
		return;
	}
	if (Acf)
	{
		// Source は常に this。他のシステム（ゲームの Suppress Output など）の抑制とは独立に数えられるので、
		// 解除の順番がどうであっても、最後の 1 つが外れるまで ACF の出力は戻らない。
		if (bWant)
		{
			Acf->SuppressOutput(this, BlendTime);
		}
		else
		{
			Acf->ReleaseOutputSuppression(this, BlendTime);
		}
	}
	State.bSuppressing = bWant;
	State.bSuppressingForCameraCut = bWant && bForCameraCut;
	if (bWant && !bForCameraCut)
	{
		State.WholeSequenceBlendTime = BlendTime;
	}
	LogStateChange(FString::Printf(TEXT("%s: output suppression %s (%s, blend %.2fs)"), *GetNameSafe(State.Controller.Get()),
		bWant ? TEXT("ON") : TEXT("OFF"), bForCameraCut ? TEXT("camera cut") : TEXT("whole sequence"), BlendTime));
}

void UACFSequencerCameraSubsystem::ApplyControlRotationPolicy(const FPlayerState& State, UACFCameraSubsystem* Acf, const FACFSequencerCameraOptions& Options) const
{
	APlayerController* Controller = State.Controller.Get();
	APlayerCameraManager* CameraManager = State.CameraManager.Get();
	if (Options.ControlRotationPolicy != EACFSequencerControlRotationPolicy::MatchSequencerCamera || !Controller || !CameraManager)
	{
		return;
	}
	// ゲームプレイカメラが再び見え始める瞬間の Sequencer の向きに合わせる。PCM の制限（Pitch）は守る。
	FRotator Rotation = CameraManager->GetCameraCacheView().Rotation;
	Rotation.Roll = 0.f;
	CameraManager->LimitViewPitch(Rotation, CameraManager->ViewPitchMin, CameraManager->ViewPitchMax);
	Controller->SetControlRotation(Rotation);
	if (Acf)
	{
		Acf->SetAllOrbitRotations(Rotation, /*bInstant*/ true); // Input Source = Camera Input の Orbit も同じ向きから再開
	}
	LogStateChange(FString::Printf(TEXT("%s: control rotation matched to the sequencer camera (%s)"), *GetNameSafe(Controller), *Rotation.ToCompactString()));
}

void UACFSequencerCameraSubsystem::RefreshPlayer(ULocalPlayer* LocalPlayer)
{
	UWorld* World = GetWorld();
	if (!World || !LocalPlayer)
	{
		return;
	}
	FPlayerState& State = FindOrAddState(LocalPlayer);
	UACFCameraSubsystem* Acf = LocalPlayer->GetSubsystem<UACFCameraSubsystem>();
	APlayerController* Controller = LocalPlayer->GetPlayerController(World);
	APlayerCameraManager* CameraManager = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
	const bool bAutomatic = GetDefault<UACFSequencerSettings>()->bAutomaticHandling;

	// PlayerController / PCM が作り直された: 古い PCM についての判定は捨て、新しい PCM の状態から判定し直す。
	// （Sequencer は次の評価で新しい PCM に Camera Cut を掛け直すので、そこで改めて Started になる。）
	if (State.Controller.Get() != Controller || State.CameraManager.Get() != CameraManager || !bAutomatic)
	{
		if (State.Phase != EACFSequencerCameraPhase::None)
		{
			LogStateChange(FString::Printf(TEXT("%s: camera manager changed / handling disabled while %s -> reset"), *GetNameSafe(Controller), ACFSequencer::PhaseName(State.Phase)));
			ALevelSequenceActor* Sequence = State.ControllingSequence.Get();
			State.Phase = EACFSequencerCameraPhase::None;
			State.ControllingSequence.Reset();
			State.SequencerViewTarget.Reset();
			OnSequencerCameraEnded.Broadcast(Controller, Sequence, EACFSequencerReturnKind::Cut);
		}
		ApplySuppression(State, Acf, false, false, 0.f);
		if (Acf)
		{
			Acf->SetExternalViewTargets(this, TConstArrayView<AActor*>());
		}
		State.Controller = Controller;
		State.CameraManager = CameraManager;
		State.GameplayViewTarget.Reset();
		State.NumActiveSequences = 0;
	}
	if (!CameraManager || !bAutomatic)
	{
		return;
	}

	// 1. このプレイヤーを対象に再生中の Level Sequence。
	int32 NumActive = 0;
	ALevelSequenceActor* FirstActive = nullptr;
	bool bWholeSequenceSuppress = false;
	float WholeSequenceBlendTime = 0.f;
	for (const TWeakObjectPtr<ALevelSequenceActor>& WeakActor : SequenceActors)
	{
		ALevelSequenceActor* SequenceActor = WeakActor.Get();
		const ULevelSequencePlayer* Player = SequenceActor ? SequenceActor->GetSequencePlayer() : nullptr;
		// 一時停止（Pause、Pause At End）は「まだ握っている」。Stopped だけが非アクティブ。
		if (!Player || !(Player->IsPlaying() || Player->IsPaused()) || ResolveSequenceController(*SequenceActor) != Controller)
		{
			continue;
		}
		++NumActive;
		FirstActive = FirstActive ? FirstActive : SequenceActor;
		const FACFSequencerCameraOptions SequenceOpts = ResolveOptions(&State, SequenceActor);
		if (SequenceOpts.bEnabled && SequenceOpts.bSuppressOutputWhilePlaying)
		{
			bWholeSequenceSuppress = true;
			WholeSequenceBlendTime = SequenceOpts.OutputBlendTime;
		}
	}
	State.NumActiveSequences = NumActive;

	// 2. PCM の ViewTarget から「Sequencer のカメラか」を判定する。
	AActor* Current = CameraManager->ViewTarget.Target;
	AActor* Pending = CameraManager->PendingViewTarget.Target;
	const AActor* Pawn = Controller->GetPawn();
	auto IsGameplayTarget = [&](const AActor* Target)
	{
		return !Target || Target == State.GameplayViewTarget.Get() || Target == Pawn || Target == Controller;
	};
	// bClientSimulatingViewTarget は Camera Cut が ViewTarget を握っている間だけ立つ（Restore で下りる）。
	// 再生が終わっても ViewTarget が残されている場合（Keep State）は、同じカメラが映っている限り「握っている」とみなす。
	const AActor* KnownSequencerTarget = State.SequencerViewTarget.Get();
	const bool bStillHolding = State.Phase != EACFSequencerCameraPhase::None && KnownSequencerTarget
		&& (Current == KnownSequencerTarget || Pending == KnownSequencerTarget);
	const bool bCandidate = CameraManager->bClientSimulatingViewTarget && (NumActive > 0 || bStillHolding);
	const bool bCurrentIsSequencer = bCandidate && !IsGameplayTarget(Current);
	const bool bPendingIsSequencer = bCandidate && Pending && !IsGameplayTarget(Pending);

	EACFSequencerCameraPhase NewPhase = EACFSequencerCameraPhase::None;
	if (bCurrentIsSequencer || bPendingIsSequencer)
	{
		if (!Pending)
		{
			NewPhase = EACFSequencerCameraPhase::InControl;
		}
		else if (bPendingIsSequencer && !bCurrentIsSequencer)
		{
			NewPhase = EACFSequencerCameraPhase::BlendingIn;
		}
		else if (bCurrentIsSequencer && !bPendingIsSequencer)
		{
			NewPhase = EACFSequencerCameraPhase::BlendingOut;
		}
		else
		{
			NewPhase = EACFSequencerCameraPhase::InControl; // Sequencer のカメラ同士のブレンド
		}
	}
	const EACFSequencerCameraPhase OldPhase = State.Phase;
	AActor* SequencerTarget = bPendingIsSequencer ? Pending : (bCurrentIsSequencer ? Current : nullptr);

	// 3. 開始。
	if (OldPhase == EACFSequencerCameraPhase::None && NewPhase != EACFSequencerCameraPhase::None)
	{
		State.ControllingSequence = FirstActive;
	}
	if (NewPhase != EACFSequencerCameraPhase::None && SequencerTarget)
	{
		State.SequencerViewTarget = SequencerTarget;
	}
	ALevelSequenceActor* ControllingSequence = State.ControllingSequence.Get();
	const FACFSequencerCameraOptions Options = ResolveOptions(&State, ControllingSequence);

	// 4. ACF の出力制御。
	//    Camera Cut: Sequencer のカメラ（ViewTarget）を「外部 ViewTarget」として ACF に登録する。ACF はその ViewTarget にだけ
	//    出力しない。PCM の ViewTarget Blend 中もゲームプレイ側の ViewTarget には 100% で乗り続けるので、ブレンドは PCM の
	//    1 本だけになる（ACF の Weight はフェードさせない）。
	if (Acf)
	{
		TArray<AActor*, TInlineAllocator<2>> ExternalTargets;
		if (Options.bEnabled)
		{
			if (bCurrentIsSequencer)
			{
				ExternalTargets.Add(Current);
			}
			if (bPendingIsSequencer)
			{
				ExternalTargets.AddUnique(Pending);
			}
		}
		Acf->SetExternalViewTargets(this, ExternalTargets);
	}
	//    Camera Cut の無い Sequence で「再生中は抑制」が指定されている場合だけ、通常の出力抑制（Weight のフェード）を使う。
	if (bWholeSequenceSuppress)
	{
		ApplySuppression(State, Acf, true, false, WholeSequenceBlendTime);
	}
	else if (State.bSuppressing)
	{
		ApplySuppression(State, Acf, false, false, State.WholeSequenceBlendTime);
	}

	// 5. 状態遷移と復帰処理。
	if (OldPhase != NewPhase)
	{
		LogStateChange(FString::Printf(TEXT("%s: %s -> %s (view target %s, pending %s, sequence %s)"), *GetNameSafe(Controller), ACFSequencer::PhaseName(OldPhase),
			ACFSequencer::PhaseName(NewPhase), *GetNameSafe(Current), *GetNameSafe(Pending), *GetNameSafe(ControllingSequence)));
	}
	State.Phase = NewPhase;
	if (OldPhase == EACFSequencerCameraPhase::None && NewPhase != EACFSequencerCameraPhase::None)
	{
		OnSequencerCameraStarted.Broadcast(Controller, ControllingSequence);
	}
	else if (NewPhase == EACFSequencerCameraPhase::BlendingOut && OldPhase != EACFSequencerCameraPhase::BlendingOut)
	{
		// Sequencer 自身がブレンドで戻り始めた。ブレンドは PCM に任せ、ACF は何も重ねない。
		// ControlRotation だけは、ゲームプレイカメラが見え始めるこの時点で合わせる。
		if (Options.bEnabled)
		{
			ApplyControlRotationPolicy(State, Acf, Options);
		}
	}
	else if (OldPhase != EACFSequencerCameraPhase::None && NewPhase == EACFSequencerCameraPhase::None)
	{
		EACFSequencerReturnKind Kind = OldPhase == EACFSequencerCameraPhase::BlendingOut ? EACFSequencerReturnKind::SequencerBlend : EACFSequencerReturnKind::Cut;
		if (Kind == EACFSequencerReturnKind::Cut && Options.bEnabled)
		{
			// Sequencer はカットで戻った（再生終了の Restore、Stop、途中停止、Sequence Actor の破棄など）。
			ApplyControlRotationPolicy(State, Acf, Options);
			// PCM がブレンド中でなければ、ACF が「前フレームに映っていた姿勢 → ACF カメラ」を補間する。
			if (Acf && !Pending && Acf->BlendFromPose(ACFSequencer::LastRenderedPose(*CameraManager), Options.ReturnBlend))
			{
				Kind = EACFSequencerReturnKind::AcfReturnBlend;
			}
		}
		State.ControllingSequence.Reset();
		State.SequencerViewTarget.Reset();
		OnSequencerCameraEnded.Broadcast(Controller, ControllingSequence, Kind);
	}

	// 6. Sequencer が握っていない間の ViewTarget を「戻り先」として覚える。
	if (State.Phase == EACFSequencerCameraPhase::None)
	{
		State.GameplayViewTarget = Pending ? Pending : Current;
	}
}
