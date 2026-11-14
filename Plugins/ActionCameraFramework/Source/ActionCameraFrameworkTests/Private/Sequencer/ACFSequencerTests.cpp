#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFSequencerBlueprintLibrary.h"
#include "ACFSequencerCameraSubsystem.h"
#include "ACFSequencerTestListener.h"
#include "ACFSequencerTestProbeModifier.h"
#include "ACFSequencerTestCameraManager.h"
#include "ACFCameraHealth.h"
#include "Effects/ACFCameraBuiltinEffects.h"
#include "Effects/ACFCameraEffect.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraDirector.h"
#include "ACFCameraOutputModifier.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSequence.h"
#include "ACFCameraSubsystem.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_LockOn.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Generators/MovieSceneEasingCurves.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Misc/App.h"
#include "MovieScene.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"
#include "ACFSequencerSettings.h"
#include "ACFCameraTestEnvironment.h"
#include "ACFCameraTuningLibrary.h"
#include "Nodes/ACFCameraNode_Combat.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"

/**
 * Phase 5A: 実際の Level Sequence（Camera Cut Track）・PlayerCameraManager との結合テスト。ClientContext のみ:
 *   UnrealEditor-Cmd <project> <map> -game -NullRHI -ExecCmds="Automation RunTests ActionCameraFramework.Sequencer"
 * Level Sequence はテスト内で作る（Transient）。時間は固定ステップ（1/60 秒）にして、1 フレームあたりの移動量を判定できるようにする。
 */
namespace ACFSeqTest
{
	constexpr double FixedDt = 1.0 / 60.0;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	// マップのジオメトリから十分離れた場所（Collision Node は使わない）。
	static const FVector Base(-60000.f, 60000.f, 20000.f);
	static const FVector GameplayLoc = Base;
	static const FRotator GameplayRot(0.f, 0.f, 0.f);
	constexpr float GameplayFov = 80.f;
	static const FVector CineLoc = Base + FVector(1000.f, 500.f, 300.f);
	static const FRotator CineRot(-10.f, 90.f, 0.f);
	constexpr float CineFov = 60.f;
	static const FVector CineLocB = Base + FVector(-800.f, 900.f, 200.f);
	static const FRotator CineRotB(-5.f, -45.f, 0.f);

	static APlayerController* FindLocalPlayerController()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				APlayerController* PC = World->GetFirstPlayerController();
				if (PC && PC->GetLocalPlayer() && PC->PlayerCameraManager)
				{
					return PC;
				}
			}
		}
		return nullptr;
	}

	/** 1 つのシナリオの共有状態。ステップは連続するエンジンフレームで実行される。 */
	struct FScenario
	{
		FAutomationTestBase* Test = nullptr;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<UACFCameraSubsystem> Acf;
		TWeakObjectPtr<UACFSequencerCameraSubsystem> Sequencer;
		TStrongObjectPtr<UACFSequencerTestListener> Listener;
		TArray<TStrongObjectPtr<UObject>> Keep;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TWeakObjectPtr<ALevelSequenceActor> SequenceA;
		TWeakObjectPtr<ALevelSequenceActor> SequenceB;
		TWeakObjectPtr<AActor> ActorA;
		TWeakObjectPtr<AActor> ActorB;
		TWeakObjectPtr<AActor> ActorC;
		FACFCameraRequestHandle Handle;
		FACFCameraRequestHandle HandleB;
		FACFCameraSequenceHandle Finisher;
		TWeakObjectPtr<ULocalPlayer> SecondPlayer;
		TWeakObjectPtr<UACFSequencerTestProbeModifier> Probe;
		TWeakObjectPtr<AActor> GameplayViewTarget;
		TWeakObjectPtr<APlayerCameraManager> OldCameraManager;
		TSubclassOf<APlayerCameraManager> SavedCameraManagerClass; // B-2: テストが PCM のクラスを差し替えた場合の元のクラス
		bool bSwappedCameraManager = false;
		FVector SavedLocation = FVector::ZeroVector;
		double Value = 0.0;
		int32 Counter = 0;
		uint64 MarkFrame = 0;
		bool bRetry = false;

		// 毎フレームのサンプル（最初のローカルプレイヤー）。ResetSamples で区間を切る。
		bool bHasLastCamera = false;
		FVector LastCameraLocation = FVector::ZeroVector;
		double MaxFrameJump = 0.0;
		float MinAcfWeight = 1.f;
		bool bAcfBlended = false;
		int32 MaxSuppressions = 0;
		int32 MaxEvaluationLag = 0;
		int32 GameplayFrames = 0; // ゲームプレイカメラ（GameplayLoc）が映ったフレーム数
		bool bLogJumps = false; // 1 フレーム 15cm を超える移動を AddInfo に出す（調査用）
		bool bHasLastStep = false;
		double LastStep = 0.0;
		double MaxStepChange = 0.0; // 1 フレームの移動量の変化の最大値（滑らかさ。カットや段差で大きくなる）
		int32 BigJumps = 0; // 1 フレームで 200cm を超えた回数（カット）
		TArray<EACFSequencerCameraPhase> Phases;
		// Phase 7B-1 (assistance): the moved follow target and the debugger-vs-camera check of player 1.
		TWeakObjectPtr<AActor> Mover;
		FACFCameraRequestHandle AssistHandle;
		bool bAssistHasLast = false;
		bool bAssistInputThisFrame = false;
		float AssistLastYaw = 0.f;
		uint64 AssistLastFrame = 0;
		int32 AssistChecked = 0;
		int32 AssistMismatches = 0;

		APlayerCameraManager* PCM() const { return Controller.IsValid() ? Controller->PlayerCameraManager.Get() : nullptr; }
		UWorld* World() const { return Controller.IsValid() ? Controller->GetWorld() : nullptr; }
		EACFSequencerCameraPhase Phase() const { return Sequencer->GetCameraState(Controller.Get()).Phase; }
		FVector Camera() const { return PCM()->GetCameraLocation(); }

		void ResetSamples()
		{
			MaxFrameJump = 0.0;
			MinAcfWeight = 1.f;
			bAcfBlended = false;
			MaxSuppressions = 0;
			MaxEvaluationLag = 0;
			GameplayFrames = 0;
			bHasLastStep = false;
			MaxStepChange = 0.0;
			BigJumps = 0;
			Phases.Reset();
			Phases.Add(Phase());
		}

		void Sample()
		{
			const APlayerCameraManager* M = PCM();
			if (!M || !Acf.IsValid() || !Sequencer.IsValid())
			{
				return;
			}
			const FVector Location = M->GetCameraLocation();
			if (bHasLastCamera)
			{
				MaxFrameJump = FMath::Max(MaxFrameJump, FVector::Dist(Location, LastCameraLocation));
				const double Step = FVector::Dist(Location, LastCameraLocation);
				BigJumps += Step > 200.0 ? 1 : 0;
				if (bHasLastStep)
				{
					MaxStepChange = FMath::Max(MaxStepChange, FMath::Abs(Step - LastStep));
				}
				LastStep = Step;
				bHasLastStep = true;
				if (bLogJumps && FVector::Dist(Location, LastCameraLocation) > 15.0)
				{
					Test->AddInfo(FString::Printf(TEXT("frame %llu: %.1f cm, phase %s, pending %s, ACF blending %d"), GFrameCounter, FVector::Dist(Location, LastCameraLocation),
						*UEnum::GetValueAsString(Phase()), *GetNameSafe(PCM() ? PCM()->PendingViewTarget.Target.Get() : nullptr), Acf->GetLastEvaluationResult().bIsBlending ? 1 : 0));
				}
			}
			LastCameraLocation = Location;
			bHasLastCamera = true;
			GameplayFrames += Location.Equals(GameplayLoc, 1.0) ? 1 : 0;
			const FACFCameraEvaluationResult Result = Acf->GetLastEvaluationResult();
			if (Result.NumRequests > 0)
			{
				MinAcfWeight = FMath::Min(MinAcfWeight, Result.Weight);
				bAcfBlended |= Result.bIsBlending;
				MaxEvaluationLag = FMath::Max(MaxEvaluationLag, static_cast<int32>(GFrameCounter - Acf->GetDirector()->GetLastEvaluatedFrame()));
			}
			MaxSuppressions = FMath::Max(MaxSuppressions, Acf->GetNumOutputSuppressions());
			const EACFSequencerCameraPhase Now = Phase();
			if (Phases.IsEmpty() || Phases.Last() != Now)
			{
				Phases.Add(Now);
			}
		}

		bool CameraNear(const FVector& Location, double Tolerance, const TCHAR* What) const
		{
			const double Distance = FVector::Dist(Camera(), Location);
			Test->TestTrue(FString::Printf(TEXT("%s: camera %s within %.1f of %s (distance %.2f)"), What, *Camera().ToCompactString(), Tolerance, *Location.ToCompactString(), Distance), Distance <= Tolerance);
			return Distance <= Tolerance;
		}

		FString PhaseHistory() const
		{
			FString Out;
			for (const EACFSequencerCameraPhase P : Phases)
			{
				Out += UEnum::GetValueAsString(P).RightChop(26) + TEXT(" ");
			}
			return Out;
		}
	};

	using FStep = TFunction<bool(FScenario&)>; // false で中断

	class FCommand : public IAutomationLatentCommand
	{
	public:
		FCommand(FAutomationTestBase* InTest, TArray<FStep> InSteps) : Steps(MoveTemp(InSteps))
		{
			State->Test = InTest;
		}

		virtual bool Update() override
		{
			if (bCleaningUp)
			{
				return DrainAndEnd();
			}
			if (!bStarted)
			{
				APlayerController* PC = FindLocalPlayerController();
				if (!PC)
				{
					if (GetCurrentRunTime() > 30.0)
					{
						State->Test->AddError(TEXT("No local PlayerController with a PlayerCameraManager within 30s."));
						return true;
					}
					return false;
				}
				State->Controller = PC;
				State->Acf = PC->GetLocalPlayer()->GetSubsystem<UACFCameraSubsystem>();
				State->Sequencer = UACFSequencerBlueprintLibrary::GetSequencerCameraSubsystem(PC);
				if (!State->Acf.IsValid() || !State->Sequencer.IsValid())
				{
					State->Test->AddError(TEXT("ACF camera subsystem or sequencer subsystem missing."));
					return true;
				}
				State->Listener.Reset(NewObject<UACFSequencerTestListener>());
				State->Sequencer->OnSequencerCameraStarted.AddDynamic(State->Listener.Get(), &UACFSequencerTestListener::HandleStarted);
				State->Sequencer->OnSequencerCameraEnded.AddDynamic(State->Listener.Get(), &UACFSequencerTestListener::HandleEnded);
				// B-9: process-wide state is restored at the end (also on abort); diagnostics count from zero.
				Guard.Capture(PC->GetWorld(), { GetMutableDefault<UACFSequencerSettings>() });
				ACFTestEnvironment::BeginScenario(*State->Acf);
				bSavedFixedStep = FApp::UseFixedTimeStep();
				SavedFixedDt = FApp::GetFixedDeltaTime();
				FApp::SetFixedDeltaTime(FixedDt);
				FApp::SetUseFixedTimeStep(true);
				bStarted = true;
				LastFrame = GFrameCounter;
				return false; // 固定ステップは次のフレームから
			}
			if (GFrameCounter == LastFrame)
			{
				return false;
			}
			LastFrame = GFrameCounter;
			if (!State->PCM() || !State->Acf.IsValid() || !State->Sequencer.IsValid())
			{
				State->Test->AddError(TEXT("Player, camera manager or subsystem lost during the scenario."));
				return Finish();
			}
			State->Sample();
			State->bRetry = false;
			if (!Steps[Index](*State))
			{
				return Finish();
			}
			if (State->bRetry)
			{
				if (++RetryFrames > 1800)
				{
					State->Test->AddError(FString::Printf(TEXT("Scenario step %d timed out (phase history: %s)."), Index, *State->PhaseHistory()));
					return Finish();
				}
				return false;
			}
			RetryFrames = 0;
			if (++Index >= Steps.Num())
			{
				return Finish();
			}
			return false;
		}

	private:
		/** B-9: cleanup, then let notifications queued by the cleanup (the listener is already unbound) drain before the next test. */
		bool Finish()
		{
			Cleanup();
			bCleaningUp = true;
			return DrainAndEnd();
		}

		bool DrainAndEnd()
		{
			const int32 Pending = State->Sequencer.IsValid() ? State->Sequencer->GetNumPendingEvents() : 0;
			if (Pending > 0 && ++DrainFrames <= 10)
			{
				return false;
			}
			if (Pending > 0)
			{
				State->Test->AddError(FString::Printf(TEXT("Test environment: %d sequencer notification(s) still pending 10 frames after the scenario."), Pending));
			}
			else if (DrainFrames > 0)
			{
				State->Test->AddInfo(FString::Printf(TEXT("Cleanup: pending sequencer notifications delivered (listener unbound) in %d frame(s)."), DrainFrames));
			}
			ACFTestEnvironment::EndScenario(*State->Test, State->Acf.Get(), State->PCM(), Guard);
			return true;
		}

		void Cleanup()
		{
			FScenario& S = *State;
			if (S.bSwappedCameraManager && S.Controller.IsValid())
			{
				// B-2: 差し替えた PCM を元のクラスで作り直す（後続のテストは標準 PCM で動く）。
				APlayerCameraManager* Swapped = S.PCM();
				S.Controller->PlayerCameraManagerClass = S.SavedCameraManagerClass;
				S.Controller->SpawnPlayerCameraManager();
				if (Swapped && S.PCM() != Swapped)
				{
					Swapped->Destroy();
				}
				S.bSwappedCameraManager = false;
			}
			if (APlayerCameraManager* M = S.PCM())
			{
				if (S.Probe.IsValid())
				{
					M->RemoveCameraModifier(S.Probe.Get());
				}
				if (S.GameplayViewTarget.IsValid() && M->GetViewTarget() != S.GameplayViewTarget.Get())
				{
					S.Controller->SetViewTarget(S.GameplayViewTarget.Get()); // テストが ViewTarget を動かした場合は戻す
				}
			}
			for (TWeakObjectPtr<ALevelSequenceActor> Sequence : { S.SequenceA, S.SequenceB })
			{
				if (ALevelSequenceActor* Actor = Sequence.Get())
				{
					if (ULevelSequencePlayer* Player = Actor->GetSequencePlayer())
					{
						Player->Stop();
					}
					Actor->Destroy();
				}
			}
			if (S.Sequencer.IsValid())
			{
				if (S.Listener.IsValid())
				{
					S.Sequencer->OnSequencerCameraStarted.RemoveAll(S.Listener.Get());
					S.Sequencer->OnSequencerCameraEnded.RemoveAll(S.Listener.Get());
				}
				S.Sequencer->ClearPlayerOptions(S.Controller.Get());
				S.Sequencer->RefreshNow();
			}
			if (ULocalPlayer* Second = S.SecondPlayer.Get())
			{
				if (UACFCameraSubsystem* SecondAcf = Second->GetSubsystem<UACFCameraSubsystem>())
				{
					SecondAcf->ReleaseAllCameras(true);
				}
				if (UGameInstance* GameInstance = S.World() ? S.World()->GetGameInstance() : nullptr)
				{
					ACFTestEnvironment::RemoveLocalPlayer(Second); // also its pawn (Phase 7A-2)
				}
			}
			if (S.Acf.IsValid())
			{
				S.Test->TestEqual(TEXT("Cleanup: no output suppression left behind by the sequencer bridge"), S.Acf->GetNumOutputSuppressions(), 0);
				S.Acf->GetDirector()->StopAllSequences(true);
				S.Acf->ReleaseAllCameras(true);
				S.Acf->StopAllCameraEffects(true); // B-9: effects used to survive into the next test
			}
			S.Spawned.Add(S.ActorA);
			S.Spawned.Add(S.ActorB);
			S.Spawned.Add(S.ActorC);
			for (TWeakObjectPtr<AActor> Actor : S.Spawned)
			{
				if (Actor.IsValid())
				{
					Actor->Destroy();
				}
			}
			FApp::SetFixedDeltaTime(SavedFixedDt);
			FApp::SetUseFixedTimeStep(bSavedFixedStep);
		}

		TSharedRef<FScenario> State = MakeShared<FScenario>();
		TArray<FStep> Steps;
		uint64 LastFrame = 0;
		int32 Index = 0;
		int32 RetryFrames = 0;
		double SavedFixedDt = 0.0;
		bool bSavedFixedStep = false;
		bool bStarted = false;
		bool bCleaningUp = false;
		int32 DrainFrames = 0;
		ACFTestEnvironment::FGlobalStateGuard Guard;
	};

	static void WaitFrames(TArray<FStep>& Steps, int32 Frames)
	{
		Steps.Add([](FScenario& S) { S.MarkFrame = GFrameCounter; return true; });
		Steps.Add([Frames](FScenario& S) { S.bRetry = GFrameCounter - S.MarkFrame < static_cast<uint64>(Frames); return true; });
	}

	static UACFCameraPreset* MakeFixedPreset(FScenario& S, const FVector& Location, const FRotator& Rotation, float FOV)
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_FixedPose* Node = NewObject<UACFCameraNode_FixedPose>(Preset);
		Node->Location = Location;
		Node->Rotation = Rotation;
		Node->FieldOfView = FOV;
		Preset->Nodes.Add(Node);
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		S.Keep.Emplace(Preset);
		return Preset;
	}

	static FACFCameraRequestHandle RequestGameplay(FScenario& S, int32 Priority = 0)
	{
		FACFCameraRequestParams Params;
		Params.Priority = Priority;
		return S.Acf->RequestCamera(MakeFixedPreset(S, GameplayLoc, GameplayRot, GameplayFov), Params);
	}

	static AActor* SpawnPoint(FScenario& S, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = S.World()->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		S.Spawned.Add(Actor);
		return Actor;
	}

	/** Camera Actor（Sequencer の Camera Cut で最も一般的なカメラ）。 */
	static ACameraActor* SpawnCineCamera(FScenario& S, const FVector& Location, const FRotator& Rotation, float FOV = CineFov)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Camera = S.World()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(Rotation, Location), Params);
		Camera->GetCameraComponent()->SetFieldOfView(FOV);
		Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
		S.Spawned.Add(Camera);
		return Camera;
	}

	/** Camera Component を持つ普通の Actor（Camera Actor と同じく外部 ViewTarget として扱われることを確認する）。 */
	static AActor* SpawnComponentCamera(FScenario& S, const FVector& Location, const FRotator& Rotation, float FOV = CineFov)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = S.World()->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Rotation, Location), Params);
		UCameraComponent* Camera = NewObject<UCameraComponent>(Actor, TEXT("Camera"));
		Camera->SetMobility(EComponentMobility::Movable);
		Camera->SetFieldOfView(FOV);
		Camera->SetConstraintAspectRatio(false);
		Actor->SetRootComponent(Camera);
		Camera->RegisterComponent();
		Actor->SetActorLocationAndRotation(Location, Rotation);
		S.Spawned.Add(Actor);
		return Actor;
	}

	/**
	 * Camera Cut Track 1 本の Level Sequence を作り、Level Sequence Actor（再生は呼び出し側）を返す。
	 * Camera が null なら Camera Cut なしの空の Sequence。EaseIn / EaseOut > 0 で Sequencer 自身のブレンド（PCM の ViewTarget Blend）。
	 */
	static ALevelSequenceActor* MakeSequence(FScenario& S, AActor* Camera, double Seconds, double EaseIn = 0.0, double EaseOut = 0.0,
		EMovieSceneCompletionMode Completion = EMovieSceneCompletionMode::RestoreState)
	{
		ULevelSequence* Sequence = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transient);
		Sequence->Initialize();
		S.Keep.Emplace(Sequence);
		UMovieScene* MovieScene = Sequence->GetMovieScene();
		const FFrameRate Resolution = MovieScene->GetTickResolution();
		const FFrameNumber End = Resolution.AsFrameNumber(Seconds);
		MovieScene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), End));

		FGuid Binding;
		if (Camera)
		{
			Binding = MovieScene->AddPossessable(Camera->GetName(), Camera->GetClass());
			Sequence->BindPossessableObject(Binding, *Camera, S.World());
			UMovieSceneCameraCutTrack* Track = CastChecked<UMovieSceneCameraCutTrack>(MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
			Track->bCanBlend = true;
			UMovieSceneCameraCutSection* Section = Track->AddNewCameraCut(UE::MovieScene::FRelativeObjectBindingID(Binding), FFrameNumber(0));
			Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), End));
			// ゲームプレイへ戻すには Restore State が必要（Ease Out のブレンドも保存した ViewTarget へ向かう）。
			Section->EvalOptions.CompletionMode = Completion;
			auto MakeEasing = [Section]()
			{
				UMovieSceneBuiltInEasingFunction* Function = NewObject<UMovieSceneBuiltInEasingFunction>(Section);
				Function->Type = EMovieSceneBuiltInEasing::Linear;
				return Function;
			};
			if (EaseIn > 0.0)
			{
				Section->Easing.bManualEaseIn = true;
				Section->Easing.ManualEaseInDuration = Resolution.AsFrameNumber(EaseIn).Value;
				Section->Easing.EaseIn = MakeEasing();
			}
			if (EaseOut > 0.0)
			{
				Section->Easing.bManualEaseOut = true;
				Section->Easing.ManualEaseOutDuration = Resolution.AsFrameNumber(EaseOut).Value;
				Section->Easing.EaseOut = MakeEasing();
			}
		}

		ALevelSequenceActor* Actor = nullptr;
		ULevelSequencePlayer::CreateLevelSequencePlayer(S.World(), Sequence, FMovieSceneSequencePlaybackSettings(), Actor);
		if (Actor && Camera)
		{
			// 実行時のバインド上書き（Transient な Sequence でも確実に解決される）。
			Actor->SetBinding(UE::MovieScene::FRelativeObjectBindingID(Binding), { Camera });
		}
		return Actor;
	}

	static ULevelSequencePlayer* Player(const TWeakObjectPtr<ALevelSequenceActor>& Actor)
	{
		return Actor.IsValid() ? Actor->GetSequencePlayer() : nullptr;
	}

	static void SetReturnBlend(FScenario& S, float Seconds, EACFSequencerControlRotationPolicy Policy = EACFSequencerControlRotationPolicy::Keep)
	{
		FACFSequencerCameraOptions Options;
		Options.ReturnBlend.Curve = Seconds > 0.f ? EACFCameraBlendCurve::Linear : EACFCameraBlendCurve::Cut;
		Options.ReturnBlend.Duration = Seconds;
		Options.ControlRotationPolicy = Policy;
		UACFSequencerBlueprintLibrary::SetSequencerOptionsForPlayer(S.Controller.Get(), Options);
	}

	// "No jump" threshold used below: a frame may move less than 20 % of GameplayToCine. A cut moves 100 %. The fastest
	// smooth transition in these tests is a Linear 0.25 s return at the fixed 1/60 s step = 1/15 (6.7 %) per frame
	// (0.5 s: 3.3 %), and PCM Ease blends of the same length stay in that range, so 20 % keeps a 3x margin over any
	// tested blend while catching a cut or a half-way pop (B-11 / audit T-7: the rationale of the threshold).
	static const double GameplayToCine = FVector::Dist(GameplayLoc, CineLoc);
}

// -------------------------------------------------------------------------------------------------
// 開始・終了（カットで入り、カットで戻る Sequence）: 再生中は ACF が裏で評価を続け、戻りは ACF の復帰ブレンド。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerCutInOutTest, "ActionCameraFramework.Sequencer.StartEndWithReturnBlend", ACFSeqTest::Flags)
bool FACFSequencerCutInOutTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 1.0);
		SetReturnBlend(S, 0.5f);
		return S.Test->TestNotNull(TEXT("sequence actor created"), S.SequenceA.Get());
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("Before the sequence: ACF gameplay camera"));
		S.Test->TestTrue(TEXT("Before: phase None"), S.Phase() == EACFSequencerCameraPhase::None);
		S.Test->TestFalse(TEXT("Before: Blueprint query says the sequencer is not in control"), UACFSequencerBlueprintLibrary::IsSequencerControllingCamera(S.Controller.Get()));
		Player(S.SequenceA)->Play();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::InControl)
		{
			S.bRetry = true;
			return true;
		}
		const FACFSequencerCameraState State = UACFSequencerBlueprintLibrary::GetSequencerCameraState(S.Controller.Get());
		S.Test->TestTrue(TEXT("In control: reported by the Blueprint state"), State.bSequencerControlsCamera);
		S.Test->TestEqual(TEXT("In control: one active sequence"), State.NumActiveSequences, 1);
		S.Test->TestTrue(TEXT("In control: sequence actor reported"), State.SequenceActor == S.SequenceA.Get());
		S.Test->TestTrue(TEXT("In control: sequencer view target is the cine camera"), State.SequencerViewTarget == S.ActorA.Get());
		S.Test->TestEqual(TEXT("Started event"), S.Listener->NumStarted, 1);
		S.Test->TestTrue(TEXT("Started for this player"), S.Listener->LastStartedController == S.Controller);
		S.ResetSamples(); // カットで入った直後から数える
		return true;
	});
	WaitFrames(Steps, 20);
	Steps.Add([](FScenario& S)
	{
		// Sequencer のカメラがそのまま映る。ACF は Weight を落とさず（抑制ではなくバイパス）、裏で毎フレーム評価している。
		S.CameraNear(CineLoc, 0.1, TEXT("During the sequence: the cine camera is on screen"));
		S.Test->TestEqual(TEXT("During: cine FOV"), S.PCM()->GetFOVAngle(), CineFov, 0.01f);
		S.Test->TestTrue(TEXT("During: ACF output bypassed"), S.Acf->IsOutputBypassed());
		S.Test->TestTrue(TEXT("During: state reports the bypass"), S.Sequencer->GetCameraState(S.Controller.Get()).bOutputBypassed);
		S.Test->TestFalse(TEXT("During: not suppressed (the camera is an external view target instead)"), S.Acf->IsOutputSuppressed());
		S.Test->TestEqual(TEXT("During: ACF weight untouched"), S.MinAcfWeight, 1.f);
		S.Test->TestTrue(FString::Printf(TEXT("During: ACF evaluated every frame in the background (max lag %d frames)"), S.MaxEvaluationLag), S.MaxEvaluationLag <= 1);
		S.Test->TestTrue(TEXT("During: the gameplay request is still alive and active"), S.Acf->IsActiveCamera(S.Handle));
		S.Test->TestTrue(TEXT("During: ACF pose is the gameplay camera"), S.Acf->GetLastEvaluationResult().Pose.Location.Equals(GameplayLoc, 0.1));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::None)
		{
			S.bRetry = true;
			return true;
		}
		// 再生が終わり、Sequencer はカットで ViewTarget を戻した。ACF は Sequencer の最終姿勢から Return Blend を始めている。
		S.Test->TestEqual(TEXT("Ended event"), S.Listener->NumEnded, 1);
		S.Test->TestTrue(TEXT("Return kind: ACF return blend"), S.Listener->LastReturnKind == EACFSequencerReturnKind::AcfReturnBlend);
		S.Test->TestTrue(TEXT("Return: ACF is blending"), S.Acf->GetLastEvaluationResult().bIsBlending);
		S.Test->TestFalse(TEXT("Return: no longer bypassed"), S.Acf->IsOutputBypassed());
		S.CameraNear(CineLoc, GameplayToCine * 0.2, TEXT("Return, first frames: still close to the sequencer's last pose (no jump)"));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Acf->GetLastEvaluationResult().bIsBlending)
		{
			S.bRetry = true;
			return true;
		}
		S.CameraNear(GameplayLoc, 0.1, TEXT("After the return blend: gameplay camera"));
		S.Test->TestEqual(TEXT("After: gameplay FOV"), S.PCM()->GetFOVAngle(), GameplayFov, 0.01f);
		S.Test->TestTrue(FString::Printf(TEXT("Whole return: largest move in one frame %.1f cm (a cut would be %.0f)"), S.MaxFrameJump, GameplayToCine), S.MaxFrameJump < GameplayToCine * 0.2);
		S.Test->TestEqual(TEXT("ACF weight never dropped"), S.MinAcfWeight, 1.f);
		S.Test->TestEqual(TEXT("No suppression was used"), S.MaxSuppressions, 0);
		S.Test->TestTrue(TEXT("Same request still in control (gameplay camera state kept)"), S.Acf->IsActiveCamera(S.Handle));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Camera Cut の Ease In / Ease Out: ブレンドは PCM の ViewTarget Blend だけ。ACF の Weight もブレンドも動かない。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerCameraCutBlendTest, "ActionCameraFramework.Sequencer.CameraCutBlendIsNotDoubled", ACFSeqTest::Flags)
bool FACFSequencerCameraCutBlendTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 2.0, 0.5, 0.5);
		SetReturnBlend(S, 0.5f); // Sequencer がブレンドで戻る場合は使われないことを確認する
		S.Probe = Cast<UACFSequencerTestProbeModifier>(S.PCM()->AddNewCameraModifier(UACFSequencerTestProbeModifier::StaticClass()));
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("Before"));
		S.ResetSamples();
		Player(S.SequenceA)->Play();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::BlendingIn)
		{
			S.bRetry = true;
			return true;
		}
		// Ease In 中の実際の呼び出し経路: 切替元（ゲームプレイ）には ACF が 100%、行き先（Sequencer のカメラ）は素通し。
		TArray<UACFSequencerTestProbeModifier::FCall> Calls;
		S.Probe->GetLastFrameCalls(Calls);
		const UACFSequencerTestProbeModifier::FCall* Outgoing = Calls.FindByPredicate([](const auto& C) { return C.bKnown && !C.bPending; });
		const UACFSequencerTestProbeModifier::FCall* Incoming = Calls.FindByPredicate([](const auto& C) { return C.bPending; });
		S.Test->TestEqual(TEXT("Ease in: two modifier passes (current + pending view target)"), Calls.Num(), 2);
		S.Test->TestTrue(TEXT("Ease in: gameplay view target carries 100% ACF"), Outgoing && Outgoing->Location.Equals(GameplayLoc, 0.1));
		S.Test->TestTrue(TEXT("Ease in: sequencer camera reaches the next modifier untouched"), Incoming && Incoming->Target == S.ActorA && Incoming->Location.Equals(CineLoc, 0.1));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::InControl)
		{
			S.bRetry = true;
			return true;
		}
		S.Test->TestTrue(FString::Printf(TEXT("Blend in was seen before In Control (history: %s)"), *S.PhaseHistory()), S.Phases.Contains(EACFSequencerCameraPhase::BlendingIn));
		S.CameraNear(CineLoc, 0.5, TEXT("In control after the ease in"));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::BlendingOut)
		{
			S.bRetry = true;
			return true;
		}
		// 戻りのブレンド中: 行き先（ゲームプレイの ViewTarget）には ACF が 100% で乗っている。
		S.Test->TestEqual(TEXT("Blending out: ACF weight is 1"), S.Acf->GetLastEvaluationResult().Weight, 1.f);
		S.Test->TestFalse(TEXT("Blending out: ACF is evaluated through the modifier again"), S.Acf->IsOutputBypassed());
		TArray<UACFSequencerTestProbeModifier::FCall> Calls;
		S.Probe->GetLastFrameCalls(Calls);
		const UACFSequencerTestProbeModifier::FCall* Outgoing = Calls.FindByPredicate([](const auto& C) { return C.bKnown && !C.bPending; });
		const UACFSequencerTestProbeModifier::FCall* Incoming = Calls.FindByPredicate([](const auto& C) { return C.bPending; });
		S.Test->TestTrue(TEXT("Ease out: sequencer camera (outgoing) untouched"), Outgoing && Outgoing->Target == S.ActorA && Outgoing->Location.Equals(CineLoc, 0.1));
		S.Test->TestTrue(TEXT("Ease out: gameplay view target (incoming) carries 100% ACF"), Incoming && Incoming->Location.Equals(GameplayLoc, 0.1));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::None || S.PCM()->PendingViewTarget.Target)
		{
			S.bRetry = true;
			return true;
		}
		const TArray<EACFSequencerCameraPhase> Expected = { EACFSequencerCameraPhase::None, EACFSequencerCameraPhase::BlendingIn, EACFSequencerCameraPhase::InControl,
			EACFSequencerCameraPhase::BlendingOut, EACFSequencerCameraPhase::None };
		S.Test->TestTrue(FString::Printf(TEXT("Phases None -> BlendingIn -> InControl -> BlendingOut -> None (got: %s)"), *S.PhaseHistory()), S.Phases == Expected);
		S.Test->TestTrue(TEXT("Return kind: the sequencer's own blend"), S.Listener->LastReturnKind == EACFSequencerReturnKind::SequencerBlend);
		S.Test->TestFalse(TEXT("ACF never ran a blend of its own (no double blend)"), S.bAcfBlended);
		S.Test->TestEqual(TEXT("ACF weight never faded (no double fade)"), S.MinAcfWeight, 1.f);
		S.Test->TestEqual(TEXT("No suppression"), S.MaxSuppressions, 0);
		S.Test->TestTrue(FString::Printf(TEXT("Largest move in one frame %.1f cm over the whole sequence (a cut would be %.0f)"), S.MaxFrameJump, GameplayToCine), S.MaxFrameJump < GameplayToCine * 0.2);
		S.Test->TestTrue(FString::Printf(TEXT("ACF evaluated every frame (max lag %d)"), S.MaxEvaluationLag), S.MaxEvaluationLag <= 1);
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("After"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 一時停止・途中停止・連続再生・Sequence Actor の破棄。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerStopAndReplayTest, "ActionCameraFramework.Sequencer.PauseStopAndConsecutivePlayback", ACFSeqTest::Flags)
bool FACFSequencerStopAndReplayTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.ActorB = SpawnCineCamera(S, CineLocB, CineRotB);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 30.0);
		S.SequenceB = MakeSequence(S, S.ActorB.Get(), 30.0);
		SetReturnBlend(S, 0.5f);
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S) { Player(S.SequenceA)->Play(); return true; });
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Playing: in control"), S.Phase() == EACFSequencerCameraPhase::InControl);
		Player(S.SequenceA)->Pause();
		return true;
	});
	WaitFrames(Steps, 20);
	Steps.Add([](FScenario& S)
	{
		// 一時停止中も Sequencer がカメラを握っている。
		S.Test->TestTrue(TEXT("Paused: still in control"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.CameraNear(CineLoc, 0.1, TEXT("Paused: cine camera"));
		S.Test->TestEqual(TEXT("Paused: no end event"), S.Listener->NumEnded, 0);
		S.ResetSamples();
		Player(S.SequenceA)->Stop(); // 途中停止
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::None)
		{
			S.bRetry = true;
			return true;
		}
		S.Test->TestTrue(TEXT("Stopped midway: ACF return blend"), S.Listener->LastReturnKind == EACFSequencerReturnKind::AcfReturnBlend);
		S.CameraNear(CineLoc, GameplayToCine * 0.2, TEXT("Stopped midway: starts from the sequencer pose"));
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		// 復帰ブレンドの途中で次の Sequence（別のカメラ）を再生する。
		S.Test->TestTrue(TEXT("Return blend still running"), S.Acf->GetLastEvaluationResult().bIsBlending);
		S.Test->TestTrue(FString::Printf(TEXT("Stop: no jump (%.1f cm)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.2);
		Player(S.SequenceB)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Second sequence: in control"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.CameraNear(CineLocB, 0.1, TEXT("Second sequence: its camera"));
		S.Test->TestEqual(TEXT("Second sequence: started twice in total"), S.Listener->NumStarted, 2);
		// 同じフレームで B を止めて A を再生し直す（連続再生）。
		Player(S.SequenceB)->Stop();
		Player(S.SequenceA)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Back-to-back: in control"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.CameraNear(CineLoc, 0.1, TEXT("Back-to-back: first camera again"));
		S.Test->TestTrue(TEXT("Back-to-back: reported view target follows"), S.Sequencer->GetCameraState(S.Controller.Get()).SequencerViewTarget == S.ActorA.Get());
		S.Test->TestTrue(TEXT("Back-to-back: ACF kept evaluating"), S.Acf->IsOutputBypassed() && S.Acf->IsActiveCamera(S.Handle));
		// 再生中に Level Sequence Actor ごと破棄する。
		S.ResetSamples();
		S.SequenceA->Destroy();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::None || S.Acf->GetLastEvaluationResult().bIsBlending)
		{
			S.bRetry = true;
			return true;
		}
		S.CameraNear(GameplayLoc, 0.1, TEXT("Sequence actor destroyed: back on the gameplay camera"));
		S.Test->TestTrue(FString::Printf(TEXT("Destroyed: no jump (%.1f cm)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.2);
		S.Test->TestEqual(TEXT("Every start has its end"), S.Listener->NumEnded, S.Listener->NumStarted);
		S.Test->TestEqual(TEXT("No suppression left"), S.Acf->GetNumOutputSuppressions(), 0);
		S.Test->TestFalse(TEXT("Not bypassed"), S.Acf->IsOutputBypassed());
		S.Test->TestEqual(TEXT("Request count unchanged"), S.Acf->GetLastEvaluationResult().NumRequests, 1);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Lock-On 中の Sequencer: 要求は残り、裏で追従し続け、対象の破棄による自動解除も進む。復帰は最新の構図から。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerLockOnTest, "ActionCameraFramework.Sequencer.LockOnKeepsEvaluatingAndReturns", ACFSeqTest::Flags)
bool FACFSequencerLockOnTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S); // 通常カメラ（Priority 0）
		S.ActorA = SpawnPoint(S, Base + FVector(0.f, -3000.f, 0.f));   // プレイヤー
		S.ActorB = SpawnPoint(S, Base + FVector(800.f, -3000.f, 0.f)); // ロック対象
		UACFCameraPreset* LockOn = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		LockOn->Nodes.Add(NewObject<UACFCameraNode_LockOn>(LockOn));
		LockOn->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		LockOn->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		S.Keep.Emplace(LockOn);
		S.HandleB = S.Acf->RequestCamera(LockOn, UACFCameraBlueprintLibrary::MakeLockOnRequestParams(10, S.ActorA.Get(), S.ActorB.Get(), true));
		S.ActorC = SpawnCineCamera(S, CineLoc, CineRot);
		S.SequenceA = MakeSequence(S, S.ActorC.Get(), 2.5);
		S.SequenceB = MakeSequence(S, S.ActorC.Get(), 1.0);
		SetReturnBlend(S, 0.f); // カットで戻す: 戻った瞬間の姿勢が「裏で評価し続けた結果」であることを直接確認する
		return true;
	});
	WaitFrames(Steps, 60);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Lock-on in control"), S.Acf->IsActiveCamera(S.HandleB));
		S.SavedLocation = S.Camera();
		Player(S.SequenceA)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Sequencer in control"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.CameraNear(CineLoc, 0.1, TEXT("Cine camera on screen"));
		S.Test->TestTrue(TEXT("Lock-on request kept while the sequencer plays"), S.Acf->IsCameraRequestAlive(S.HandleB) && S.Acf->IsActiveCamera(S.HandleB));
		// Sequence の間にプレイヤーと敵が動く（テレポート判定の 1000cm 未満 = Damping で追う動き）。
		S.ActorA->SetActorLocation(S.ActorA->GetActorLocation() + FVector(0.f, 600.f, 0.f));
		S.ActorB->SetActorLocation(S.ActorB->GetActorLocation() + FVector(300.f, 900.f, 0.f));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::None)
		{
			S.bRetry = true;
			return true;
		}
		S.Test->TestTrue(TEXT("Return kind: cut (Return Blend = Cut)"), S.Listener->LastReturnKind == EACFSequencerReturnKind::Cut);
		S.Test->TestTrue(TEXT("Back on the lock-on camera"), S.Acf->IsActiveCamera(S.HandleB));
		S.Test->TestTrue(FString::Printf(TEXT("The lock-on camera followed the actors during the sequence (moved %.0f cm)"), FVector::Dist(S.Camera(), S.SavedLocation)),
			FVector::Dist(S.Camera(), S.SavedLocation) > 300.0);
		S.ResetSamples();
		return true;
	});
	WaitFrames(Steps, 30);
	Steps.Add([](FScenario& S)
	{
		// 戻った時点で Damping は収束済み: 戻ってからカメラが「追いかけて滑る」ことはない。
		S.Test->TestTrue(FString::Printf(TEXT("No catch-up motion after the return (max %.2f cm / frame)"), S.MaxFrameJump), S.MaxFrameJump < 2.0);
		// 2 回目: Sequence の間にロック対象が破棄される → 裏で自動解除され、通常カメラへ戻る。
		S.Listener->ReturnKinds.Reset();
		SetReturnBlend(S, 0.5f);
		Player(S.SequenceB)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Second sequence in control"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.ActorB->Destroy();
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Still in the sequence"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.Test->TestFalse(TEXT("Lock-on released in the background when its target died"), S.Acf->IsCameraRequestAlive(S.HandleB));
		S.Test->TestTrue(TEXT("Gameplay camera is the active request underneath"), S.Acf->IsActiveCamera(S.Handle));
		S.ResetSamples();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::None || S.Acf->GetLastEvaluationResult().bIsBlending)
		{
			S.bRetry = true;
			return true;
		}
		S.Test->TestTrue(TEXT("Second return: ACF return blend"), S.Listener->ReturnKinds.Contains(EACFSequencerReturnKind::AcfReturnBlend));
		S.CameraNear(GameplayLoc, 0.1, TEXT("Second return: gameplay camera (lock-on gone)"));
		S.Test->TestTrue(FString::Printf(TEXT("Second return: no jump (%.1f cm)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.2);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Finisher（ACF の Camera Sequence）中に Level Sequence が割り込む: Interrupt Policy に従う。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerFinisherInterruptTest, "ActionCameraFramework.Sequencer.FinisherInterruptedBySequencer", ACFSeqTest::Flags)
bool FACFSequencerFinisherInterruptTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	static const FVector FinisherLoc = Base + FVector(-500.f, -700.f, 150.f);
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		SetReturnBlend(S, 0.25f);
		S.Counter = 0;
		return true;
	});
	for (const EACFCameraSequenceInterruptPolicy Policy : { EACFCameraSequenceInterruptPolicy::Continue, EACFCameraSequenceInterruptPolicy::Pause, EACFCameraSequenceInterruptPolicy::Cancel })
	{
		const FString Name = UEnum::GetValueAsString(Policy).RightChop(35);
		Steps.Add([Policy](FScenario& S)
		{
			UACFCameraSequence* Sequence = NewObject<UACFCameraSequence>(GetTransientPackage(), NAME_None, RF_Transient);
			S.Keep.Emplace(Sequence);
			FACFCameraShot& Shot = Sequence->Shots.AddDefaulted_GetRef();
			Shot.Name = TEXT("Finisher");
			Shot.Preset = MakeFixedPreset(S, FinisherLoc, FRotator(0.f, 30.f, 0.f), 50.f);
			Shot.AdvanceMode = EACFCameraShotAdvanceMode::Duration;
			Shot.Duration = 30.f;
			Shot.BlendIn = FACFCameraBlendSettings::MakeCut();
			Sequence->CancelBlend = FACFCameraBlendSettings::MakeCut();
			Sequence->InterruptPolicy = Policy;
			S.Finisher = S.Acf->StartFinisherCamera(Sequence, nullptr, nullptr, 1.f);
			S.SequenceA = MakeSequence(S, S.ActorA.Get(), 0.75);
			return S.Test->TestTrue(TEXT("finisher started"), S.Finisher.IsValid());
		});
		WaitFrames(Steps, 12);
		Steps.Add([Name](FScenario& S)
		{
			const FACFCameraSequenceState State = S.Acf->GetCameraSequenceState(S.Finisher);
			S.Test->TestTrue(*(Name + TEXT(": finisher in control before the level sequence")), State.bInControl);
			S.CameraNear(FinisherLoc, 0.1, *(Name + TEXT(": finisher shot on screen")));
			S.Value = State.ShotTime;
			Player(S.SequenceA)->Play();
			return true;
		});
		WaitFrames(Steps, 20);
		Steps.Add([Name, Policy](FScenario& S)
		{
			S.Test->TestTrue(*(Name + TEXT(": level sequence in control")), S.Phase() == EACFSequencerCameraPhase::InControl);
			S.CameraNear(CineLoc, 0.1, *(Name + TEXT(": cine camera on screen")));
			const FACFCameraSequenceState State = S.Acf->GetCameraSequenceState(S.Finisher);
			if (Policy == EACFCameraSequenceInterruptPolicy::Cancel)
			{
				S.Test->TestFalse(*(Name + TEXT(": finisher cancelled by the interruption")), State.bIsPlaying);
				S.Test->TestEqual(*(Name + TEXT(": its shot request is gone")), S.Acf->GetLastEvaluationResult().NumRequests, 1);
			}
			else
			{
				S.Test->TestTrue(*(Name + TEXT(": finisher still playing")), State.bIsPlaying);
				S.Test->TestFalse(*(Name + TEXT(": finisher not in control")), State.bInControl);
				const double Advanced = State.ShotTime - S.Value;
				if (Policy == EACFCameraSequenceInterruptPolicy::Pause)
				{
					S.Test->TestTrue(FString::Printf(TEXT("%s: shot time frozen (advanced %.3fs)"), *Name, Advanced), Advanced < 0.1);
				}
				else
				{
					S.Test->TestTrue(FString::Printf(TEXT("%s: shot time kept running (advanced %.3fs)"), *Name, Advanced), Advanced > 0.25);
				}
			}
			S.ResetSamples();
			return true;
		});
		Steps.Add([Name, Policy](FScenario& S)
		{
			if (S.Phase() != EACFSequencerCameraPhase::None || S.Acf->GetLastEvaluationResult().bIsBlending)
			{
				S.bRetry = true;
				return true;
			}
			const FVector Expected = Policy == EACFCameraSequenceInterruptPolicy::Cancel ? GameplayLoc : FinisherLoc;
			S.CameraNear(Expected, 0.1, *(Name + TEXT(": returned to the camera that is in control now")));
			S.Test->TestTrue(FString::Printf(TEXT("%s: return without a jump (%.1f cm)"), *Name, S.MaxFrameJump), S.MaxFrameJump < FVector::Dist(CineLoc, Expected) * 0.4);
			if (Policy != EACFCameraSequenceInterruptPolicy::Cancel)
			{
				S.Test->TestTrue(*(Name + TEXT(": finisher in control again")), S.Acf->GetCameraSequenceState(S.Finisher).bInControl);
			}
			S.Acf->CancelCameraSequence(S.Finisher);
			S.SequenceA->Destroy();
			return true;
		});
		WaitFrames(Steps, 5);
	}
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Camera Component を持つ普通の Actor への Camera Cut（Camera Actor と同じ扱い）と、出力抑制の重複・解除順序。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerSuppressionOverlapTest, "ActionCameraFramework.Sequencer.SuppressionOverlapAndReleaseOrder", ACFSeqTest::Flags)
bool FACFSequencerSuppressionOverlapTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.ActorA = SpawnComponentCamera(S, CineLoc, CineRot); // Camera Actor ではないカメラ
		S.ActorB = SpawnPoint(S, Base);                        // ゲーム側の抑制の Source
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 30.0);
		SetReturnBlend(S, 0.f);
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S) { Player(S.SequenceA)->Play(); return true; });
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		// 1) Camera Cut は抑制ではなく外部 ViewTarget として扱われる。その間にゲームが抑制し、Sequence が先に終わる。
		S.Test->TestTrue(TEXT("In control"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.Test->TestTrue(TEXT("The sequencer camera is registered as an external view target"), S.Acf->IsExternalViewTarget(S.ActorA.Get()));
		S.Test->TestEqual(TEXT("No suppression is used for a camera cut"), S.Acf->GetNumOutputSuppressions(), 0);
		S.Test->TestTrue(TEXT("ACF bypassed"), S.Acf->IsOutputBypassed());
		S.CameraNear(CineLoc, 0.1, TEXT("The sequencer camera is on screen, not ACF"));
		S.Acf->SuppressOutput(S.ActorB.Get(), 0.f);
		S.Test->TestEqual(TEXT("The game's suppression"), S.Acf->GetNumOutputSuppressions(), 1);
		Player(S.SequenceA)->Stop();
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Sequence stopped"), S.Phase() == EACFSequencerCameraPhase::None);
		S.Test->TestEqual(TEXT("The game's suppression is still there"), S.Acf->GetNumOutputSuppressions(), 1);
		S.Test->TestTrue(TEXT("ACF still suppressed by the game"), S.Acf->GetLastEvaluationResult().Weight == 0.f);
		S.Test->TestFalse(TEXT("ACF camera not shown yet"), S.Camera().Equals(GameplayLoc, 1.0));
		S.Acf->ReleaseOutputSuppression(S.ActorB.Get(), 0.f);
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("Both released: ACF camera back"));
		// 2) ゲーム → Sequencer の順に抑制し、ゲーム → Sequencer の順に解除する。
		S.Acf->SuppressOutput(S.ActorB.Get(), 0.f);
		Player(S.SequenceA)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Reverse order: only the game's suppression"), S.Acf->GetNumOutputSuppressions(), 1);
		S.Acf->ReleaseOutputSuppression(S.ActorB.Get(), 0.f);
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Reverse order: released"), S.Acf->GetNumOutputSuppressions(), 0);
		S.CameraNear(CineLoc, 0.1, TEXT("Reverse order: still the sequencer camera"));
		SetReturnBlend(S, 0.5f);
		S.ResetSamples();
		Player(S.SequenceA)->Stop();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::None || S.Acf->GetLastEvaluationResult().bIsBlending)
		{
			S.bRetry = true;
			return true;
		}
		S.Test->TestEqual(TEXT("Reverse order: nothing left"), S.Acf->GetNumOutputSuppressions(), 0);
		S.CameraNear(GameplayLoc, 0.1, TEXT("Reverse order: ACF camera back"));
		S.Test->TestFalse(TEXT("External view target cleared after the return"), S.Acf->IsExternalViewTarget(S.ActorA.Get()));
		S.Test->TestTrue(FString::Printf(TEXT("Return blend from a component camera: no jump (%.1f cm)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.2);
		// 3) Camera Cut の無い Sequence + 「再生中は抑制」オプション（Level Sequence Actor 単位）。
		S.SequenceB = MakeSequence(S, nullptr, 30.0);
		FACFSequencerCameraOptions Options;
		Options.bSuppressOutputWhilePlaying = true;
		Options.OutputBlendTime = 0.f;
		UACFSequencerBlueprintLibrary::SetSequencerOptionsForSequence(S.SequenceB.Get(), Options);
		Player(S.SequenceB)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		const FACFSequencerCameraState State = S.Sequencer->GetCameraState(S.Controller.Get());
		S.Test->TestTrue(TEXT("No camera cut: the sequencer does not own the view"), State.Phase == EACFSequencerCameraPhase::None);
		S.Test->TestEqual(TEXT("No camera cut: one active sequence"), State.NumActiveSequences, 1);
		S.Test->TestTrue(TEXT("Whole-sequence option: ACF output suppressed"), State.bSuppressingOutput && S.Acf->IsOutputSuppressed());
		S.Test->TestFalse(TEXT("Whole-sequence option: UE camera on screen"), S.Camera().Equals(GameplayLoc, 1.0));
		Player(S.SequenceB)->Stop();
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Whole-sequence option: released at the end"), S.Acf->GetNumOutputSuppressions(), 0);
		S.CameraNear(GameplayLoc, 0.1, TEXT("Whole-sequence option: ACF camera back"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// ControlRotation の整合: Match Sequencer Camera で、戻り先の向きを Sequencer の最後のカメラに合わせる。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerControlRotationTest, "ActionCameraFramework.Sequencer.ControlRotationOnReturn", ACFSeqTest::Flags)
bool FACFSequencerControlRotationTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.ActorA = SpawnCineCamera(S, CineLoc, FRotator(-10.f, 90.f, 25.f)); // Roll つき
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 0.5);
		S.SequenceB = MakeSequence(S, S.ActorA.Get(), 1.5, 0.0, 0.5);
		S.Controller->SetControlRotation(FRotator(0.f, -120.f, 0.f));
		SetReturnBlend(S, 0.25f, EACFSequencerControlRotationPolicy::Keep);
		Player(S.SequenceA)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::None)
		{
			S.bRetry = true;
			return true;
		}
		S.Test->TestTrue(FString::Printf(TEXT("Keep: control rotation untouched (%s)"), *S.Controller->GetControlRotation().ToCompactString()),
			S.Controller->GetControlRotation().Equals(FRotator(0.f, -120.f, 0.f), 0.1f));
		SetReturnBlend(S, 0.25f, EACFSequencerControlRotationPolicy::MatchSequencerCamera);
		S.Sequencer->RefreshNow();
		Player(S.SequenceA)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::None)
		{
			S.bRetry = true;
			return true;
		}
		const FRotator Control = S.Controller->GetControlRotation().GetNormalized();
		S.Test->TestTrue(FString::Printf(TEXT("Match (cut return): yaw / pitch from the sequencer camera, roll 0 (%s)"), *Control.ToCompactString()),
			Control.Equals(FRotator(-10.f, 90.f, 0.f), 0.1f));
		// Sequencer 自身がブレンドで戻る場合は、ブレンドが始まる時点（ゲームプレイカメラが見え始める時点）で合わせる。
		S.Controller->SetControlRotation(FRotator(0.f, -120.f, 0.f));
		Player(S.SequenceB)->Play();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::BlendingOut)
		{
			S.bRetry = true;
			return true;
		}
		const FRotator Control = S.Controller->GetControlRotation().GetNormalized();
		S.Test->TestTrue(FString::Printf(TEXT("Match (blended return): applied when the blend out starts (%s)"), *Control.ToCompactString()),
			Control.Equals(FRotator(-10.f, 90.f, 0.f), 0.1f));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.bRetry = S.Phase() != EACFSequencerCameraPhase::None || S.PCM()->PendingViewTarget.Target != nullptr;
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// PlayerCameraManager の作り直し（Sequence 再生中）。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerCameraManagerRecreatedTest, "ActionCameraFramework.Sequencer.CameraManagerRecreatedDuringSequence", ACFSeqTest::Flags)
bool FACFSequencerCameraManagerRecreatedTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 30.0);
		SetReturnBlend(S, 0.25f);
		Player(S.SequenceA)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("In control on the first PCM"), S.Phase() == EACFSequencerCameraPhase::InControl);
		// 同じ PlayerController のまま PCM だけ作り直す（PlayerControllerChanged は来ない）。
		APlayerCameraManager* Old = S.PCM();
		S.OldCameraManager = Old;
		S.Controller->SpawnPlayerCameraManager();
		if (S.PCM() != Old)
		{
			Old->Destroy();
		}
		return S.Test->TestTrue(TEXT("A new PCM was spawned"), S.PCM() != S.OldCameraManager.Get());
	});
	WaitFrames(Steps, 20);
	Steps.Add([](FScenario& S)
	{
		// Sequencer は次の評価で新しい PCM に Camera Cut を掛け直す。ブリッジはそれを改めて検出する。
		S.Test->TestTrue(TEXT("In control again on the new PCM"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.CameraNear(CineLoc, 0.1, TEXT("New PCM shows the cine camera"));
		S.Test->TestTrue(TEXT("ACF keeps evaluating for the new PCM"), S.Acf->IsOutputBypassed() && S.Acf->IsActiveCamera(S.Handle));
		S.Test->TestEqual(TEXT("No suppression leaked across the PCM change"), S.Acf->GetNumOutputSuppressions(), 0);
		S.ResetSamples();
		Player(S.SequenceA)->Stop();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::None || S.Acf->GetLastEvaluationResult().bIsBlending)
		{
			S.bRetry = true;
			return true;
		}
		S.Test->TestNotNull(TEXT("ACF output modifier attached to the new PCM"), S.PCM()->FindCameraModifierByClass(UACFCameraOutputModifier::StaticClass()));
		S.CameraNear(GameplayLoc, 0.1, TEXT("Back on the gameplay camera on the new PCM"));
		S.Test->TestTrue(FString::Printf(TEXT("Return on the new PCM without a jump (%.1f cm)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.3);
		S.Test->TestEqual(TEXT("Every start has its end"), S.Listener->NumEnded, S.Listener->NumStarted);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// ローカルプレイヤー間の独立性。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerLocalPlayersTest, "ActionCameraFramework.Sequencer.LocalPlayersAreIndependent", ACFSeqTest::Flags)
bool FACFSequencerLocalPlayersTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	// 2 人目の PlayerController は BeginPlay の時点ではまだ LocalPlayer が付いていない。ホストプロジェクトの Blueprint の
	// PlayerController（REBoarGetch の BP_PC_Lobby）は BeginPlay で HUD を CreateWidget し、UMG がエラーを出す（ACF とは無関係）。
	// B-9: そのエラーを予期するのは、PlayerController が Blueprint のときだけ。ACF のテスト環境（エンジン標準の
	// GameModeBase / PlayerController、README 22 章）では何も無視せず、エラーが出ればテストは失敗する。
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this); // B-9: only for a host Blueprint PlayerController
	static const FVector SecondLoc = Base + FVector(0.f, 8000.f, 0.f);
	auto SecondController = [](FScenario& S) { return S.SecondPlayer.IsValid() ? S.SecondPlayer->GetPlayerController(S.World()) : nullptr; };
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		FString Error;
		S.SecondPlayer = S.World()->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(1), Error, true);
		if (!S.SecondPlayer.IsValid())
		{
			S.Test->AddError(FString::Printf(TEXT("Could not create a second local player: %s"), *Error));
			return false;
		}
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([SecondController](FScenario& S)
	{
		APlayerController* PC2 = SecondController(S);
		UACFCameraSubsystem* Acf2 = S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>();
		if (!PC2 || !PC2->PlayerCameraManager || !Acf2)
		{
			S.Test->AddError(TEXT("Second local player has no controller / camera manager / ACF subsystem."));
			return false;
		}
		S.HandleB = Acf2->RequestCamera(MakeFixedPreset(S, SecondLoc, FRotator(0.f, 180.f, 0.f), 70.f), FACFCameraRequestParams());
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 30.0); // Owner なし → 最初のローカルプレイヤーが対象
		S.SequenceB = MakeSequence(S, S.ActorA.Get(), 30.0);
		S.SequenceB->SetOwner(PC2);                           // Owner = 2 人目の PlayerController → 2 人目が対象
		SetReturnBlend(S, 0.f);                               // 1 人目だけカットで戻る設定（2 人目は既定の 0.5 秒）
		Player(S.SequenceA)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([SecondController](FScenario& S)
	{
		APlayerController* PC2 = SecondController(S);
		UACFCameraSubsystem* Acf2 = S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>();
		S.Test->TestTrue(TEXT("Player 1: sequencer in control"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.Test->TestTrue(TEXT("Player 1: ACF bypassed"), S.Acf->IsOutputBypassed());
		S.Test->TestTrue(TEXT("Player 2: not affected by player 1's sequence"), S.Sequencer->GetCameraState(PC2).Phase == EACFSequencerCameraPhase::None);
		S.Test->TestEqual(TEXT("Player 2: no active sequence"), S.Sequencer->GetCameraState(PC2).NumActiveSequences, 0);
		S.Test->TestFalse(TEXT("Player 2: ACF not bypassed"), Acf2->IsOutputBypassed());
		S.Test->TestFalse(TEXT("Player 2: ACF not suppressed"), Acf2->IsOutputSuppressed());
		S.Test->TestTrue(FString::Printf(TEXT("Player 2: its own ACF camera on screen (%s)"), *PC2->PlayerCameraManager->GetCameraLocation().ToCompactString()),
			PC2->PlayerCameraManager->GetCameraLocation().Equals(SecondLoc, 0.1));
		S.Test->TestTrue(TEXT("Started event names player 1"), S.Listener->LastStartedController == S.Controller);
		Player(S.SequenceA)->Stop();
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([SecondController](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Player 1: back"), S.Phase() == EACFSequencerCameraPhase::None);
		S.Test->TestTrue(TEXT("Player 1: cut return (its own option)"), S.Listener->LastReturnKind == EACFSequencerReturnKind::Cut);
		S.CameraNear(GameplayLoc, 0.1, TEXT("Player 1: gameplay camera"));
		Player(S.SequenceB)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([SecondController](FScenario& S)
	{
		APlayerController* PC2 = SecondController(S);
		UACFCameraSubsystem* Acf2 = S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>();
		S.Test->TestTrue(TEXT("Player 2: sequencer in control (sequence owned by its controller)"), S.Sequencer->GetCameraState(PC2).Phase == EACFSequencerCameraPhase::InControl);
		S.Test->TestTrue(TEXT("Player 2: ACF bypassed"), Acf2->IsOutputBypassed());
		S.Test->TestTrue(TEXT("Player 2: cine camera on screen"), PC2->PlayerCameraManager->GetCameraLocation().Equals(CineLoc, 0.1));
		S.Test->TestTrue(TEXT("Player 1: untouched"), S.Phase() == EACFSequencerCameraPhase::None && !S.Acf->IsOutputBypassed());
		S.CameraNear(GameplayLoc, 0.1, TEXT("Player 1: still its gameplay camera"));
		S.Test->TestTrue(TEXT("Started event names player 2"), S.Listener->LastStartedController == PC2);
		Player(S.SequenceB)->Stop();
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([SecondController](FScenario& S)
	{
		APlayerController* PC2 = SecondController(S);
		UACFCameraSubsystem* Acf2 = S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>();
		S.Test->TestTrue(TEXT("Player 2: back"), S.Sequencer->GetCameraState(PC2).Phase == EACFSequencerCameraPhase::None);
		S.Test->TestTrue(TEXT("Player 2: default option = ACF return blend"), S.Listener->LastReturnKind == EACFSequencerReturnKind::AcfReturnBlend);
		S.Test->TestTrue(TEXT("Player 2: its return blend is running"), Acf2->GetLastEvaluationResult().bIsBlending);
		S.Test->TestFalse(TEXT("Player 1: no blend started by player 2's return"), S.Acf->GetLastEvaluationResult().bIsBlending);
		return true;
	});
	WaitFrames(Steps, 45);
	Steps.Add([SecondController](FScenario& S)
	{
		APlayerController* PC2 = SecondController(S);
		S.Test->TestTrue(TEXT("Player 2: its ACF camera back"), PC2->PlayerCameraManager->GetCameraLocation().Equals(SecondLoc, 0.1));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}


// =================================================================================================
// Phase 5A 検証で追加したケース。

namespace ACFSeqTest
{
	static UACFSequencerTestProbeModifier* AddProbe(FScenario& S)
	{
		UACFSequencerTestProbeModifier* Probe = Cast<UACFSequencerTestProbeModifier>(S.PCM()->AddNewCameraModifier(UACFSequencerTestProbeModifier::StaticClass()));
		S.Probe = Probe;
		return Probe;
	}

	static FACFCameraPose RenderedPose(FScenario& S)
	{
		const FMinimalViewInfo& View = S.PCM()->GetCameraCacheView();
		FACFCameraPose Pose;
		Pose.Location = View.Location;
		Pose.Rotation = View.Rotation;
		Pose.FieldOfView = View.FOV;
		return Pose;
	}

	static void RetryUntil(TArray<FStep>& Steps, TFunction<bool(FScenario&)> Condition)
	{
		Steps.Add([Condition](FScenario& S) { S.bRetry = !Condition(S); return true; });
	}
}

// -------------------------------------------------------------------------------------------------
// Core の外部 ViewTarget API（Sequencer なし）と、通常の SetViewTargetWithBlend との共存。PCM の実際の呼び出し経路を
// プローブ Modifier（ACF の直後）で観察する。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerExternalViewTargetCoreTest, "ActionCameraFramework.Sequencer.ExternalViewTargetCoreApiAndViewTargetBlend", ACFSeqTest::Flags)
bool FACFSequencerExternalViewTargetCoreTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.GameplayViewTarget = S.PCM()->GetViewTarget();
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		AddProbe(S);
		return S.Test->TestNotNull(TEXT("probe modifier added"), S.Probe.Get());
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("Before"));
		S.Controller->SetViewTarget(S.ActorA.Get()); // 登録していない普通の Camera Actor へカット
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		// 登録していない ViewTarget には ACF がそのまま乗る（ACF は ViewTarget に関係なく上に乗るレイヤー。従来どおり）。
		TArray<UACFSequencerTestProbeModifier::FCall> Calls;
		S.Probe->GetLastFrameCalls(Calls);
		S.Test->TestEqual(TEXT("Unregistered camera actor: one modifier pass per frame"), Calls.Num(), 1);
		if (Calls.Num() == 1)
		{
			S.Test->TestTrue(TEXT("Unregistered: the pass is for the camera actor (ViewTarget.POV)"), Calls[0].bKnown && !Calls[0].bPending && Calls[0].Target == S.ActorA);
			S.Test->TestTrue(TEXT("Unregistered: ACF wrote the gameplay pose into it"), Calls[0].Location.Equals(GameplayLoc, 0.1));
		}
		S.CameraNear(GameplayLoc, 0.1, TEXT("Unregistered camera actor: ACF camera on screen"));
		S.Test->TestFalse(TEXT("Unregistered: not bypassed"), S.Acf->IsOutputBypassed());
		S.Acf->AddExternalViewTarget(S.ActorA.Get(), S.Listener.Get());
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		TArray<UACFSequencerTestProbeModifier::FCall> Calls;
		S.Probe->GetLastFrameCalls(Calls);
		S.Test->TestTrue(TEXT("Registered: the camera actor's POV reaches the next modifier untouched"), Calls.Num() == 1 && Calls[0].Target == S.ActorA && Calls[0].Location.Equals(CineLoc, 0.1));
		S.CameraNear(CineLoc, 0.1, TEXT("Registered external view target: its own camera on screen"));
		S.Test->TestTrue(TEXT("Registered: IsExternalViewTarget"), S.Acf->IsExternalViewTarget(S.ActorA.Get()));
		S.Test->TestTrue(TEXT("Registered: ACF bypassed (evaluated in the background)"), S.Acf->IsOutputBypassed());
		S.Test->TestTrue(FString::Printf(TEXT("Registered: background evaluation every frame (lag %d)"), S.MaxEvaluationLag), S.MaxEvaluationLag <= 1);
		S.Test->TestTrue(TEXT("Registered: no sequencer involvement"), S.Phase() == EACFSequencerCameraPhase::None);
		S.ResetSamples();
		// ゲームが普通に PCM のブレンドでゲームプレイの ViewTarget へ戻す。
		S.Controller->SetViewTargetWithBlend(S.GameplayViewTarget.Get(), 1.f, VTBlend_Linear);
		return true;
	});
	WaitFrames(Steps, 30);
	Steps.Add([](FScenario& S)
	{
		// PCM の ViewTarget Blend 中: 切替元（外部）と行き先（ゲームプレイ）に 1 回ずつ Modifier が呼ばれる。
		TArray<UACFSequencerTestProbeModifier::FCall> Calls;
		S.Probe->GetLastFrameCalls(Calls);
		const UACFSequencerTestProbeModifier::FCall* Outgoing = Calls.FindByPredicate([](const auto& C) { return C.bKnown && !C.bPending; });
		const UACFSequencerTestProbeModifier::FCall* Incoming = Calls.FindByPredicate([](const auto& C) { return C.bPending; });
		S.Test->TestEqual(TEXT("View target blend: two modifier passes per frame"), Calls.Num(), 2);
		S.Test->TestTrue(TEXT("Blend: outgoing pass = external camera, untouched by ACF"), Outgoing && Outgoing->Target == S.ActorA && Outgoing->Location.Equals(CineLoc, 0.1));
		S.Test->TestTrue(TEXT("Blend: incoming pass = gameplay view target with 100% ACF"), Incoming && Incoming->Target == S.GameplayViewTarget && Incoming->Location.Equals(GameplayLoc, 0.1));
		S.Test->TestEqual(TEXT("Blend: ACF weight 1"), S.Acf->GetLastEvaluationResult().Weight, 1.f);
		const double ToGameplay = FVector::Dist(S.Camera(), GameplayLoc);
		S.Test->TestTrue(FString::Printf(TEXT("Blend: screen between the two cameras (%.0f cm from gameplay)"), ToGameplay), ToGameplay > GameplayToCine * 0.2 && ToGameplay < GameplayToCine * 0.8);
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.PCM()->PendingViewTarget.Target == nullptr; });
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("After SetViewTargetWithBlend back: gameplay camera"));
		S.Test->TestFalse(TEXT("Blend back: ACF never blended on its own (single blend = PCM)"), S.bAcfBlended);
		S.Test->TestEqual(TEXT("Blend back: ACF weight never faded"), S.MinAcfWeight, 1.f);
		S.Test->TestTrue(FString::Printf(TEXT("Blend back: smooth (max %.1f cm / frame)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.05);
		S.Test->TestFalse(TEXT("Blend back: not bypassed"), S.Acf->IsOutputBypassed());
		S.ResetSamples();
		S.Controller->SetViewTargetWithBlend(S.ActorA.Get(), 1.f, VTBlend_Linear); // ブレンドで外部カメラへ入る
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.PCM()->PendingViewTarget.Target == nullptr; });
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(CineLoc, 0.1, TEXT("Blend into the external camera"));
		S.Test->TestFalse(TEXT("Blend in: ACF never blended on its own"), S.bAcfBlended);
		S.Test->TestTrue(FString::Printf(TEXT("Blend in: smooth (max %.1f cm / frame)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.05);
		// 同じ ViewTarget を 2 つの Source で登録し、1 つずつ外す。
		UACFSequencerTestListener* Temp = NewObject<UACFSequencerTestListener>();
		S.Keep.Emplace(Temp);
		S.Acf->AddExternalViewTarget(S.ActorA.Get(), Temp);
		S.Acf->RemoveExternalViewTarget(S.ActorA.Get(), S.Listener.Get());
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(CineLoc, 0.1, TEXT("One of two sources removed: still external"));
		UObject* Temp = S.Keep.Last().Get();
		S.Keep.Pop();
		Temp->MarkAsGarbage(); // 残りの Source が破棄された
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestFalse(TEXT("Source destroyed: registration dropped"), S.Acf->IsExternalViewTarget(S.ActorA.Get()));
		S.CameraNear(GameplayLoc, 0.1, TEXT("Source destroyed: ACF on the camera actor again"));
		S.Acf->AddExternalViewTarget(S.ActorA.Get(), S.Listener.Get());
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(CineLoc, 0.1, TEXT("Registered again"));
		// ゲーム側でカットで戻し、Blend ACF Camera From Pose で滑らかにする（Sequencer モジュールを使わない復帰）。
		S.SavedLocation = S.Camera();
		const FACFCameraPose From = RenderedPose(S);
		S.Controller->SetViewTarget(S.GameplayViewTarget.Get());
		FACFCameraBlendSettings Blend;
		Blend.Curve = EACFCameraBlendCurve::Linear;
		Blend.Duration = 0.5f;
		S.Test->TestTrue(TEXT("BlendFromPose accepted"), S.Acf->BlendFromPose(From, Blend));
		S.ResetSamples();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const double FirstStep = FVector::Dist(S.Camera(), S.SavedLocation);
		S.Test->TestTrue(FString::Printf(TEXT("BlendFromPose: first frame %.1f cm from the external camera (one frame of 0.5 s = %.1f)"), FirstStep, GameplayToCine / 30.0),
			FirstStep <= GameplayToCine / 30.0 * 1.5);
		S.Test->TestTrue(TEXT("BlendFromPose: ACF blending"), S.Acf->GetLastEvaluationResult().bIsBlending);
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return !S.Acf->GetLastEvaluationResult().bIsBlending; });
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("BlendFromPose: back on the gameplay camera"));
		S.Test->TestTrue(FString::Printf(TEXT("BlendFromPose: smooth (max %.1f cm / frame)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine / 30.0 * 1.5);
		S.Acf->RemoveExternalViewTarget(S.ActorA.Get(), S.Listener.Get());
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Camera Cut が Keep State で終わる（Sequencer が ViewTarget を戻さない）。ゲームが戻すまで「握っている」扱い。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerKeepStateTest, "ActionCameraFramework.Sequencer.KeepStateEndAndGameReturn", ACFSeqTest::Flags)
bool FACFSequencerKeepStateTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.GameplayViewTarget = S.PCM()->GetViewTarget();
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 0.5, 0.0, 0.0, EMovieSceneCompletionMode::KeepState);
		S.SequenceB = MakeSequence(S, S.ActorA.Get(), 0.5, 0.0, 0.0, EMovieSceneCompletionMode::KeepState);
		SetReturnBlend(S, 0.5f);
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S) { Player(S.SequenceA)->Play(); return true; });
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::InControl; });
	RetryUntil(Steps, [](FScenario& S) { return !Player(S.SequenceA)->IsPlaying(); });
	WaitFrames(Steps, 15);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Keep State: playback finished"), !Player(S.SequenceA)->IsPlaying());
		S.Test->TestTrue(TEXT("Keep State: the cine camera is still the view target"), S.PCM()->GetViewTarget() == S.ActorA.Get());
		S.Test->TestTrue(TEXT("Keep State: still treated as held by the sequencer"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.Test->TestEqual(TEXT("Keep State: no active sequence"), S.Sequencer->GetCameraState(S.Controller.Get()).NumActiveSequences, 0);
		S.CameraNear(CineLoc, 0.1, TEXT("Keep State: the cine camera stays on screen (ACF does not overwrite it)"));
		S.Test->TestEqual(TEXT("Keep State: no end event yet"), S.Listener->NumEnded, 0);
		S.ResetSamples();
		S.Controller->SetViewTargetWithBlend(S.GameplayViewTarget.Get(), 0.5f, VTBlend_Linear); // ゲームがブレンドで戻す
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::None && !S.PCM()->PendingViewTarget.Target; });
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(FString::Printf(TEXT("Game blend return: phases (%s)"), *S.PhaseHistory()), S.Phases.Contains(EACFSequencerCameraPhase::BlendingOut));
		S.Test->TestTrue(TEXT("Game blend return: reported as the sequencer-side blend"), S.Listener->LastReturnKind == EACFSequencerReturnKind::SequencerBlend);
		S.Test->TestFalse(TEXT("Game blend return: ACF did not add a blend"), S.bAcfBlended);
		S.Test->TestTrue(FString::Printf(TEXT("Game blend return: smooth (max %.1f cm / frame)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.05);
		S.CameraNear(GameplayLoc, 0.1, TEXT("Game blend return: gameplay camera"));
		Player(S.SequenceB)->Play();
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::InControl && !Player(S.SequenceB)->IsPlaying(); });
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.SavedLocation = S.Camera();
		S.ResetSamples();
		S.Controller->SetViewTarget(S.GameplayViewTarget.Get()); // ゲームがカットで戻す
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Game cut return: ended"), S.Phase() == EACFSequencerCameraPhase::None);
		S.Test->TestTrue(TEXT("Game cut return: ACF return blend"), S.Listener->LastReturnKind == EACFSequencerReturnKind::AcfReturnBlend);
		const double FirstStep = FVector::Dist(S.Camera(), S.SavedLocation);
		S.Test->TestTrue(FString::Printf(TEXT("Game cut return: first frame %.1f cm from the cine camera"), FirstStep), FirstStep < GameplayToCine * 0.05);
		S.Test->TestEqual(TEXT("Every start has its end"), S.Listener->NumEnded, S.Listener->NumStarted);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Sequencer の終了と次の Camera Cut が同じフレームに起きる（ゲームが Stop + Play、OnFinished から次を再生）。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerSameFrameTest, "ActionCameraFramework.Sequencer.EndAndCameraCutInSameFrame", ACFSeqTest::Flags)
bool FACFSequencerSameFrameTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.ActorB = SpawnCineCamera(S, CineLocB, CineRotB);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 0.5);
		S.SequenceB = MakeSequence(S, S.ActorB.Get(), 30.0);
		SetReturnBlend(S, 0.5f);
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S) { Player(S.SequenceB)->Play(); return true; });
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::InControl; });
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		// 1) ゲームコードが同じフレームで B を止めて A を再生する。
		S.ResetSamples();
		Player(S.SequenceB)->Stop();
		Player(S.SequenceA)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Stop + Play: in control"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.CameraNear(CineLoc, 0.1, TEXT("Stop + Play: the new sequence's camera"));
		S.Test->TestEqual(FString::Printf(TEXT("Stop + Play: gameplay camera never shown (phases %s)"), *S.PhaseHistory()), S.GameplayFrames, 0);
		S.Test->TestFalse(TEXT("Stop + Play: no ACF return blend started"), S.bAcfBlended);
		S.Test->TestEqual(TEXT("Stop + Play: events balanced or still open"), S.Listener->NumStarted - S.Listener->NumEnded, 1);
		// 2) A が自然に終わり、その OnFinished から B を再生する（終了と Camera Cut が同じフレーム）。
		S.Listener->Next = Player(S.SequenceB);
		Player(S.SequenceA)->OnFinished.AddDynamic(S.Listener.Get(), &UACFSequencerTestListener::PlayNext);
		S.ResetSamples();
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Listener->NextPlayedFrame != 0; });
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.Test->AddInfo(FString::Printf(TEXT("OnFinished -> Play: phases %s, gameplay frames %d, ACF blended %d, started %d, ended %d"), *S.PhaseHistory(),
			S.GameplayFrames, S.bAcfBlended ? 1 : 0, S.Listener->NumStarted, S.Listener->NumEnded));
		S.Test->TestTrue(TEXT("OnFinished -> Play: in control"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.CameraNear(CineLocB, 0.1, TEXT("OnFinished -> Play: the next sequence's camera"));
		S.Test->TestEqual(TEXT("OnFinished -> Play: gameplay camera never shown"), S.GameplayFrames, 0);
		S.Test->TestFalse(TEXT("OnFinished -> Play: no ACF return blend started"), S.bAcfBlended);
		S.Test->TestEqual(TEXT("OnFinished -> Play: exactly one open start"), S.Listener->NumStarted - S.Listener->NumEnded, 1);
		S.ResetSamples();
		Player(S.SequenceB)->Stop();
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::None && !S.Acf->GetLastEvaluationResult().bIsBlending; });
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("Final return"));
		S.Test->TestTrue(FString::Printf(TEXT("Final return: no jump (%.1f cm)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.2);
		S.Test->TestEqual(TEXT("Every start has its end"), S.Listener->NumEnded, S.Listener->NumStarted);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 複数の Sequence が重なって再生される（A の再生中に B が始まり、先に終わる）。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerOverlapTest, "ActionCameraFramework.Sequencer.OverlappingSequences", ACFSeqTest::Flags)
bool FACFSequencerOverlapTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.ActorB = SpawnCineCamera(S, CineLocB, CineRotB);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 3.0);
		S.SequenceB = MakeSequence(S, S.ActorB.Get(), 1.0);
		SetReturnBlend(S, 0.5f);
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S) { Player(S.SequenceA)->Play(); return true; });
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::InControl; });
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.ResetSamples();
		Player(S.SequenceB)->Play();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(CineLocB, 0.1, TEXT("Overlap: the later sequence's camera"));
		S.Test->TestTrue(TEXT("Overlap: in control"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.Test->TestEqual(TEXT("Overlap: two active sequences"), S.Sequencer->GetCameraState(S.Controller.Get()).NumActiveSequences, 2);
		S.Test->TestEqual(TEXT("Overlap: still one start"), S.Listener->NumStarted, 1);
		S.Test->TestTrue(TEXT("Overlap: reported view target follows"), S.Sequencer->GetCameraState(S.Controller.Get()).SequencerViewTarget == S.ActorB.Get());
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return !Player(S.SequenceB)->IsPlaying(); });
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.Test->AddInfo(FString::Printf(TEXT("After B ended (A still playing): view target %s, camera %s, phases %s"), *GetNameSafe(S.PCM()->GetViewTarget()),
			*S.Camera().ToCompactString(), *S.PhaseHistory()));
		S.Test->TestTrue(TEXT("B ended first: A is still playing"), Player(S.SequenceA)->IsPlaying());
		S.Test->TestTrue(TEXT("B ended first: sequencer still in control"), S.Phase() == EACFSequencerCameraPhase::InControl);
		S.CameraNear(CineLoc, 0.1, TEXT("B ended first: back on A's camera"));
		S.Test->TestEqual(TEXT("B ended first: gameplay camera never shown"), S.GameplayFrames, 0);
		S.Test->TestEqual(TEXT("B ended first: no end event"), S.Listener->NumEnded, 0);
		S.Test->TestFalse(TEXT("B ended first: ACF did not blend"), S.bAcfBlended);
		S.ResetSamples();
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::None; });
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("A ended last: ACF return blend"), S.Listener->LastReturnKind == EACFSequencerReturnKind::AcfReturnBlend);
		S.Test->TestEqual(TEXT("One start, one end for the whole overlap"), S.Listener->NumEnded, 1);
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return !S.Acf->GetLastEvaluationResult().bIsBlending; });
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("Overlap: back on the gameplay camera"));
		S.Test->TestTrue(FString::Printf(TEXT("Overlap: return without a jump (%.1f cm)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.2);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Camera Cut Track の無い Level Sequence、Camera Cut がプレイヤー自身（ゲームプレイの ViewTarget）を指す Level Sequence。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerNoCutAndPawnCutTest, "ActionCameraFramework.Sequencer.NoCameraCutAndGameplayTargetCut", ACFSeqTest::Flags)
bool FACFSequencerNoCutAndPawnCutTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.GameplayViewTarget = S.PCM()->GetViewTarget();
		S.SequenceA = MakeSequence(S, nullptr, 1.0);
		// Camera Cut が現在のゲームプレイ ViewTarget（Pawn または PlayerController）を指す。
		S.SequenceB = MakeSequence(S, S.GameplayViewTarget.Get(), 1.0);
		S.Test->AddInfo(FString::Printf(TEXT("Gameplay view target: %s (pawn %s)"), *GetNameSafe(S.GameplayViewTarget.Get()), *GetNameSafe(S.Controller->GetPawn())));
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.ResetSamples();
		Player(S.SequenceA)->Play();
		return true;
	});
	WaitFrames(Steps, 20);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("No camera cut: the sequence is playing"), Player(S.SequenceA)->IsPlaying());
		S.Test->TestEqual(TEXT("No camera cut: counted as an active sequence"), S.Sequencer->GetCameraState(S.Controller.Get()).NumActiveSequences, 1);
		S.Test->TestTrue(TEXT("No camera cut: sequencer not in control"), S.Phase() == EACFSequencerCameraPhase::None);
		S.CameraNear(GameplayLoc, 0.1, TEXT("No camera cut: ACF camera unchanged"));
		S.Test->TestFalse(TEXT("No camera cut: not bypassed"), S.Acf->IsOutputBypassed());
		S.Test->TestEqual(TEXT("No camera cut: no suppression (option off by default)"), S.MaxSuppressions, 0);
		S.Test->TestEqual(TEXT("No camera cut: no events"), S.Listener->NumStarted, 0);
		Player(S.SequenceA)->Stop();
		S.ResetSamples();
		Player(S.SequenceB)->Play();
		return true;
	});
	WaitFrames(Steps, 20);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Cut to the gameplay target: playing"), Player(S.SequenceB)->IsPlaying());
		S.Test->TestTrue(TEXT("Cut to the gameplay target: view target unchanged"), S.PCM()->GetViewTarget() == S.GameplayViewTarget.Get());
		S.Test->TestTrue(TEXT("Cut to the gameplay target: not treated as a sequencer camera"), S.Phase() == EACFSequencerCameraPhase::None);
		S.CameraNear(GameplayLoc, 0.1, TEXT("Cut to the gameplay target: ACF stays on"));
		S.Test->TestFalse(TEXT("Cut to the gameplay target: not bypassed"), S.Acf->IsOutputBypassed());
		S.Test->TestEqual(TEXT("Cut to the gameplay target: no events"), S.Listener->NumStarted, 0);
		Player(S.SequenceB)->Stop();
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("After both: ACF camera"));
		S.Test->TestTrue(FString::Printf(TEXT("Both sequences: the screen never moved (max %.2f cm / frame)"), S.MaxFrameJump), S.MaxFrameJump < 0.1);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// テストマップ上のサンプル Level Sequence（README 9 章 / 14 章の手順）。描画ありならスクリーンショットを保存する。

namespace ACFSeqTest
{
	static void Exec(TArray<FStep>& Steps, const TCHAR* Command)
	{
		Steps.Add([Command](FScenario& S) { GEngine->Exec(S.World(), Command); return true; });
	}

	static void Shot(TArray<FStep>& Steps, const TCHAR* Name)
	{
		Steps.Add([Name](FScenario& S)
		{
			if (FApp::CanEverRender())
			{
				FScreenshotRequest::RequestScreenshot(Name, false, false);
			}
			return true;
		});
		WaitFrames(Steps, 3);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSampleSequencerTest, "ActionCameraFramework.Samples.SequencerOnTestMap", ACFSeqTest::Flags)
bool FACFSampleSequencerTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		if (!S.World()->GetMapName().Contains(TEXT("L_ACFCameraTest")))
		{
			S.Test->AddWarning(TEXT("Skipped: run on /ActionCameraFramework/Samples/L_ACFCameraTest."));
			return false;
		}
		if (!LoadObject<ULevelSequence>(nullptr, TEXT("/ActionCameraFramework/Samples/LS_ACFSample_Cut.LS_ACFSample_Cut"))
			|| !LoadObject<ULevelSequence>(nullptr, TEXT("/ActionCameraFramework/Samples/LS_ACFSample_Blend.LS_ACFSample_Blend")))
		{
			S.Test->AddError(TEXT("LS_ACFSample_Cut / LS_ACFSample_Blend missing: run Scripts/CreateManualTestAssets.py."));
			return false;
		}
		return true;
	});
	Exec(Steps, TEXT("ACF.Sample.Goto Start"));
	Exec(Steps, TEXT("ACF.Sample.Sequencer Reset"));
	Exec(Steps, TEXT("ACF.Sample.Use ThirdPerson 0"));
	WaitFrames(Steps, 30);
	Steps.Add([](FScenario& S)
	{
		S.SavedLocation = S.Camera(); // ゲームプレイカメラ（ThirdPerson）
		S.Handle = S.Acf->GetLastEvaluationResult().ActiveRequest;
		S.ResetSamples();
		return true;
	});
	// 1) Cut: カットで入り、Sequencer 内でカット、カットで終わる → ACF の Return Blend。
	Exec(Steps, TEXT("ACF.Sample.Sequencer Play Cut"));
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::InControl; });
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Cut: the sample camera is on screen (not the gameplay camera)"), FVector::Dist(S.Camera(), S.SavedLocation) > 300.0);
		S.Test->TestTrue(TEXT("Cut: ACF bypassed, gameplay request kept"), S.Acf->IsOutputBypassed() && S.Acf->IsActiveCamera(S.Handle));
		S.Test->AddInfo(FString::Printf(TEXT("Cut shot 1 camera %s"), *S.Camera().ToCompactString()));
		return true;
	});
	Shot(Steps, TEXT("ACF_5A_Cut_Shot1"));
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::None; });
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Cut: ended with the ACF return blend"), S.Listener->LastReturnKind == EACFSequencerReturnKind::AcfReturnBlend);
		S.Test->TestEqual(TEXT("Cut: exactly two cuts on screen (into shot 1, shot 1 -> shot 2), no other jump"), S.BigJumps, 2);
		S.Test->TestTrue(TEXT("Cut: return blend running"), S.Acf->GetLastEvaluationResult().bIsBlending);
		S.Value = FVector::Dist(S.Camera(), S.SavedLocation);
		S.ResetSamples();
		return true;
	});
	WaitFrames(Steps, 12);
	Shot(Steps, TEXT("ACF_5A_Cut_Return"));
	RetryUntil(Steps, [](FScenario& S) { return !S.Acf->GetLastEvaluationResult().bIsBlending; });
	Steps.Add([](FScenario& S)
	{
		S.LastCameraLocation = S.Camera();
		S.Test->TestTrue(FString::Printf(TEXT("Cut: back on the gameplay camera (%.1f cm away)"), FVector::Dist(S.Camera(), S.SavedLocation)), FVector::Dist(S.Camera(), S.SavedLocation) < 5.0);
		S.Test->TestTrue(FString::Printf(TEXT("Cut: return blend without a jump (max %.1f cm / frame over a %.0f cm return)"), S.MaxFrameJump, S.Value), S.MaxFrameJump < S.Value * 0.15);
		S.ResetSamples();
		return true;
	});
	// 2) Blend: Ease In / Out（PCM の ViewTarget Blend）。ACF は何も重ねない。
	Exec(Steps, TEXT("ACF.Sample.Sequencer Play Blend"));
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::BlendingIn; });
	WaitFrames(Steps, 25);
	Shot(Steps, TEXT("ACF_5A_Blend_EaseIn"));
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::None && !S.PCM()->PendingViewTarget.Target; });
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(FString::Printf(TEXT("Blend: phases None -> BlendingIn -> InControl -> BlendingOut -> None (%s)"), *S.PhaseHistory()),
			S.Phases.Contains(EACFSequencerCameraPhase::BlendingIn) && S.Phases.Contains(EACFSequencerCameraPhase::InControl) && S.Phases.Contains(EACFSequencerCameraPhase::BlendingOut));
		S.Test->TestTrue(TEXT("Blend: returned with the sequencer's own blend"), S.Listener->LastReturnKind == EACFSequencerReturnKind::SequencerBlend);
		S.Test->TestFalse(TEXT("Blend: ACF never blended on its own (no double blend)"), S.bAcfBlended);
		S.Test->TestEqual(TEXT("Blend: ACF weight never faded"), S.MinAcfWeight, 1.f);
		// 1 本のブレンドなら速度は滑らかに上下する（cubic）。二重ブレンドや段差・カットがあると 1 フレームで速度が跳ぶ。
		S.Test->TestTrue(FString::Printf(TEXT("Blend: smooth speed (peak %.1f cm / frame, largest change between frames %.2f cm)"), S.MaxFrameJump, S.MaxStepChange), S.MaxStepChange < 5.0);
		return true;
	});
	// 3) Lock-On 中に再生 → ロックオンのカメラへ戻る。
	Exec(Steps, TEXT("ACF.Sample.LockOn 0"));
	WaitFrames(Steps, 60);
	Steps.Add([](FScenario& S)
	{
		S.HandleB = S.Acf->GetLastEvaluationResult().ActiveRequest; // lock-on
		S.SavedLocation = S.Camera();
		return true;
	});
	Exec(Steps, TEXT("ACF.Sample.Sequencer Play Cut"));
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::InControl; });
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::None && !S.Acf->GetLastEvaluationResult().bIsBlending; });
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Lock-On: the lock-on request is in control again"), S.Acf->GetLastEvaluationResult().ActiveRequest == S.HandleB && S.HandleB != S.Handle);
		S.Test->TestTrue(FString::Printf(TEXT("Lock-On: lock-on camera restored (%.1f cm)"), FVector::Dist(S.Camera(), S.SavedLocation)), FVector::Dist(S.Camera(), S.SavedLocation) < 10.0);
		S.Test->TestEqual(TEXT("Every start has its end"), S.Listener->NumEnded, S.Listener->NumStarted);
		return true;
	});
	Shot(Steps, TEXT("ACF_5A_LockOnReturn"));
	Exec(Steps, TEXT("ACF.Sample.LockOff 0"));
	Exec(Steps, TEXT("ACF.Sample.Release 0"));
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// =================================================================================================
// Phase 6B / B-2: PlayerCameraManager の互換性。POV をコピーして Modifier に渡す派生 PCM（GameplayCameras など）では、
// ACF は POV のアドレスで ViewTarget を判定できない。最終的な画面の姿勢と、ACF が書き込んだかどうかを確認する。

namespace ACFSeqTest
{
	/** PCM を指定クラスで作り直す（後片付けで元のクラスに戻す）。 */
	static bool UseCameraManagerClass(FScenario& S, UClass* Class)
	{
		APlayerCameraManager* Old = S.PCM();
		if (!S.bSwappedCameraManager)
		{
			S.SavedCameraManagerClass = S.Controller->PlayerCameraManagerClass;
			S.bSwappedCameraManager = true;
		}
		S.Controller->PlayerCameraManagerClass = Class;
		S.Controller->SpawnPlayerCameraManager();
		if (Old && S.PCM() != Old)
		{
			Old->Destroy();
		}
		return S.Test->TestTrue(FString::Printf(TEXT("PCM recreated as %s"), *GetNameSafe(Class)), S.PCM() && S.PCM()->IsA(Class));
	}

	/** 点 P と線分 AB の距離、と線分上の位置（0..1）。 */
	static double DistanceToSegment(const FVector& P, const FVector& A, const FVector& B, double& OutAlpha)
	{
		const FVector AB = B - A;
		OutAlpha = FMath::Clamp(FVector::DotProduct(P - A, AB) / AB.SizeSquared(), 0.0, 1.0);
		return FVector::Dist(P, A + AB * OutAlpha);
	}

	/** Camera Cut（Ease なし）の再生中も Sequencer のカメラが映り、終了後は ACF の Return Blend で戻ること。 */
	static void AddCameraCutRoundTrip(TArray<FStep>& Steps, EACFViewTargetResolution Expected)
	{
		Steps.Add([](FScenario& S)
		{
			S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
			S.SequenceA = MakeSequence(S, S.ActorA.Get(), 1.0);
			SetReturnBlend(S, 0.5f);
			return S.Test->TestNotNull(TEXT("sequence actor created"), S.SequenceA.Get());
		});
		WaitFrames(Steps, 5);
		Steps.Add([Expected](FScenario& S)
		{
			S.CameraNear(GameplayLoc, 0.1, TEXT("Before the cut: ACF gameplay camera"));
			const FACFCameraOutputDiagnostics D = S.Acf->GetOutputDiagnostics();
			S.Test->TestTrue(FString::Printf(TEXT("Before the cut: view target resolved by %s"), *UEnum::GetValueAsString(D.LastResolution)), D.LastResolution == Expected);
			S.Test->TestTrue(FString::Printf(TEXT("Before the cut: ACF wrote every pass (%d of %d)"), D.Writes, D.Passes), D.Passes >= 1 && D.Writes == D.Passes);
			S.Test->TestEqual(TEXT("Before the cut: no unresolved pass"), D.UnresolvedPasses, 0);
			Player(S.SequenceA)->Play();
			return true;
		});
		RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::InControl; });
		Steps.Add([](FScenario& S) { S.ResetSamples(); return true; });
		WaitFrames(Steps, 20);
		Steps.Add([Expected](FScenario& S)
		{
			S.CameraNear(CineLoc, 0.1, TEXT("Camera Cut: the Sequencer camera is on screen (ACF does not overwrite it)"));
			S.Test->TestEqual(TEXT("Camera Cut: Sequencer FOV"), S.PCM()->GetFOVAngle(), CineFov, 0.01f);
			S.Test->TestTrue(TEXT("Camera Cut: ACF output bypassed"), S.Acf->IsOutputBypassed());
			S.Test->TestTrue(FString::Printf(TEXT("Camera Cut: ACF keeps evaluating in the background (lag %d)"), S.MaxEvaluationLag), S.MaxEvaluationLag <= 1);
			S.Test->TestTrue(TEXT("Camera Cut: ACF pose is the gameplay camera"), S.Acf->GetLastEvaluationResult().Pose.Location.Equals(GameplayLoc, 0.1));
			const FACFCameraOutputDiagnostics D = S.Acf->GetOutputDiagnostics();
			S.Test->TestTrue(FString::Printf(TEXT("Camera Cut: view target resolved by %s"), *UEnum::GetValueAsString(D.LastResolution)), D.LastResolution == Expected);
			S.Test->TestTrue(FString::Printf(TEXT("Camera Cut: ACF wrote nothing (%d writes, %d passes)"), D.Writes, D.Passes), D.Writes == 0);
			S.Test->TestEqual(TEXT("Camera Cut: no unresolved pass"), D.UnresolvedPasses, 0);
			return true;
		});
		RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::None; });
		Steps.Add([](FScenario& S)
		{
			S.Test->TestTrue(TEXT("Return: ACF return blend"), S.Listener->LastReturnKind == EACFSequencerReturnKind::AcfReturnBlend);
			S.CameraNear(CineLoc, GameplayToCine * 0.2, TEXT("Return, first frame: close to the Sequencer's last pose"));
			S.ResetSamples();
			return true;
		});
		RetryUntil(Steps, [](FScenario& S) { return !S.Acf->GetLastEvaluationResult().bIsBlending; });
		Steps.Add([](FScenario& S)
		{
			S.CameraNear(GameplayLoc, 0.1, TEXT("After the return blend: gameplay camera"));
			S.Test->TestTrue(FString::Printf(TEXT("Return: smooth (max %.1f cm / frame)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.2);
			return true;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPcmCompatCopyCutTest, "ActionCameraFramework.Sequencer.PcmCompat.CopyingPcmCameraCut", ACFSeqTest::Flags)
bool FACFPcmCompatCopyCutTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		if (!UseCameraManagerClass(S, AACFSequencerTestCameraManager::StaticClass()))
		{
			return false;
		}
		S.Handle = RequestGameplay(S);
		return true;
	});
	AddCameraCutRoundTrip(Steps, EACFViewTargetResolution::SingleViewTarget);
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPcmCompatSingleViewCutTest, "ActionCameraFramework.Sequencer.PcmCompat.SingleViewPcmCameraCut", ACFSeqTest::Flags)
bool FACFPcmCompatSingleViewCutTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		if (!UseCameraManagerClass(S, AACFSequencerTestCameraManager_Single::StaticClass()))
		{
			return false;
		}
		S.Handle = RequestGameplay(S);
		return true;
	});
	AddCameraCutRoundTrip(Steps, EACFViewTargetResolution::SingleViewTarget);
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPcmCompatGameplayCamerasTest, "ActionCameraFramework.Sequencer.PcmCompat.GameplayCamerasPcmCameraCut", ACFSeqTest::Flags)
bool FACFPcmCompatGameplayCamerasTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	// GameplayCameras プラグインには依存しない（クラスを名前で探す）。プラグインが無効なら何もしない。
	UClass* GameplayCamerasPcm = FindObject<UClass>(nullptr, TEXT("/Script/GameplayCameras.GameplayCamerasPlayerCameraManager"));
	if (!GameplayCamerasPcm)
	{
		AddWarning(TEXT("Skipped: GameplayCameras plugin is not loaded."));
		return true;
	}
	TArray<FStep> Steps;
	Steps.Add([GameplayCamerasPcm](FScenario& S)
	{
		if (!UseCameraManagerClass(S, GameplayCamerasPcm))
		{
			return false;
		}
		S.Handle = RequestGameplay(S);
		return true;
	});
	AddCameraCutRoundTrip(Steps, EACFViewTargetResolution::SingleViewTarget);
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPcmCompatCopyBlendTest, "ActionCameraFramework.Sequencer.PcmCompat.CopyingPcmViewTargetBlend", ACFSeqTest::Flags)
bool FACFPcmCompatCopyBlendTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	// 判定できないブレンドの警告は PCM ごとに 1 回だけ（毎フレームではない）。
	AddExpectedMessagePlain(TEXT("cannot tell which view target the modifier pass belongs to"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		if (!UseCameraManagerClass(S, AACFSequencerTestCameraManager::StaticClass()))
		{
			return false;
		}
		S.Handle = RequestGameplay(S);
		S.GameplayViewTarget = S.PCM()->GetViewTarget();
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot); // 外部 ViewTarget
		S.ActorB = SpawnPoint(S, CineLocB);               // 登録しない普通の ViewTarget
		S.Acf->AddExternalViewTarget(S.ActorA.Get(), S.Listener.Get());
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("Single view target: ACF camera"));
		S.ResetSamples();
		S.Controller->SetViewTargetWithBlend(S.ActorB.Get(), 1.f, VTBlend_Linear); // 外部でない ViewTarget 同士のブレンド
		return true;
	});
	WaitFrames(Steps, 20);
	Steps.Add([](FScenario& S)
	{
		const AACFSequencerTestCameraManager* M = Cast<AACFSequencerTestCameraManager>(S.PCM());
		S.Test->TestTrue(TEXT("Gameplay -> gameplay blend: two modifier passes"), M && M->PassesThisFrame == 2);
		S.CameraNear(GameplayLoc, 0.1, TEXT("Gameplay -> gameplay blend: ACF on both passes (same as the standard PCM)"));
		const FACFCameraOutputDiagnostics D = S.Acf->GetOutputDiagnostics();
		S.Test->TestTrue(TEXT("Gameplay -> gameplay blend: SameDecision"), D.LastResolution == EACFViewTargetResolution::SameDecision);
		S.Test->TestTrue(FString::Printf(TEXT("Gameplay -> gameplay blend: ACF wrote both passes (%d of %d)"), D.Writes, D.Passes), D.Passes == 2 && D.Writes == 2);
		S.Test->TestEqual(TEXT("Gameplay -> gameplay blend: nothing unresolved"), D.UnresolvedPasses, 0);
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.PCM()->PendingViewTarget.Target == nullptr; });
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("After the gameplay blend: ACF camera"));
		S.Controller->SetViewTargetWithBlend(S.ActorA.Get(), 1.f, VTBlend_Linear); // 外部 ViewTarget へのブレンド
		return true;
	});
	WaitFrames(Steps, 20);
	Steps.Add([](FScenario& S)
	{
		// 2 回の Modifier 呼び出しのどちらが外部 ViewTarget のものか分からない。ACF は外部カメラを上書きしないよう、
		// どちらにも書かない（ゲームプレイ側は UE のカメラ = ActorB の位置のまま）。画面は ActorB と外部カメラの間。
		double Alpha = 0.0;
		const double Off = DistanceToSegment(S.Camera(), CineLocB, CineLoc, Alpha);
		S.Test->TestTrue(FString::Printf(TEXT("Gameplay -> external blend (ambiguous): screen on the UE blend line, not the ACF camera (off %.2f cm, alpha %.2f)"), Off, Alpha),
			Off < 1.0 && Alpha > 0.05 && Alpha < 0.95);
		S.Test->TestFalse(TEXT("Ambiguous: the camera is not the ACF camera"), S.Camera().Equals(GameplayLoc, 1.0));
		S.Test->TestEqual(TEXT("Ambiguous: ACF weight untouched"), S.Acf->GetLastEvaluationResult().Weight, 1.f);
		const FACFCameraOutputDiagnostics D = S.Acf->GetOutputDiagnostics();
		S.Test->TestTrue(TEXT("Ambiguous: reported as Unresolved"), D.LastResolution == EACFViewTargetResolution::Unresolved);
		S.Test->TestTrue(FString::Printf(TEXT("Ambiguous: ACF wrote no pass (%d writes, %d passes)"), D.Writes, D.Passes), D.Passes == 2 && D.Writes == 0);
		S.Test->TestTrue(FString::Printf(TEXT("Ambiguous: unresolved passes counted (%d)"), D.UnresolvedPasses), D.UnresolvedPasses >= 2);
		S.Test->TestTrue(TEXT("Ambiguous: the POV copy is reported"), D.bPOVCopied);
		S.Test->TestTrue(TEXT("Ambiguous: ACF keeps evaluating in the background"), S.Acf->IsOutputBypassed() && S.Acf->IsActiveCamera(S.Handle));
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.PCM()->PendingViewTarget.Target == nullptr; });
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(CineLoc, 0.1, TEXT("External view target only: its own camera (ACF does not overwrite it)"));
		S.Test->TestTrue(TEXT("External view target only: ACF bypassed"), S.Acf->IsOutputBypassed());
		S.Test->TestTrue(TEXT("External view target only: SingleViewTarget, nothing written"), S.Acf->GetOutputDiagnostics().LastResolution == EACFViewTargetResolution::SingleViewTarget && S.Acf->GetOutputDiagnostics().Writes == 0);
		S.Acf->RemoveExternalViewTarget(S.ActorA.Get(), S.Listener.Get());
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("Registration removed: ACF camera again"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

namespace ACFSeqTest
{
	/**
	 * ViewTarget が正確に分かる PCM（標準 PCM / 連携 API を使う派生 PCM）: ゲームプレイ → 外部カメラへの SetViewTargetWithBlend で、
	 * ゲームプレイ側の呼び出しには ACF が 100%、外部カメラ側には 0%。画面は ACF カメラと外部カメラの間（ブレンドは PCM の 1 本だけ）。
	 */
	static void AddExactBlendIntoExternal(TArray<FStep>& Steps, EACFViewTargetResolution Expected)
	{
		Steps.Add([](FScenario& S)
		{
			S.Handle = RequestGameplay(S);
			S.GameplayViewTarget = S.PCM()->GetViewTarget();
			S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
			S.Acf->AddExternalViewTarget(S.ActorA.Get(), S.Listener.Get());
			return true;
		});
		WaitFrames(Steps, 5);
		Steps.Add([](FScenario& S)
		{
			S.CameraNear(GameplayLoc, 0.1, TEXT("Before the blend: ACF camera"));
			S.ResetSamples();
			S.Controller->SetViewTargetWithBlend(S.ActorA.Get(), 1.f, VTBlend_Linear);
			return true;
		});
		WaitFrames(Steps, 20);
		Steps.Add([Expected](FScenario& S)
		{
			double Alpha = 0.0;
			const double Off = DistanceToSegment(S.Camera(), GameplayLoc, CineLoc, Alpha);
			S.Test->TestTrue(FString::Printf(TEXT("Blend: screen between the ACF camera and the external camera (off %.2f cm, alpha %.2f)"), Off, Alpha), Off < 1.0 && Alpha > 0.05 && Alpha < 0.95);
			const FACFCameraOutputDiagnostics D = S.Acf->GetOutputDiagnostics();
			S.Test->TestTrue(FString::Printf(TEXT("Blend: view target resolved by %s"), *UEnum::GetValueAsString(D.LastResolution)), D.LastResolution == Expected);
			S.Test->TestTrue(FString::Printf(TEXT("Blend: ACF wrote the gameplay pass only (%d writes, %d passes)"), D.Writes, D.Passes), D.Passes == 2 && D.Writes == 1);
			S.Test->TestEqual(TEXT("Blend: nothing unresolved"), D.UnresolvedPasses, 0);
			S.Test->TestEqual(TEXT("Blend: ACF weight untouched (single PCM blend)"), S.MinAcfWeight, 1.f);
			S.Test->TestFalse(TEXT("Blend: ACF does not blend on its own"), S.bAcfBlended);
			return true;
		});
		RetryUntil(Steps, [](FScenario& S) { return S.PCM()->PendingViewTarget.Target == nullptr; });
		Steps.Add([](FScenario& S)
		{
			S.CameraNear(CineLoc, 0.1, TEXT("After the blend: the external camera"));
			S.Test->TestTrue(FString::Printf(TEXT("Blend: smooth (max %.1f cm / frame)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.05);
			S.Test->TestEqual(TEXT("After the blend: ACF wrote nothing"), S.Acf->GetOutputDiagnostics().Writes, 0);
			S.Acf->RemoveExternalViewTarget(S.ActorA.Get(), S.Listener.Get());
			return true;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPcmCompatExplicitTest, "ActionCameraFramework.Sequencer.PcmCompat.ExplicitViewTargetApi", ACFSeqTest::Flags)
bool FACFPcmCompatExplicitTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S) { return UseCameraManagerClass(S, AACFSequencerTestCameraManager_Explicit::StaticClass()); });
	AddExactBlendIntoExternal(Steps, EACFViewTargetResolution::Explicit);
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPcmCompatStandardTest, "ActionCameraFramework.Sequencer.PcmCompat.StandardPcmUnchanged", ACFSeqTest::Flags)
bool FACFPcmCompatStandardTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	AddExactBlendIntoExternal(Steps, EACFViewTargetResolution::POVAddress);
	Steps.Add([](FScenario& S)
	{
		S.Controller->SetViewTarget(S.GameplayViewTarget.Get());
		S.Acf->ReleaseAllCameras(true);
		S.Handle = RequestGameplay(S);
		S.Test->TestFalse(TEXT("Standard PCM: the POV is never a copy"), S.Acf->GetOutputDiagnostics().bPOVCopied);
		return true;
	});
	AddCameraCutRoundTrip(Steps, EACFViewTargetResolution::POVAddress);
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPcmCompatLocalPlayersTest, "ActionCameraFramework.Sequencer.PcmCompat.LocalPlayersKeepTheirOwnDiagnostics", ACFSeqTest::Flags)
bool FACFPcmCompatLocalPlayersTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	// 2 人目の PlayerController の BeginPlay でホストプロジェクトの HUD が出すエラー（LocalPlayersAreIndependent と同じ。ACF とは無関係）。
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this); // B-9: only for a host Blueprint PlayerController
	static const FVector SecondLoc = Base + FVector(0.f, 8000.f, 0.f);
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		// 1 人目: POV をコピーする PCM。2 人目: 標準 PCM。
		if (!UseCameraManagerClass(S, AACFSequencerTestCameraManager::StaticClass()))
		{
			return false;
		}
		S.Handle = RequestGameplay(S);
		FString Error;
		S.SecondPlayer = S.World()->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(1), Error, true);
		return S.Test->TestTrue(FString::Printf(TEXT("second local player created %s"), *Error), S.SecondPlayer.IsValid());
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		UACFCameraSubsystem* Acf2 = S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>();
		if (!S.Test->TestNotNull(TEXT("second ACF subsystem"), Acf2))
		{
			return false;
		}
		S.HandleB = Acf2->RequestCamera(MakeFixedPreset(S, SecondLoc, FRotator(0.f, 180.f, 0.f), 70.f), FACFCameraRequestParams());
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 30.0); // Owner なし → 1 人目
		Player(S.SequenceA)->Play();
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::InControl; });
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		const APlayerController* PC2 = S.SecondPlayer->GetPlayerController(S.World());
		const UACFCameraSubsystem* Acf2 = S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>();
		S.CameraNear(CineLoc, 0.1, TEXT("Player 1 (POV-copying PCM): Sequencer camera"));
		const FACFCameraOutputDiagnostics D1 = S.Acf->GetOutputDiagnostics();
		S.Test->TestTrue(TEXT("Player 1: SingleViewTarget, nothing written"), D1.LastResolution == EACFViewTargetResolution::SingleViewTarget && D1.Writes == 0 && D1.bPOVCopied);
		const FACFCameraOutputDiagnostics D2 = Acf2->GetOutputDiagnostics();
		S.Test->TestTrue(FString::Printf(TEXT("Player 2 (standard PCM): own diagnostics (%s, %s)"), *D2.CameraManagerClass.ToString(), *UEnum::GetValueAsString(D2.LastResolution)),
			D2.LastResolution == EACFViewTargetResolution::POVAddress && !D2.bPOVCopied && D2.UnresolvedPasses == 0);
		S.Test->TestTrue(FString::Printf(TEXT("Player 2: ACF wrote every pass (%d of %d)"), D2.Writes, D2.Passes), D2.Passes >= 1 && D2.Writes == D2.Passes);
		S.Test->TestTrue(TEXT("Player 2: its ACF camera on screen"), PC2 && PC2->PlayerCameraManager && PC2->PlayerCameraManager->GetCameraLocation().Equals(SecondLoc, 0.1));
		S.Test->TestFalse(TEXT("Player 2: not bypassed"), Acf2->IsOutputBypassed());
		Player(S.SequenceA)->Stop();
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::None && !S.Acf->GetLastEvaluationResult().bIsBlending; });
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("Player 1 back on the ACF camera"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// =================================================================================================
// Phase 6B / B-4: 不正値に対する出力の健全性を実際の PCM 上で確認する（PCM の最終出力、ローカルプレイヤー間、外部 ViewTarget）。

namespace ACFSeqTest
{
	static bool IsFiniteCamera(const APlayerCameraManager* M)
	{
		const FMinimalViewInfo& View = M->GetCameraCacheView();
		return ACFCameraValidation::IsFinite(View.Location) && ACFCameraValidation::IsFinite(View.Rotation) && ACFCameraValidation::IsValidFov(View.FOV);
	}

	// The target is a bare (unregistered) scene component: writing NaN into a registered component trips the engine's own
	// ensure (SceneComponent.cpp, NewTransform.IsValid()), which is the engine's diagnostic, not ACF's.
	static void ForceTargetTransform(USceneComponent* Component, const FVector& Location)
	{
		FTransform Transform;
		Transform.SetLocation(Location);
		Component->SetComponentToWorld(Transform);
	}
	static void ForceNaNTransform(USceneComponent* Component)
	{
		const double NaN = std::numeric_limits<double>::quiet_NaN();
		ForceTargetTransform(Component, FVector(NaN, NaN, NaN));
	}

	static USceneComponent* HealthTarget(FScenario& S)
	{
		return Cast<USceneComponent>(S.Keep[0].Get()); // the target is the first object the scenario keeps
	}

	static UACFCameraPreset* MakeTrackingPreset(FScenario& S)
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_Follow* Follow = NewObject<UACFCameraNode_Follow>(Preset);
		Follow->Offset = FVector(-300.0, 0.0, 100.0);
		Follow->OffsetSpace = EACFCameraOffsetSpace::World;
		Follow->Damping = FACFCameraPositionDamping::Uniform(0.2f);
		Preset->Nodes.Add(Follow);
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		S.Keep.Emplace(Preset);
		return Preset;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFHealthPcmOutputTest, "ActionCameraFramework.Sequencer.Health.PcmOutputStaysFiniteAndPlayersStaySeparate", ACFSeqTest::Flags)
bool FACFHealthPcmOutputTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	// 2 人目の PlayerController の BeginPlay でホストプロジェクトの HUD が出すエラー（ACF とは無関係。LocalPlayersAreIndependent と同じ）。
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this); // B-9: only for a host Blueprint PlayerController
	static const FVector SecondLoc = Base + FVector(0.f, 8000.f, 0.f);
	static const FVector TargetLoc = Base + FVector(500.f, 0.f, 0.f);
	static const FVector NewTargetLoc = Base + FVector(900.f, 300.f, 0.f);
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		USceneComponent* Target = NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		S.Keep.Emplace(Target);
		ForceTargetTransform(Target, TargetLoc);
		S.Value = 0.0;
		FACFCameraRequestParams Params;
		Params.FollowTarget.Component = Target;
		S.Handle = S.Acf->RequestCamera(MakeTrackingPreset(S), Params);
		FString Error;
		S.SecondPlayer = S.World()->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(1), Error, true);
		return S.Test->TestTrue(FString::Printf(TEXT("second local player created %s"), *Error), S.SecondPlayer.IsValid());
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		UACFCameraSubsystem* Acf2 = S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>();
		if (!S.Test->TestNotNull(TEXT("second ACF subsystem"), Acf2))
		{
			return false;
		}
		S.HandleB = Acf2->RequestCamera(MakeFixedPreset(S, SecondLoc, FRotator(0.f, 180.f, 0.f), 70.f), FACFCameraRequestParams());
		return true;
	});
	WaitFrames(Steps, 30);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(TargetLoc + FVector(-300.0, 0.0, 100.0), 1.0, TEXT("Before: following the target"));
		S.Counter = 0;
		S.Value = 0.0; // frames with a non-finite camera
		ForceNaNTransform(HealthTarget(S));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		// The previous frame's camera update read the NaN target.
		S.Value += IsFiniteCamera(S.PCM()) ? 0.0 : 1.0;
		const APlayerController* PC2 = S.SecondPlayer->GetPlayerController(S.World());
		S.Value += (PC2 && PC2->PlayerCameraManager && IsFiniteCamera(PC2->PlayerCameraManager)) ? 0.0 : 1.0;
		if (++S.Counter < 15)
		{
			ForceNaNTransform(HealthTarget(S));
			S.bRetry = true;
		}
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("NaN target: the PCM camera of both players stayed finite with a valid FOV"), S.Value, 0.0);
		S.CameraNear(TargetLoc + FVector(-300.0, 0.0, 100.0), 1.0, TEXT("NaN target: the camera holds the last valid target"));
		const FACFCameraHealthDiagnostics D1 = S.Acf->GetHealthDiagnostics();
		const FACFCameraHealthDiagnostics D2 = S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>()->GetHealthDiagnostics();
		S.Test->TestTrue(FString::Printf(TEXT("Player 1: invalid target reported (%d, last: %s)"), D1.InvalidTargets, *D1.LastIssue), D1.InvalidTargets >= 15);
		S.Test->TestEqual(TEXT("Player 2: nothing reported (diagnostics are per local player)"), D2.TotalIssues, 0);
		const APlayerController* PC2 = S.SecondPlayer->GetPlayerController(S.World());
		S.Test->TestTrue(TEXT("Player 2: its own camera unaffected"), PC2 && PC2->PlayerCameraManager->GetCameraLocation().Equals(SecondLoc, 0.1));
		ForceTargetTransform(HealthTarget(S), NewTargetLoc); // valid again
		return true;
	});
	WaitFrames(Steps, 120);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(NewTargetLoc + FVector(-300.0, 0.0, 100.0), 1.0, TEXT("Valid again: the camera recovered (damping not stuck)"));
		S.Test->TestTrue(TEXT("Valid again: the request is still active (an invalid transform is not a lost target)"), S.Acf->IsActiveCamera(S.Handle));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFHealthExternalViewTargetTest, "ActionCameraFramework.Sequencer.Health.ExternalViewTargetUntouched", ACFSeqTest::Flags)
bool FACFHealthExternalViewTargetTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		// ACF in a broken state: its camera has a NaN FOV and a NaN location (both dropped by ACF).
		const double NaN = std::numeric_limits<double>::quiet_NaN();
		S.Handle = S.Acf->RequestCamera(MakeFixedPreset(S, FVector(NaN, 0.0, 0.0), GameplayRot, static_cast<float>(NaN)), FACFCameraRequestParams());
		S.GameplayViewTarget = S.PCM()->GetViewTarget();
		// External camera with an extreme but valid FOV: ACF must not "correct" it.
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot, 175.f);
		S.Acf->AddExternalViewTarget(S.ActorA.Get(), S.Listener.Get());
		S.Controller->SetViewTarget(S.ActorA.Get());
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(CineLoc, 0.001, TEXT("External view target: its own location, unchanged"));
		S.Test->TestEqual(TEXT("External view target: its own FOV (175), not clamped by ACF"), S.PCM()->GetFOVAngle(), 175.f);
		S.Test->TestEqual(TEXT("External view target: ACF wrote nothing"), S.Acf->GetOutputDiagnostics().Writes, 0);
		S.Test->TestTrue(TEXT("ACF still evaluates in the background with a valid pose"), S.Acf->IsOutputBypassed()
			&& ACFCameraValidation::IsValidPose(S.Acf->GetLastEvaluationResult().Pose));
		S.Acf->RemoveExternalViewTarget(S.ActorA.Get(), S.Listener.Get());
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// =================================================================================================
// Phase 6B / B-5: 有効な要求が無いときの演出。過去の演出が後から再生されないこと、Handle の状態、実際の画面の姿勢。
// 演出は Punch（カメラ空間で上に 100 cm、Attack 0 / Sustain / Release 0）: 画面の Z の差で効いているかが分かる。

namespace ACFSeqTest
{
	static UACFCameraEffectPreset* MakeLiftEffect(FScenario& S, float Seconds)
	{
		UACFCameraEffectPreset* Preset = NewObject<UACFCameraEffectPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraEffect_Punch* Punch = NewObject<UACFCameraEffect_Punch>(Preset);
		Punch->Location = FVector(0.0, 0.0, 100.0);
		Punch->Envelope.Attack = 0.f;
		Punch->Envelope.Sustain = Seconds;
		Punch->Envelope.Release = 0.f;
		Preset->Layers.Add(Punch);
		S.Keep.Emplace(Preset);
		return Preset;
	}

	static int32 NumEffects(const UACFCameraSubsystem* Acf) { return Acf->GetActiveCameraEffects().Num(); }

	static float EffectTime(const UACFCameraSubsystem* Acf, FACFCameraEffectHandle Handle)
	{
		for (const FACFCameraEffectInfo& Info : Acf->GetActiveCameraEffects())
		{
			if (Info.Handle == Handle)
			{
				return Info.Time;
			}
		}
		return -1.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectsNoRequestTest, "ActionCameraFramework.Sequencer.Effects.WithoutRequestNotPlayedLater", ACFSeqTest::Flags)
bool FACFEffectsNoRequestTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this); // B-9: only for a host Blueprint PlayerController
	static const FVector SecondLoc = Base + FVector(0.f, 8000.f, 0.f);
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		FString Error;
		// A fresh local player: its ACF has never had a camera request (nothing attached to its PCM).
		S.SecondPlayer = S.World()->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(1), Error, true);
		return S.Test->TestTrue(FString::Printf(TEXT("second local player %s"), *Error), S.SecondPlayer.IsValid());
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		UACFCameraSubsystem* Acf2 = S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>();
		const FACFCameraEffectHandle Effect = Acf2->PlayCameraEffect(MakeLiftEffect(S, 2.f), FACFCameraEffectParams());
		S.Test->TestFalse(TEXT("No request: the effect is rejected (invalid handle)"), Effect.IsValid());
		S.Test->TestFalse(TEXT("No request: the handle is not active"), Acf2->IsCameraEffectActive(Effect));
		S.Test->TestEqual(TEXT("No request: nothing queued"), NumEffects(Acf2), 0);
		S.Test->TestFalse(TEXT("No request: Can Play ACF Camera Effects is false"), Acf2->CanPlayCameraEffects());
		S.Test->TestEqual(TEXT("No request: the rejection is counted"), Acf2->GetNumRejectedCameraEffects(), 1);
		S.Test->TestFalse(TEXT("Blueprint: Is Valid (ACF Camera Effect Handle) is false"), UACFCameraBlueprintLibrary::IsValidEffectHandle(Effect));
		S.Test->TestTrue(TEXT("Player 1 (with a request) can play effects"), S.Acf->CanPlayCameraEffects());
		S.Test->TestEqual(TEXT("Player 1 has no effect"), NumEffects(S.Acf.Get()), 0);
		return true;
	});
	WaitFrames(Steps, 180); // three seconds later the game requests a camera for player 2
	Steps.Add([](FScenario& S)
	{
		UACFCameraSubsystem* Acf2 = S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>();
		S.Test->TestEqual(TEXT("3 s later: still nothing queued"), NumEffects(Acf2), 0);
		S.HandleB = Acf2->RequestCamera(MakeFixedPreset(S, SecondLoc, FRotator(0.f, 180.f, 0.f), 70.f), FACFCameraRequestParams());
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		const APlayerController* PC2 = S.SecondPlayer->GetPlayerController(S.World());
		UACFCameraSubsystem* Acf2 = S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>();
		S.Test->TestEqual(TEXT("After the request: the old effect is not played"), NumEffects(Acf2), 0);
		S.Test->TestTrue(FString::Printf(TEXT("After the request: camera exactly on the preset (%s)"), *PC2->PlayerCameraManager->GetCameraLocation().ToString()),
			PC2->PlayerCameraManager->GetCameraLocation().Equals(SecondLoc, 0.01));
		// A new effect on the new request plays normally.
		const FACFCameraEffectHandle Effect = Acf2->PlayCameraEffect(MakeLiftEffect(S, 0.5f), FACFCameraEffectParams());
		S.Test->TestTrue(TEXT("With a request: a new effect is accepted"), Effect.IsValid());
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		const APlayerController* PC2 = S.SecondPlayer->GetPlayerController(S.World());
		S.Test->TestTrue(TEXT("New effect visible on player 2 (+100 cm)"), PC2->PlayerCameraManager->GetCameraLocation().Equals(SecondLoc + FVector(0.0, 0.0, 100.0), 0.5));
		S.CameraNear(GameplayLoc, 0.01, TEXT("Player 1 is not affected by the effect of player 2"));
		return true;
	});
	WaitFrames(Steps, 60);
	Steps.Add([](FScenario& S)
	{
		const APlayerController* PC2 = S.SecondPlayer->GetPlayerController(S.World());
		S.Test->TestEqual(TEXT("New effect ended"), NumEffects(S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>()), 0);
		S.Test->TestTrue(TEXT("Back on the preset"), PC2->PlayerCameraManager->GetCameraLocation().Equals(SecondLoc, 0.01));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectsReleaseSwitchTest, "ActionCameraFramework.Sequencer.Effects.AfterReleaseAndDuringSwitch", ACFSeqTest::Flags)
bool FACFEffectsReleaseSwitchTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S) { S.Handle = RequestGameplay(S); return true; });
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.Acf->ReleaseCamera(S.Handle); // the preset blends out with a cut // cut back to the UE camera
		const FACFCameraEffectHandle Effect = S.Acf->PlayCameraEffect(MakeLiftEffect(S, 2.f), FACFCameraEffectParams());
		S.Test->TestFalse(TEXT("Right after the last request was released (cut): rejected"), Effect.IsValid());
		S.Test->TestEqual(TEXT("Nothing queued"), NumEffects(S.Acf.Get()), 0);
		return true;
	});
	WaitFrames(Steps, 30);
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.01, TEXT("New request: no old effect"));
		// Switch to another camera with a 1 s blend and play an effect in the middle of it.
		UACFCameraPreset* Other = MakeFixedPreset(S, FVector(CineLoc.X, CineLoc.Y, GameplayLoc.Z), GameplayRot, GameplayFov); // same height: blended Z stays GameplayLoc.Z
		Other->DefaultBlendIn.Curve = EACFCameraBlendCurve::Linear;
		Other->DefaultBlendIn.Duration = 1.f;
		FACFCameraRequestParams Params;
		Params.Priority = 10;
		S.HandleB = S.Acf->RequestCamera(Other, Params);
		return true;
	});
	WaitFrames(Steps, 20);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Switching"), S.Acf->GetLastEvaluationResult().bIsBlending);
		const FACFCameraEffectHandle Effect = S.Acf->PlayCameraEffect(MakeLiftEffect(S, 0.25f), FACFCameraEffectParams());
		S.Test->TestTrue(TEXT("During a request switch: accepted"), Effect.IsValid() && S.Acf->IsCameraEffectActive(Effect));
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const double Lift = S.Camera().Z - GameplayLoc.Z;
		S.Test->TestTrue(FString::Printf(TEXT("During the switch: the effect is on screen (+%.1f cm over the blended camera)"), Lift), FMath::IsNearlyEqual(Lift, 100.0, 1.0));
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return !S.Acf->GetLastEvaluationResult().bIsBlending && NumEffects(S.Acf.Get()) == 0; });
	WaitFrames(Steps, 2);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(FVector(CineLoc.X, CineLoc.Y, GameplayLoc.Z), 0.01, TEXT("After the switch: the effect ended, the new camera exactly"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectsSequencerTest, "ActionCameraFramework.Sequencer.Effects.WhileSequencerCameraIsShown", ACFSeqTest::Flags)
bool FACFEffectsSequencerTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 1.5);
		SetReturnBlend(S, 0.f);
		Player(S.SequenceA)->Play();
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::InControl; });
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		const FACFCameraEffectHandle Effect = S.Acf->PlayCameraEffect(MakeLiftEffect(S, 0.4f), FACFCameraEffectParams());
		S.Test->TestTrue(TEXT("Sequencer camera on screen (request alive in the background): accepted"), Effect.IsValid());
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(CineLoc, 0.001, TEXT("The Sequencer camera is not moved by the effect"));
		S.Test->TestTrue(TEXT("The effect advances in the background"), NumEffects(S.Acf.Get()) == 1 && S.Acf->GetActiveCameraEffects()[0].Time > 0.1f);
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::None; });
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("After the sequence: the effect has run its course (not replayed)"), NumEffects(S.Acf.Get()), 0);
		S.CameraNear(GameplayLoc, 0.01, TEXT("After the sequence: gameplay camera exactly"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectsPcmRecreatedTest, "ActionCameraFramework.Sequencer.Effects.RunningEffectWithoutRequestAndPcmRecreated", ACFSeqTest::Flags)
bool FACFEffectsPcmRecreatedTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S) { S.Handle = RequestGameplay(S); return true; });
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		const FACFCameraEffectHandle Effect = S.Acf->PlayCameraEffect(MakeLiftEffect(S, 2.f), FACFCameraEffectParams());
		S.Value = Effect.GetId();
		S.Test->TestTrue(TEXT("Accepted with a request"), Effect.IsValid());
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.Acf->ReleaseCamera(S.Handle); // the preset blends out with a cut
		// The PCM is recreated while the effect (2 s) still runs and no request exists.
		APlayerCameraManager* Old = S.PCM();
		S.Controller->SpawnPlayerCameraManager();
		if (S.PCM() != Old)
		{
			Old->Destroy();
		}
		return true;
	});
	WaitFrames(Steps, 30);
	Steps.Add([](FScenario& S)
	{
		const float Time = EffectTime(S.Acf.Get(), FACFCameraEffectHandle(static_cast<int32>(S.Value)));
		S.Test->TestTrue(FString::Printf(TEXT("Without a request the running effect keeps advancing (time %.2f s)"), Time), Time > 0.5f);
		return true;
	});
	WaitFrames(Steps, 150);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("It ended on its own"), NumEffects(S.Acf.Get()), 0);
		S.Handle = RequestGameplay(S);
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.01, TEXT("New request: the old effect is not resumed"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectsPauseTest, "ActionCameraFramework.Sequencer.Effects.RequestedWhilePaused", ACFSeqTest::Flags)
bool FACFEffectsPauseTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S) { S.Handle = RequestGameplay(S); return true; });
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		UGameplayStatics::SetGamePaused(S.World(), true);
		const FACFCameraEffectHandle Effect = S.Acf->PlayCameraEffect(MakeLiftEffect(S, 0.25f), FACFCameraEffectParams());
		S.Value = Effect.GetId();
		S.Test->TestTrue(TEXT("Paused with a request: accepted"), Effect.IsValid());
		return true;
	});
	WaitFrames(Steps, 30);
	Steps.Add([](FScenario& S)
	{
		const float Time = EffectTime(S.Acf.Get(), FACFCameraEffectHandle(static_cast<int32>(S.Value)));
		S.Test->TestEqual(TEXT("Paused: the effect time does not advance (existing pause behavior)"), Time, 0.f);
		UGameplayStatics::SetGamePaused(S.World(), false);
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		const float Time = EffectTime(S.Acf.Get(), FACFCameraEffectHandle(static_cast<int32>(S.Value)));
		S.Test->TestTrue(FString::Printf(TEXT("Unpaused: the effect starts (time %.3f)"), Time), Time > 0.f);
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return NumEffects(S.Acf.Get()) == 0; });
	WaitFrames(Steps, 2);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.01, TEXT("Ended: gameplay camera exactly"));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// =================================================================================================
// Phase 6B / B-6: Sequencer の開始・終了通知（Blueprint の Dynamic Delegate）の配信タイミングと再入。
// テスト用 PCM（標準の流れ）で、カメラ更新の最初に任意の処理を差し込む（「ACF の Tick の後に、別のシステムが Sequence を止めた」）。

namespace ACFSeqTest
{
	static AACFSequencerTestCameraManager* TestPCM(FScenario& S) { return Cast<AACFSequencerTestCameraManager>(S.PCM()); }

	static FString EventOrder(const UACFSequencerTestListener* Listener)
	{
		FString Out;
		for (const UACFSequencerTestListener::FEvent& Event : Listener->Events)
		{
			Out += Event.bStarted ? TEXT("S") : TEXT("E");
		}
		return Out;
	}

	/** Standard test PCM, gameplay request, a 30 s Camera Cut sequence playing and in control. */
	static void AddPlayingSequence(TArray<FStep>& Steps, float ReturnBlend)
	{
		Steps.Add([ReturnBlend](FScenario& S)
		{
			if (!UseCameraManagerClass(S, AACFSequencerTestCameraManager_Standard::StaticClass()))
			{
				return false;
			}
			S.Handle = RequestGameplay(S);
			S.GameplayViewTarget = S.PCM()->GetViewTarget();
			S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
			S.SequenceA = MakeSequence(S, S.ActorA.Get(), 30.0);
			SetReturnBlend(S, ReturnBlend);
			return true;
		});
		WaitFrames(Steps, 3);
		Steps.Add([](FScenario& S) { Player(S.SequenceA)->Play(); return true; });
		RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::InControl; });
		WaitFrames(Steps, 5);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEventsStoppedBeforeCameraUpdateTest, "ActionCameraFramework.Sequencer.Events.StoppedBeforeCameraUpdate", ACFSeqTest::Flags)
bool FACFEventsStoppedBeforeCameraUpdateTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	AddPlayingSequence(Steps, 0.25f);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Started once"), S.Listener->NumStarted, 1);
		// Stopped inside the camera update of this frame, after the ACF sequencer tick: only the camera-update path sees it.
		TestPCM(S)->PreUpdate = [&S]() { S.MarkFrame = GFrameCounter; Player(S.SequenceA)->Stop(); };
		S.ResetSamples();
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Phase() == EACFSequencerCameraPhase::None && S.Listener->NumEnded > 0; });
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Ended once"), S.Listener->NumEnded, 1);
		S.Test->TestEqual(TEXT("Order: start, end"), EventOrder(S.Listener.Get()), FString(TEXT("SE")));
		S.Test->TestEqual(TEXT("No notification is delivered while the PCM updates the camera"), S.Listener->DeliveriesDuringCameraUpdate, 0);
		S.Test->TestTrue(TEXT("Return: ACF return blend (unchanged)"), S.Listener->LastReturnKind == EACFSequencerReturnKind::AcfReturnBlend);
		S.Test->TestEqual(TEXT("Timing: delivered in the frame the sequence stopped (after its camera update)"), S.Listener->Events.Last().Frame, S.MarkFrame);
		S.Test->TestEqual(TEXT("Nothing left pending"), S.Sequencer->GetNumPendingEvents(), 0);
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return !S.Acf->GetLastEvaluationResult().bIsBlending; });
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.01, TEXT("Back on the gameplay camera"));
		S.Test->TestTrue(FString::Printf(TEXT("Return blend smooth (max %.1f cm / frame)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine * 0.2);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEventsHandlerViewTargetTest, "ActionCameraFramework.Sequencer.Events.HandlerChangesViewTarget", ACFSeqTest::Flags)
bool FACFEventsHandlerViewTargetTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	AddPlayingSequence(Steps, 0.f);
	Steps.Add([](FScenario& S)
	{
		S.ActorB = SpawnPoint(S, CineLocB);
		// Blueprint-like handler: when the sequencer camera ends, the game blends to another view target.
		S.Listener->OnEnded = [&S](APlayerController* PC, ALevelSequenceActor*)
		{
			++S.Counter;
			if (PC)
			{
				PC->SetViewTargetWithBlend(S.ActorB.Get(), 0.5f, VTBlend_Linear);
			}
		};
		S.Counter = 0;
		TestPCM(S)->PreUpdate = [&S]() { Player(S.SequenceA)->Stop(); };
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Counter > 0; });
	RetryUntil(Steps, [](FScenario& S) { return S.PCM()->PendingViewTarget.Target == nullptr; });
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Handler ran once"), S.Counter, 1);
		S.Test->TestTrue(TEXT("Final view target: the one set by the handler"), S.PCM()->GetViewTarget() == S.ActorB.Get());
		S.Test->TestTrue(TEXT("Sequencer state cleaned up"), S.Phase() == EACFSequencerCameraPhase::None && !S.Acf->IsExternalViewTarget(S.ActorA.Get()));
		S.Test->TestEqual(TEXT("No suppression left"), S.Acf->GetNumOutputSuppressions(), 0);
		S.CameraNear(GameplayLoc, 0.01, TEXT("ACF camera on the new (non-external) view target"));
		S.Test->TestEqual(TEXT("Events: start, end (once each)"), EventOrder(S.Listener.Get()), FString(TEXT("SE")));
		S.Test->TestEqual(TEXT("No notification delivered during a camera update"), S.Listener->DeliveriesDuringCameraUpdate, 0);
		S.Listener->OnEnded = nullptr;
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEventsHandlerStopsAndReleasesTest, "ActionCameraFramework.Sequencer.Events.HandlerStopsSequenceAndReleasesRequest", ACFSeqTest::Flags)
bool FACFEventsHandlerStopsAndReleasesTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		if (!UseCameraManagerClass(S, AACFSequencerTestCameraManager_Standard::StaticClass()))
		{
			return false;
		}
		S.Handle = RequestGameplay(S);
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 30.0);
		SetReturnBlend(S, 0.f);
		// The start handler stops the sequence at once; the end handler releases the ACF request.
		S.Listener->OnStarted = [&S](APlayerController*, ALevelSequenceActor*) { Player(S.SequenceA)->Stop(); };
		S.Listener->OnEnded = [&S](APlayerController*, ALevelSequenceActor*) { S.Acf->ReleaseCamera(S.Handle); };
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S) { Player(S.SequenceA)->Play(); return true; });
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Started once"), S.Listener->NumStarted, 1);
		S.Test->TestEqual(TEXT("Ended once (no recursion, no duplicate)"), S.Listener->NumEnded, 1);
		S.Test->TestEqual(TEXT("Order: start, end"), EventOrder(S.Listener.Get()), FString(TEXT("SE")));
		S.Test->TestEqual(TEXT("No notification delivered during a camera update"), S.Listener->DeliveriesDuringCameraUpdate, 0);
		S.Test->TestTrue(TEXT("Sequencer state: none"), S.Phase() == EACFSequencerCameraPhase::None);
		S.Test->TestEqual(TEXT("The request was released by the handler"), S.Acf->GetDirector()->GetNumRequests(), 0);
		S.Test->TestEqual(TEXT("Nothing left pending"), S.Sequencer->GetNumPendingEvents(), 0);
		S.Test->TestEqual(TEXT("No suppression left"), S.Acf->GetNumOutputSuppressions(), 0);
		S.Listener->OnStarted = nullptr;
		S.Listener->OnEnded = nullptr;
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEventsQueueOverflowTest, "ActionCameraFramework.Sequencer.Events.QueueOverflowAndChainedEvents", ACFSeqTest::Flags)
bool FACFEventsQueueOverflowTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	static const EACFSequencerReturnKind Kinds[] = { EACFSequencerReturnKind::Cut, EACFSequencerReturnKind::AcfReturnBlend, EACFSequencerReturnKind::SequencerBlend };
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		ULocalPlayer* LocalPlayer = S.Controller->GetLocalPlayer();
		for (int32 I = 0; I < 150; ++I)
		{
			S.Sequencer->QueueNotificationForTesting(I % 2 == 0, LocalPlayer, S.Controller.Get(), nullptr, Kinds[(I / 2) % 3]);
		}
		S.Test->TestEqual(TEXT("Queued, not delivered synchronously"), S.Listener->Events.Num(), 0);
		S.Test->TestEqual(TEXT("150 pending"), S.Sequencer->GetNumPendingEvents(), 150);
		S.MarkFrame = GFrameCounter;
		return true;
	});
	WaitFrames(Steps, 2);
	Steps.Add([](FScenario& S)
	{
		// No new sequencer activity at all: the remainder after a 64-event pass is delivered by the next delivery points.
		S.Test->TestEqual(TEXT("All 150 delivered without any new event"), S.Listener->Events.Num(), 150);
		S.Test->TestEqual(TEXT("Nothing left pending"), S.Sequencer->GetNumPendingEvents(), 0);
		bool bOrder = S.Listener->Events.Num() == 150;
		for (int32 I = 0; bOrder && I < 150; ++I)
		{
			bOrder = S.Listener->Events[I].bStarted == (I % 2 == 0);
		}
		for (int32 I = 0; bOrder && I < S.Listener->ReturnKinds.Num(); ++I)
		{
			bOrder = S.Listener->ReturnKinds[I] == Kinds[I % 3];
		}
		S.Test->TestTrue(TEXT("Delivered in the queued order"), bOrder);
		TMap<uint64, int32> PerFrame;
		for (const UACFSequencerTestListener::FEvent& Event : S.Listener->Events)
		{
			++PerFrame.FindOrAdd(Event.Frame);
		}
		int32 MaxFrame = 0;
		for (const TPair<uint64, int32>& Pair : PerFrame)
		{
			MaxFrame = FMath::Max(MaxFrame, Pair.Value);
		}
		S.Test->TestTrue(FString::Printf(TEXT("Bounded per frame (max %d; 64 per delivery pass, 3 points per frame)"), MaxFrame), MaxFrame <= 64 * 3);
		S.Test->TestEqual(TEXT("None delivered during a camera update"), S.Listener->DeliveriesDuringCameraUpdate, 0);

		// A handler that keeps queueing new notifications while it is being notified: no recursion, all eventually delivered.
		S.Listener->Events.Reset();
		S.Counter = 0;
		ULocalPlayer* LocalPlayer = S.Controller->GetLocalPlayer();
		S.Listener->OnStarted = [&S, LocalPlayer](APlayerController* PC, ALevelSequenceActor*)
		{
			if (++S.Counter < 200)
			{
				S.Sequencer->QueueNotificationForTesting(true, LocalPlayer, PC, nullptr, EACFSequencerReturnKind::Cut);
			}
		};
		S.Sequencer->QueueNotificationForTesting(true, LocalPlayer, S.Controller.Get(), nullptr, EACFSequencerReturnKind::Cut);
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Counter >= 200 || GFrameCounter - S.MarkFrame > 30; });
	WaitFrames(Steps, 2);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Chained: every notification delivered once"), S.Listener->Events.Num(), 200);
		S.Test->TestEqual(TEXT("Chained: nothing left pending"), S.Sequencer->GetNumPendingEvents(), 0);
		TSet<uint64> Frames;
		for (const UACFSequencerTestListener::FEvent& Event : S.Listener->Events)
		{
			Frames.Add(Event.Frame);
		}
		S.Test->TestTrue(FString::Printf(TEXT("Chained: spread over the delivery points (%d frames), not one unbounded recursion"), Frames.Num()), Frames.Num() >= 2);
		S.Listener->OnStarted = nullptr;
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEventsPendingLifetimeTest, "ActionCameraFramework.Sequencer.Events.PendingEventsWithRemovedPlayerListenerAndWorld", ACFSeqTest::Flags)
bool FACFEventsPendingLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this); // B-9: only for a host Blueprint PlayerController
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		FString Error;
		S.SecondPlayer = S.World()->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(1), Error, true);
		return S.Test->TestTrue(FString::Printf(TEXT("second local player %s"), *Error), S.SecondPlayer.IsValid());
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		ULocalPlayer* Second = S.SecondPlayer.Get();
		APlayerController* PC2 = Second->GetPlayerController(S.World());
		// A listener that is garbage collected while a notification is pending.
		UACFSequencerTestListener* Temp = NewObject<UACFSequencerTestListener>();
		S.Sequencer->OnSequencerCameraStarted.AddDynamic(Temp, &UACFSequencerTestListener::HandleStarted);
		S.Sequencer->QueueNotificationForTesting(true, S.Controller->GetLocalPlayer(), S.Controller.Get(), nullptr, EACFSequencerReturnKind::Cut);
		S.Sequencer->QueueNotificationForTesting(true, Second, PC2, nullptr, EACFSequencerReturnKind::Cut);
		S.Sequencer->QueueNotificationForTesting(false, Second, PC2, nullptr, EACFSequencerReturnKind::Cut);
		// Before any delivery point: player 2 is removed and the temporary listener collected.
		ACFTestEnvironment::RemoveLocalPlayer(Second); // also its pawn (Phase 7A-2)
		Temp->MarkAsGarbage();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		S.Test->TestEqual(TEXT("Three pending before the delivery point"), S.Sequencer->GetNumPendingEvents(), 3);

		// A pending notification in a world that is destroyed is never delivered.
		FTestWorldWrapper Wrapper;
		if (S.Test->TestTrue(TEXT("test world created"), Wrapper.CreateTestWorld(EWorldType::Game)))
		{
			UACFSequencerCameraSubsystem* Other = Wrapper.GetTestWorld()->GetSubsystem<UACFSequencerCameraSubsystem>();
			if (S.Test->TestNotNull(TEXT("sequencer subsystem of the test world"), Other))
			{
				UACFSequencerTestListener* OtherListener = NewObject<UACFSequencerTestListener>();
				S.Keep.Emplace(OtherListener);
				Other->OnSequencerCameraStarted.AddDynamic(OtherListener, &UACFSequencerTestListener::HandleStarted);
				Other->QueueNotificationForTesting(true, S.Controller->GetLocalPlayer(), S.Controller.Get(), nullptr, EACFSequencerReturnKind::Cut);
				S.Test->TestEqual(TEXT("Other world: one pending"), Other->GetNumPendingEvents(), 1);
				S.Value = 0.0;
				Wrapper.DestroyTestWorld(false);
				S.Test->TestEqual(TEXT("World destroyed: its pending notification was never delivered"), OtherListener->NumStarted, 0);
			}
		}
		return true;
	});
	WaitFrames(Steps, 2);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Only player 1's notification delivered (removed player's dropped)"), S.Listener->Events.Num(), 1);
		S.Test->TestTrue(TEXT("It is player 1's"), S.Listener->Events.Num() == 1 && S.Listener->Events[0].Controller == S.Controller);
		S.Test->TestEqual(TEXT("Nothing left pending"), S.Sequencer->GetNumPendingEvents(), 0);
		S.SecondPlayer.Reset();
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEventsRefreshNowPauseTest, "ActionCameraFramework.Sequencer.Events.RefreshNowPauseAndSameFrame", ACFSeqTest::Flags)
bool FACFEventsRefreshNowPauseTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	AddPlayingSequence(Steps, 0.f);
	Steps.Add([](FScenario& S)
	{
		const int32 EndedBefore = S.Listener->NumEnded;
		Player(S.SequenceA)->Stop();
		S.Sequencer->RefreshNow();
		S.Sequencer->RefreshNow();
		S.Sequencer->RefreshNow();
		S.Test->TestTrue(TEXT("RefreshNow: the state is decided at once"), S.Phase() == EACFSequencerCameraPhase::None);
		S.Test->TestEqual(TEXT("RefreshNow: no synchronous notification"), S.Listener->NumEnded, EndedBefore);
		S.Test->TestEqual(TEXT("RefreshNow x3: one pending notification"), S.Sequencer->GetNumPendingEvents(), 1);
		return true;
	});
	WaitFrames(Steps, 2);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Delivered once (no duplicate from repeated refreshes)"), S.Listener->NumEnded, 1);
		S.Test->TestEqual(TEXT("Order: start, end"), EventOrder(S.Listener.Get()), FString(TEXT("SE")));
		// Paused: notifications are still delivered (the subsystem ticks while paused only to deliver).
		UGameplayStatics::SetGamePaused(S.World(), true);
		S.Listener->Events.Reset();
		ULocalPlayer* LocalPlayer = S.Controller->GetLocalPlayer();
		// Start and end detected in the same frame: each carries what it was detected with.
		S.Sequencer->QueueNotificationForTesting(true, LocalPlayer, S.Controller.Get(), S.SequenceA.Get(), EACFSequencerReturnKind::Cut);
		S.Sequencer->QueueNotificationForTesting(false, LocalPlayer, S.Controller.Get(), S.SequenceA.Get(), EACFSequencerReturnKind::AcfReturnBlend);
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Paused: delivered while paused"), S.World()->IsPaused() && S.Listener->Events.Num() == 2);
		S.Test->TestEqual(TEXT("Same frame: start then end"), EventOrder(S.Listener.Get()), FString(TEXT("SE")));
		S.Test->TestTrue(TEXT("Same frame: both carry the sequence they were detected with"), S.Listener->Events.Num() == 2
			&& S.Listener->Events[0].Sequence == S.SequenceA && S.Listener->Events[1].Sequence == S.SequenceA);
		S.Test->TestTrue(TEXT("Same frame: the end carries its own return kind"), S.Listener->LastReturnKind == EACFSequencerReturnKind::AcfReturnBlend);
		UGameplayStatics::SetGamePaused(S.World(), false);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// =================================================================================================
// Phase 6B / B-7: 戻りのブレンドの Blueprint 入口（Blend ACF Camera From Pose / Current View For Player、Release Camera With
// Return Blend For Player）。実際の PCM の画面で確認する。

namespace ACFSeqTest
{
	static FACFCameraPose CinePose()
	{
		FACFCameraPose Pose;
		Pose.Location = CineLoc;
		Pose.Rotation = CineRot;
		Pose.FieldOfView = CineFov;
		return Pose;
	}

	static FString ResultName(EACFCameraReturnBlendResult Result) { return UEnum::GetValueAsString(Result); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFReturnBlendBlueprintMatchesCppTest, "ActionCameraFramework.Sequencer.ReturnBlend.BlueprintMatchesCpp", ACFSeqTest::Flags)
bool FACFReturnBlendBlueprintMatchesCppTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	struct FTrack { TArray<FVector> Blueprint; TArray<FVector> Cpp; };
	TSharedRef<FTrack> Track = MakeShared<FTrack>();
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S) { S.Handle = RequestGameplay(S); return true; });
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		EACFCameraReturnBlendResult Result = EACFCameraReturnBlendResult::NoPlayer;
		const bool bOk = UACFCameraBlueprintLibrary::BlendCameraFromPoseForPlayer(S.Controller.Get(), CinePose(), 0.5f, EACFCameraBlendCurve::Linear, Result);
		S.Test->TestTrue(FString::Printf(TEXT("Blueprint entry: started (%s)"), *ResultName(Result)), bOk && Result == EACFCameraReturnBlendResult::Started);
		S.Counter = 0;
		return true;
	});
	Steps.Add([Track](FScenario& S)
	{
		Track->Blueprint.Add(S.Camera());
		S.bRetry = ++S.Counter < 40;
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.01, TEXT("Blueprint: back on the ACF camera"));
		FACFCameraBlendSettings Blend;
		Blend.Curve = EACFCameraBlendCurve::Linear;
		Blend.Duration = 0.5f;
		S.Test->TestTrue(TEXT("C++ entry (existing Blend ACF Camera From Pose): accepted"), S.Acf->BlendFromPose(CinePose(), Blend));
		S.Counter = 0;
		return true;
	});
	Steps.Add([Track](FScenario& S)
	{
		Track->Cpp.Add(S.Camera());
		S.bRetry = ++S.Counter < 40;
		return true;
	});
	Steps.Add([Track](FScenario& S)
	{
		bool bSame = Track->Blueprint.Num() == Track->Cpp.Num();
		double MaxDiff = 0.0;
		for (int32 I = 0; bSame && I < Track->Cpp.Num(); ++I)
		{
			MaxDiff = FMath::Max(MaxDiff, FVector::Dist(Track->Blueprint[I], Track->Cpp[I]));
		}
		S.Test->TestTrue(FString::Printf(TEXT("Blueprint and C++ give the same camera every frame (max diff %.4f cm)"), MaxDiff), bSame && MaxDiff < 0.01);
		S.Test->TestTrue(FString::Printf(TEXT("First frame near the given pose (%.1f cm from it)"), FVector::Dist(Track->Blueprint[0], CineLoc)),
			FVector::Dist(Track->Blueprint[0], CineLoc) < GameplayToCine * 0.1);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFReturnBlendConditionsTest, "ActionCameraFramework.Sequencer.ReturnBlend.InvalidInputsAndConditions", ACFSeqTest::Flags)
bool FACFReturnBlendConditionsTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		EACFCameraReturnBlendResult Result = EACFCameraReturnBlendResult::Started;
		S.Test->TestFalse(TEXT("No request: refused"), UACFCameraBlueprintLibrary::BlendCameraFromPoseForPlayer(S.Controller.Get(), CinePose(), 0.5f, EACFCameraBlendCurve::Linear, Result));
		S.Test->TestTrue(FString::Printf(TEXT("No request: NoCameraRequest (%s)"), *ResultName(Result)), Result == EACFCameraReturnBlendResult::NoCameraRequest);
		S.Test->TestFalse(TEXT("No player: refused"), UACFCameraBlueprintLibrary::BlendCameraFromPoseForPlayer(nullptr, CinePose(), 0.5f, EACFCameraBlendCurve::Linear, Result));
		S.Test->TestTrue(TEXT("No player: NoPlayer"), Result == EACFCameraReturnBlendResult::NoPlayer);
		// A local player without a PlayerController (and so without a PlayerCameraManager).
		FString Error;
		ULocalPlayer* Bare = S.World()->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(2), Error, /*bSpawnPlayerController*/ false);
		if (S.Test->TestNotNull(TEXT("local player without a controller"), Bare))
		{
			UACFCameraSubsystem* BareAcf = Bare->GetSubsystem<UACFCameraSubsystem>();
			S.Test->TestTrue(TEXT("No PCM: NoCameraManager"), BareAcf && BareAcf->TryBlendFromPose(CinePose(), 0.5f) == EACFCameraReturnBlendResult::NoCameraManager);
			ACFTestEnvironment::RemoveLocalPlayer(Bare);
		}
		S.Handle = RequestGameplay(S);
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		const int32 RejectedBefore = S.Acf->GetHealthDiagnostics().RejectedInputs;
		EACFCameraReturnBlendResult Result = EACFCameraReturnBlendResult::Started;
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		const float Inf = std::numeric_limits<float>::infinity();
		for (const float Bad : { NaN, Inf, -Inf, -1.f })
		{
			const bool bOk = UACFCameraBlueprintLibrary::BlendCameraFromPoseForPlayer(S.Controller.Get(), CinePose(), Bad, EACFCameraBlendCurve::Linear, Result);
			S.Test->TestTrue(FString::Printf(TEXT("Blend time %f: InvalidBlendTime (%s)"), Bad, *ResultName(Result)), !bOk && Result == EACFCameraReturnBlendResult::InvalidBlendTime);
		}
		FACFCameraPose BadPose = CinePose();
		BadPose.Location.X = NaN;
		S.Test->TestTrue(TEXT("NaN pose: InvalidPose"), !UACFCameraBlueprintLibrary::BlendCameraFromPoseForPlayer(S.Controller.Get(), BadPose, 0.5f, EACFCameraBlendCurve::Linear, Result)
			&& Result == EACFCameraReturnBlendResult::InvalidPose);
		BadPose = CinePose();
		BadPose.FieldOfView = 0.f;
		S.Test->TestTrue(TEXT("FOV 0 pose: InvalidPose"), !UACFCameraBlueprintLibrary::BlendCameraFromPoseForPlayer(S.Controller.Get(), BadPose, 0.5f, EACFCameraBlendCurve::Linear, Result)
			&& Result == EACFCameraReturnBlendResult::InvalidPose);
		S.Test->TestEqual(TEXT("Health (B-4): the invalid pose and non-finite times were reported"), S.Acf->GetHealthDiagnostics().RejectedInputs - RejectedBefore, 5);
		S.Test->TestTrue(TEXT("Blend time 0: success, nothing to blend (Cut)"), UACFCameraBlueprintLibrary::BlendCameraFromPoseForPlayer(S.Controller.Get(), CinePose(), 0.f, EACFCameraBlendCurve::Linear, Result)
			&& Result == EACFCameraReturnBlendResult::Cut);
		return true;
	});
	WaitFrames(Steps, 2);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.01, TEXT("After the refused calls and the 0 s call: the ACF camera, untouched"));
		S.Test->TestFalse(TEXT("No ACF blend was started"), S.Acf->GetLastEvaluationResult().bIsBlending);
		// The PCM blends view targets: no second blend from ACF.
		S.GameplayViewTarget = S.PCM()->GetViewTarget();
		S.ActorB = SpawnPoint(S, CineLocB);
		S.Controller->SetViewTargetWithBlend(S.ActorB.Get(), 1.f, VTBlend_Linear);
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		EACFCameraReturnBlendResult Result = EACFCameraReturnBlendResult::Started;
		S.Test->TestTrue(TEXT("PCM blending: CameraManagerBlending (no double blend)"), !UACFCameraBlueprintLibrary::BlendCameraFromPoseForPlayer(S.Controller.Get(), CinePose(), 0.5f, EACFCameraBlendCurve::Linear, Result)
			&& Result == EACFCameraReturnBlendResult::CameraManagerBlending);
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.PCM()->PendingViewTarget.Target == nullptr; });
	Steps.Add([](FScenario& S)
	{
		S.Test->TestFalse(TEXT("PCM blend: ACF never blended on its own"), S.Acf->GetLastEvaluationResult().bIsBlending);
		// External view target on screen (B-2): refused.
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.Acf->AddExternalViewTarget(S.ActorA.Get(), S.Listener.Get());
		S.Controller->SetViewTarget(S.ActorA.Get());
		EACFCameraReturnBlendResult Result = EACFCameraReturnBlendResult::Started;
		S.Test->TestTrue(TEXT("External view target on screen: ExternalViewTargetActive"), !UACFCameraBlueprintLibrary::BlendCameraFromCurrentViewForPlayer(S.Controller.Get(), 0.5f, EACFCameraBlendCurve::Linear, Result)
			&& Result == EACFCameraReturnBlendResult::ExternalViewTargetActive);
		S.Acf->RemoveExternalViewTarget(S.ActorA.Get(), S.Listener.Get());
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFReturnBlendRecallPlayersTest, "ActionCameraFramework.Sequencer.ReturnBlend.RecallDuringBlendAndLocalPlayers", ACFSeqTest::Flags)
bool FACFReturnBlendRecallPlayersTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this); // B-9: only for a host Blueprint PlayerController
	static const FVector SecondLoc = Base + FVector(0.f, 8000.f, 0.f);
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		FString Error;
		S.SecondPlayer = S.World()->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(1), Error, true);
		return S.Test->TestTrue(TEXT("second local player"), S.SecondPlayer.IsValid());
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.HandleB = S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>()->RequestCamera(MakeFixedPreset(S, SecondLoc, FRotator(0.f, 180.f, 0.f), 70.f), FACFCameraRequestParams());
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		EACFCameraReturnBlendResult Result;
		APlayerController* PC2 = S.SecondPlayer->GetPlayerController(S.World());
		S.Test->TestTrue(TEXT("Player 2: started"), UACFCameraBlueprintLibrary::BlendCameraFromPoseForPlayer(PC2, CinePose(), 1.f, EACFCameraBlendCurve::Linear, Result));
		return true;
	});
	WaitFrames(Steps, 15);
	Steps.Add([](FScenario& S)
	{
		APlayerController* PC2 = S.SecondPlayer->GetPlayerController(S.World());
		S.CameraNear(GameplayLoc, 0.01, TEXT("Player 1 untouched by player 2's return blend"));
		S.Test->TestFalse(TEXT("Player 1 not blending"), S.Acf->GetLastEvaluationResult().bIsBlending);
		S.Test->TestTrue(TEXT("Player 2 blending"), S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>()->GetLastEvaluationResult().bIsBlending);
		// Called again during its blend: restarts from the new pose (no jump from the old one).
		FACFCameraPose Again = CinePose();
		Again.Location = SecondLoc + FVector(0.0, 0.0, 600.0);
		EACFCameraReturnBlendResult Result;
		S.Test->TestTrue(TEXT("Player 2: called again during the blend"), UACFCameraBlueprintLibrary::BlendCameraFromPoseForPlayer(PC2, Again, 0.5f, EACFCameraBlendCurve::Linear, Result));
		S.SavedLocation = Again.Location;
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		APlayerController* PC2 = S.SecondPlayer->GetPlayerController(S.World());
		const double FromNew = FVector::Dist(PC2->PlayerCameraManager->GetCameraLocation(), S.SavedLocation);
		S.Test->TestTrue(FString::Printf(TEXT("Restarted from the new pose (%.1f cm from it)"), FromNew), FromNew < 30.0);
		return true;
	});
	WaitFrames(Steps, 40);
	Steps.Add([](FScenario& S)
	{
		APlayerController* PC2 = S.SecondPlayer->GetPlayerController(S.World());
		S.Test->TestTrue(TEXT("Player 2 back on its ACF camera"), PC2->PlayerCameraManager->GetCameraLocation().Equals(SecondLoc, 0.01));
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFReturnBlendCustomCameraTest, "ActionCameraFramework.Sequencer.ReturnBlend.CustomCameraRoundTripAndSequencerEnd", ACFSeqTest::Flags)
bool FACFReturnBlendCustomCameraTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	// README example: the game's own camera actor, shown with a cut, then a smooth return to the ACF camera.
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.GameplayViewTarget = S.PCM()->GetViewTarget();
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.Acf->AddExternalViewTarget(S.ActorA.Get(), S.Listener.Get()); // 1. ACF stays off the custom camera
		S.Controller->SetViewTarget(S.ActorA.Get());                     // 2. cut to it
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(CineLoc, 0.001, TEXT("Custom camera on screen, untouched by ACF"));
		S.Controller->SetViewTarget(S.GameplayViewTarget.Get());          // 3. cut back (same frame as 4)
		EACFCameraReturnBlendResult Result;
		const bool bOk = UACFCameraBlueprintLibrary::BlendCameraFromCurrentViewForPlayer(S.Controller.Get(), 0.5f, EACFCameraBlendCurve::Linear, Result); // 4.
		S.Test->TestTrue(FString::Printf(TEXT("Return blend from the current view started (%s)"), *ResultName(Result)), bOk && Result == EACFCameraReturnBlendResult::Started);
		S.Acf->RemoveExternalViewTarget(S.ActorA.Get(), S.Listener.Get()); // 5. registration no longer needed
		S.ResetSamples();
		S.SavedLocation = S.Camera();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		const double First = FVector::Dist(S.Camera(), S.SavedLocation);
		S.Test->TestTrue(FString::Printf(TEXT("First frame after the cut back: still at the custom camera picture (%.1f cm)"), First), First <= GameplayToCine / 30.0 * 1.5);
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return !S.Acf->GetLastEvaluationResult().bIsBlending; });
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.01, TEXT("Back on the ACF camera"));
		S.Test->TestTrue(FString::Printf(TEXT("Smooth (max %.1f cm / frame)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine / 30.0 * 1.5);
		// Sequencer: a cut return (Return Blend 0) and the game's own return blend from the Ended notification (B-6).
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 0.5);
		SetReturnBlend(S, 0.f);
		S.Listener->OnEnded = [&S](APlayerController* PC, ALevelSequenceActor*)
		{
			EACFCameraReturnBlendResult Result;
			S.Counter = UACFCameraBlueprintLibrary::BlendCameraFromPoseForPlayer(PC, CinePose(), 0.5f, EACFCameraBlendCurve::Linear, Result) ? 1 : -1;
			S.Value = static_cast<double>(static_cast<uint8>(Result));
		};
		S.Counter = 0;
		Player(S.SequenceA)->Play();
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return S.Counter != 0; });
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("From the Ended notification (delivered outside the camera update): started"), S.Counter, 1);
		S.Test->TestTrue(TEXT("Return kind: the sequencer cut"), S.Listener->LastReturnKind == EACFSequencerReturnKind::Cut);
		S.Listener->OnEnded = nullptr;
		S.ResetSamples();
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return !S.Acf->GetLastEvaluationResult().bIsBlending; });
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.01, TEXT("After the game's return blend: the ACF camera"));
		S.Test->TestTrue(FString::Printf(TEXT("Game's return blend after the sequencer cut: no jump after its first frame (max step change %.1f)"), S.MaxStepChange),
			S.BigJumps <= 1);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFReturnBlendReleaseTest, "ActionCameraFramework.Sequencer.ReturnBlend.ReleaseWithReturnBlend", ACFSeqTest::Flags)
bool FACFReturnBlendReleaseTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S); // the lower camera (its blend in is a cut)
		FACFCameraRequestParams High;
		High.Priority = 10;
		S.HandleB = S.Acf->RequestCamera(MakeFixedPreset(S, CineLoc, CineRot, CineFov), High);
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(CineLoc, 0.01, TEXT("Higher request on screen"));
		S.ResetSamples();
		// Without a return blend the lower request's own blend in (a cut) is used.
		S.Test->TestTrue(TEXT("Plain release"), S.Acf->ReleaseCamera(S.HandleB));
		return true;
	});
	WaitFrames(Steps, 2);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.01, TEXT("Plain release: cut to the lower camera (unchanged behavior)"));
		FACFCameraRequestParams High;
		High.Priority = 10;
		S.HandleB = S.Acf->RequestCamera(MakeFixedPreset(S, CineLoc, CineRot, CineFov), High);
		return true;
	});
	WaitFrames(Steps, 5);
	Steps.Add([](FScenario& S)
	{
		S.ResetSamples();
		S.Test->TestTrue(TEXT("Release with return blend"), UACFCameraBlueprintLibrary::ReleaseCameraWithReturnBlendForPlayer(S.Controller.Get(), S.HandleB, 0.5f, EACFCameraBlendCurve::Linear));
		S.Test->TestFalse(TEXT("Handle invalidated"), S.HandleB.IsValid());
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Return blend used for the lower request"), S.Acf->GetLastEvaluationResult().bIsBlending);
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return !S.Acf->GetLastEvaluationResult().bIsBlending; });
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.01, TEXT("Back on the lower camera"));
		S.Test->TestTrue(FString::Printf(TEXT("Smooth (max %.1f cm / frame)"), S.MaxFrameJump), S.MaxFrameJump < GameplayToCine / 30.0 * 1.5);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}


// -------------------------------------------------------------------------------------------------
// Phase 7A-1: 調整中のリクエストに Sequencer の Camera Cut が入っても、出力制御（外部 ViewTarget、抑制なし）は従来どおり。
// 戻った後は調整した値が映り、調整を止めると Preset の値に戻る。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFSequencerTuningTest, "ActionCameraFramework.Sequencer.TuningKeepsTheSequencerOutputControl", ACFSeqTest::Flags)
bool FACFSequencerTuningTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Handle = RequestGameplay(S);
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 1.0);
		SetReturnBlend(S, 0.f); // cut back: the first frame after the sequence is the tuned gameplay camera
		return S.Test->TestNotNull(TEXT("sequence actor created"), S.SequenceA.Get());
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Start tuning"), UACFCameraTuningLibrary::StartCameraTuning(S.Controller.Get(), S.Handle), EACFCameraTuningResult::Ok);
		S.Test->TestEqual(TEXT("Set FOV 50"), UACFCameraTuningLibrary::SetCameraTuningValue(S.Controller.Get(), S.Handle, EACFCameraTuningParameter::FieldOfView, FVector(50, 0, 0)), EACFCameraTuningResult::Ok);
		return true;
	});
	WaitFrames(Steps, 2);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Before the sequence: tuned FOV on screen"), S.PCM()->GetFOVAngle(), 50.f, 0.01f);
		Player(S.SequenceA)->Play();
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::InControl)
		{
			S.bRetry = true;
			return true;
		}
		S.ResetSamples();
		return true;
	});
	WaitFrames(Steps, 10);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(CineLoc, 0.1, TEXT("During the sequence: the cine camera"));
		S.Test->TestEqual(TEXT("During: cine FOV (the tuned ACF value is not written to the sequencer camera)"), S.PCM()->GetFOVAngle(), CineFov, 0.01f);
		S.Test->TestTrue(TEXT("During: bypassed, not suppressed (output control unchanged)"), S.Acf->IsOutputBypassed() && !S.Acf->IsOutputSuppressed());
		S.Test->TestEqual(TEXT("During: ACF weight untouched"), S.MinAcfWeight, 1.f);
		S.Test->TestTrue(TEXT("During: still tuning in the background"), UACFCameraTuningLibrary::GetCameraTuningStatus(S.Controller.Get(), S.Handle).bTuning);
		S.Test->TestEqual(TEXT("During: background evaluation uses the tuned value"), S.Acf->GetLastEvaluationResult().Pose.FieldOfView, 50.f, 0.01f);
		return true;
	});
	Steps.Add([](FScenario& S)
	{
		if (S.Phase() != EACFSequencerCameraPhase::None)
		{
			S.bRetry = true;
			return true;
		}
		return true;
	});
	WaitFrames(Steps, 2);
	Steps.Add([](FScenario& S)
	{
		S.CameraNear(GameplayLoc, 0.1, TEXT("After the sequence: gameplay camera"));
		S.Test->TestEqual(TEXT("After: the tuned FOV"), S.PCM()->GetFOVAngle(), 50.f, 0.01f);
		S.Test->TestEqual(TEXT("No suppression was used"), S.MaxSuppressions, 0);
		S.Test->TestEqual(TEXT("Stop tuning"), UACFCameraTuningLibrary::StopCameraTuning(S.Controller.Get(), S.Handle), EACFCameraTuningResult::Ok);
		return true;
	});
	WaitFrames(Steps, 2);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Stopped: the preset FOV"), S.PCM()->GetFOVAngle(), GameplayFov, 0.01f);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// =================================================================================================
// Phase 7B-1: Orbital Follow の移動方向アシスト（README 29 章）を実際の PCM・World・Level Sequence で確認する。

#if !UE_BUILD_SHIPPING
namespace ACFSeqTest
{
	/** Orbit (+ Combat) with Movement Assistance towards the movement direction; yaw 0 at the start. */
	static UACFCameraPreset* MakeAssistPreset(FScenario& S, bool bWithCombat = false)
	{
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		UACFCameraNode_OrbitalFollow* Orbit = NewObject<UACFCameraNode_OrbitalFollow>(Preset);
		Orbit->Distance = 400.f;
		Orbit->InitialRotation = EACFCameraOrbitInitialRotation::Fixed;
		Orbit->InitialYaw = 0.f;
		Orbit->InitialPitch = -15.f;
		Orbit->bEnableMovementAssistance = true;
		Orbit->RecenterHeading = EACFCameraRecenterHeading::MovementDirection;
		Orbit->RecenterDelay = 0.3f;
		Orbit->RecenterTime = 0.3f;
		Orbit->bRecenterPitch = false;
		Orbit->RequiredMovementDuration = 0.2f;
		Orbit->AngularDeadZone = 0.f;
		Orbit->MaxAssistanceYawSpeed = 90.f;
		Preset->Nodes.Add(Orbit);
		if (bWithCombat)
		{
			Preset->Nodes.Add(NewObject<UACFCameraNode_Combat>(Preset));
		}
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		S.Keep.Emplace(Preset);
		return Preset;
	}

	static const FACFCameraRigDebug& AssistDebug(UACFCameraSubsystem* Acf) { return Acf->GetDirector()->GetLastDebug(); }
	static float AcfYaw(UACFCameraSubsystem* Acf) { return Acf->GetLastEvaluationResult().Pose.Rotation.Yaw; }

	static FString DescribeAssist(UACFCameraSubsystem* Acf)
	{
		const FACFCameraRigDebug& D = AssistDebug(Acf);
		return FString::Printf(TEXT("%s / %s, yaw %.2f, speed %.0f, wait %.2f"), *UEnum::GetValueAsString(D.AssistanceState), *UEnum::GetValueAsString(D.AssistanceReason),
			AcfYaw(Acf), D.AssistanceMovementSpeed, D.AssistanceWaitTime);
	}

	/**
	 * Moves player 1's follow target for Frames frames at Velocity (cm/s, fixed 1/60 s step) and checks every frame that the
	 * debugger tells the truth: a frame reported Assisting (reason None) turned the ACF camera, a frame reported Waiting /
	 * Suppressed / Disabled (without input) did not.
	 */
	static void MoveFor(TArray<FStep>& Steps, int32 Frames, const FVector& Velocity)
	{
		Steps.Add([](FScenario& S) { S.Counter = 0; S.bAssistHasLast = false; return true; });
		Steps.Add([Frames, Velocity](FScenario& S)
		{
			const uint64 Evaluated = S.Acf->GetDirector()->GetLastEvaluatedFrame();
			const float Yaw = AcfYaw(S.Acf.Get());
			if (S.bAssistHasLast && Evaluated == S.AssistLastFrame + 1 && !S.bAssistInputThisFrame && S.Acf->IsActiveCamera(S.AssistHandle))
			{
				const FACFCameraRigDebug& D = AssistDebug(S.Acf.Get());
				const bool bTurned = FMath::Abs(FRotator::NormalizeAxis(Yaw - S.AssistLastYaw)) > 1e-4f;
				const bool bShouldTurn = D.AssistanceState == EACFCameraAssistanceState::Assisting && D.AssistanceReason == EACFCameraAssistanceReason::None;
				const bool bShouldHold = D.AssistanceState != EACFCameraAssistanceState::Assisting;
				++S.AssistChecked;
				if ((bShouldTurn && !bTurned) || (bShouldHold && bTurned))
				{
					++S.AssistMismatches;
					S.Test->AddInfo(FString::Printf(TEXT("Debugger mismatch at frame %llu: %s, yaw %.4f -> %.4f"), Evaluated, *DescribeAssist(S.Acf.Get()), S.AssistLastYaw, Yaw));
				}
			}
			S.bAssistInputThisFrame = false;
			S.bAssistHasLast = S.Acf->IsActiveCamera(S.AssistHandle); // a yaw of another camera is no reference
			S.AssistLastYaw = Yaw;
			S.AssistLastFrame = Evaluated;
			if (AActor* Mover = S.Mover.Get())
			{
				Mover->SetActorLocation(Mover->GetActorLocation() + Velocity * FixedDt);
			}
			S.bRetry = ++S.Counter < Frames;
			return true;
		});
	}

	static FVector Along(float YawDegrees, float Speed)
	{
		return FRotator(0.f, YawDegrees, 0.f).Vector() * Speed;
	}
}

// 複数ローカルプレイヤーの独立・Suppression の登録 / 解除 / Source 破棄・手動入力・PCM の作り直し・LocalPlayer の削除・
// Debugger の表示と実際のカメラの一致。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistPlayersSuppressionTest, "ActionCameraFramework.Sequencer.Assistance.PlayersSuppressionPcmAndDebugger", ACFSeqTest::Flags)
bool FACFAssistPlayersSuppressionTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this);
	static const FVector SecondLoc = Base + FVector(0.f, 8000.f, 0.f);
	auto SecondAcf = [](FScenario& S) { return S.SecondPlayer.IsValid() ? S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>() : nullptr; };
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Mover = SpawnPoint(S, Base);
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.Mover.Get();
		S.AssistHandle = S.Acf->RequestCamera(MakeAssistPreset(S), Params);
		FString Error;
		S.SecondPlayer = S.World()->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(1), Error, true);
		return S.Test->TestTrue(FString::Printf(TEXT("Second local player (%s)"), *Error), S.SecondPlayer.IsValid());
	});
	WaitFrames(Steps, 5);
	Steps.Add([SecondAcf](FScenario& S)
	{
		UACFCameraSubsystem* Acf2 = SecondAcf(S);
		if (!S.Test->TestNotNull(TEXT("Player 2 ACF"), Acf2) || !S.SecondPlayer->GetPlayerController(S.World()))
		{
			return false;
		}
		S.ActorB = SpawnPoint(S, SecondLoc); // player 2 follows a target that does not move
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.ActorB.Get();
		S.HandleB = Acf2->RequestCamera(MakeAssistPreset(S), Params);
		return true;
	});
	WaitFrames(Steps, 3);
	MoveFor(Steps, 90, Along(90.f, 400.f));
	Steps.Add([SecondAcf](FScenario& S)
	{
		UACFCameraSubsystem* Acf2 = SecondAcf(S);
		S.Test->TestTrue(FString::Printf(TEXT("Player 1 assists towards the movement (%s)"), *DescribeAssist(S.Acf.Get())),
			AssistDebug(S.Acf.Get()).AssistanceState == EACFCameraAssistanceState::Assisting && AcfYaw(S.Acf.Get()) > 20.f);
		S.Test->TestTrue(FString::Printf(TEXT("The PCM renders the assisted yaw (%.3f vs %.3f)"), S.PCM()->GetCameraRotation().Yaw, AcfYaw(S.Acf.Get())),
			FMath::Abs(FRotator::NormalizeAxis(S.PCM()->GetCameraRotation().Yaw - AcfYaw(S.Acf.Get()))) < 1.f);
		S.Test->TestTrue(FString::Printf(TEXT("Player 2 independent: its target stands, it waits (%s)"), *DescribeAssist(Acf2)),
			AssistDebug(Acf2).AssistanceReason == EACFCameraAssistanceReason::BelowSpeed && FMath::IsNearlyEqual(AcfYaw(Acf2), 0.f, 1e-3f));
		// Suppression by a source of player 1 only; manual input still works.
		S.ActorC = SpawnPoint(S, Base + FVector(0.f, 0.f, 500.f));
		S.Acf->SuppressCameraAssistance(S.ActorC.Get());
		S.Acf->SuppressCameraAssistance(S.ActorC.Get());
		S.Value = AcfYaw(S.Acf.Get());
		return true;
	});
	MoveFor(Steps, 30, Along(90.f, 400.f));
	Steps.Add([SecondAcf](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Same source twice: one suppression"), S.Acf->GetDirector()->GetNumAssistanceSuppressions(), 1);
		S.Test->TestTrue(FString::Printf(TEXT("Suppressed: yaw held (%s)"), *DescribeAssist(S.Acf.Get())),
			AssistDebug(S.Acf.Get()).AssistanceReason == EACFCameraAssistanceReason::SuppressedBySource && FMath::IsNearlyEqual(AcfYaw(S.Acf.Get()), static_cast<float>(S.Value), 1e-3f));
		S.Test->TestFalse(TEXT("Player 2 not suppressed by player 1's source"), SecondAcf(S)->IsCameraAssistanceSuppressed());
		S.Acf->AddCameraInput(FVector2D(5.0, 0.0));
		S.bAssistInputThisFrame = true;
		return true;
	});
	WaitFrames(Steps, 1);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(FString::Printf(TEXT("Manual input works while assistance is suppressed (%.3f -> %.3f)"), S.Value, AcfYaw(S.Acf.Get())),
			FMath::IsNearlyEqual(FRotator::NormalizeAxis(AcfYaw(S.Acf.Get()) - static_cast<float>(S.Value)), 5.f, 1e-2f));
		S.ActorC->Destroy(); // the source goes away without releasing
		return true;
	});
	MoveFor(Steps, 2, Along(90.f, 400.f));
	Steps.Add([](FScenario& S)
	{
		S.Test->TestFalse(TEXT("Destroyed source: suppression released"), S.Acf->IsCameraAssistanceSuppressed());
		S.Test->TestTrue(FString::Printf(TEXT("Released: waits again (%s)"), *DescribeAssist(S.Acf.Get())), AssistDebug(S.Acf.Get()).AssistanceReason == EACFCameraAssistanceReason::RecenterDelay);
		// PCM recreated while a source suppresses: the suppression belongs to the source, not to the PCM.
		S.ActorC = SpawnPoint(S, Base + FVector(0.f, 0.f, 600.f));
		S.Acf->SuppressCameraAssistance(S.ActorC.Get());
		APlayerCameraManager* Old = S.PCM();
		S.OldCameraManager = Old;
		S.Controller->SpawnPlayerCameraManager();
		if (S.PCM() != Old)
		{
			Old->Destroy();
		}
		return S.Test->TestTrue(TEXT("A new PCM"), S.PCM() != S.OldCameraManager.Get());
	});
	MoveFor(Steps, 20, Along(90.f, 400.f));
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("New PCM: still suppressed by the source"), S.Acf->IsCameraAssistanceSuppressed()
			&& AssistDebug(S.Acf.Get()).AssistanceReason == EACFCameraAssistanceReason::SuppressedBySource);
		S.Test->TestNotNull(TEXT("New PCM: ACF output attached"), S.PCM()->FindCameraModifierByClass(UACFCameraOutputModifier::StaticClass()));
		S.Test->TestTrue(TEXT("Release"), S.Acf->ReleaseCameraAssistanceSuppression(S.ActorC.Get()));
		S.Value = AcfYaw(S.Acf.Get());
		return true;
	});
	MoveFor(Steps, 60, Along(150.f, 400.f)); // a 60 degree turn from about 90
	Steps.Add([SecondAcf](FScenario& S)
	{
		S.Test->TestTrue(FString::Printf(TEXT("New PCM: assistance continues after the release (%.2f -> %s)"), S.Value, *DescribeAssist(S.Acf.Get())),
			AcfYaw(S.Acf.Get()) > S.Value + 5.f && S.PCM()->GetCameraRotation().Yaw > static_cast<float>(S.Value) + 5.f);
		// Player 2 holds a suppression and is removed: nothing of it reaches player 1.
		SecondAcf(S)->SuppressCameraAssistance(S.ActorB.Get());
		ACFTestEnvironment::RemoveLocalPlayer(S.SecondPlayer.Get());
		return true;
	});
	MoveFor(Steps, 10, Along(150.f, 400.f));
	Steps.Add([](FScenario& S)
	{
		S.Test->TestFalse(TEXT("Player 2 removed: its local player is gone"), S.SecondPlayer.IsValid() && S.World()->GetGameInstance()->GetLocalPlayers().Contains(S.SecondPlayer.Get()));
		S.Test->TestFalse(TEXT("Player 1 not suppressed by player 2's source"), S.Acf->IsCameraAssistanceSuppressed());
		S.Test->TestTrue(FString::Printf(TEXT("Debugger matches the camera every frame (%d frames checked, %d mismatches)"), S.AssistChecked, S.AssistMismatches),
			S.AssistChecked > 150 && S.AssistMismatches == 0);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

// Lock-On / Finisher（隠れていた Rig は進まず、戻った瞬間に回らない）・Combat 中も補助・Sequencer の Camera Cut 中は停止。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistLockOnSequencerTest, "ActionCameraFramework.Sequencer.Assistance.LockOnFinisherCombatAndSequencer", ACFSeqTest::Flags)
bool FACFAssistLockOnSequencerTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Mover = SpawnPoint(S, Base);
		S.ActorB = SpawnPoint(S, Base + FVector(1500.f, 300.f, 0.f)); // enemy
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.Mover.Get();
		S.AssistHandle = S.Acf->RequestCamera(MakeAssistPreset(S, /*bWithCombat*/ true), Params);
		return true;
	});
	WaitFrames(Steps, 3);
	MoveFor(Steps, 60, Along(60.f, 400.f));
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(FString::Printf(TEXT("Assisting before the lock-on (%s)"), *DescribeAssist(S.Acf.Get())), AssistDebug(S.Acf.Get()).AssistanceState == EACFCameraAssistanceState::Assisting);
		// Lock-On: a higher request with the Lock-On node (the orbit request stays, hidden).
		UACFCameraPreset* LockOn = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		LockOn->Nodes.Add(NewObject<UACFCameraNode_LockOn>(LockOn));
		LockOn->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		LockOn->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		S.Keep.Emplace(LockOn);
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.Mover.Get();
		Params.LookAtTarget.Actor = S.ActorB.Get();
		Params.Priority = 50;
		S.Value = AcfYaw(S.Acf.Get());
		S.Handle = S.Acf->RequestCamera(LockOn, Params);
		return true;
	});
	MoveFor(Steps, 60, Along(60.f, 400.f));
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Lock-On in control"), S.Acf->IsActiveCamera(S.Handle));
		S.Acf->ReleaseCamera(S.Handle);
		return true;
	});
	WaitFrames(Steps, 1);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Orbit back after the lock-on"), S.Acf->IsActiveCamera(S.AssistHandle));
		S.Test->TestTrue(FString::Printf(TEXT("No rotation jump on return (%.3f -> %.3f)"), S.Value, AcfYaw(S.Acf.Get())), FMath::IsNearlyEqual(AcfYaw(S.Acf.Get()), static_cast<float>(S.Value), 1e-3f));
		S.Test->TestTrue(FString::Printf(TEXT("Waits from the start after the lock-on (%s)"), *DescribeAssist(S.Acf.Get())),
			AssistDebug(S.Acf.Get()).AssistanceReason == EACFCameraAssistanceReason::RecenterDelay);
		// Finisher (camera sequence, 0.5 s).
		S.Value = AcfYaw(S.Acf.Get());
		S.Finisher = S.Acf->GetDirector()->PlayFinisherPreset(MakeFixedPreset(S, CineLoc, CineRot, CineFov), S.Mover.Get(), S.ActorB.Get(), 0.5f,
			FACFCameraBlendSettings::MakeCut(), FACFCameraBlendSettings::MakeCut(), 100);
		return true;
	});
	MoveFor(Steps, 20, Along(60.f, 400.f));
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(TEXT("Finisher in control"), S.Acf->GetDirector()->IsSequencePlaying(S.Finisher) && !S.Acf->IsActiveCamera(S.AssistHandle));
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return !S.Acf->GetDirector()->IsSequencePlaying(S.Finisher) && S.Acf->IsActiveCamera(S.AssistHandle); });
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(FString::Printf(TEXT("After the finisher: no jump, waiting (%.3f -> %s)"), S.Value, *DescribeAssist(S.Acf.Get())),
			FMath::IsNearlyEqual(AcfYaw(S.Acf.Get()), static_cast<float>(S.Value), 1e-3f) && AssistDebug(S.Acf.Get()).AssistanceState != EACFCameraAssistanceState::Assisting);
		// Combat: the assistance keeps working (suppress it from game code if a combat should stop it).
		FACFCameraCombatState Combat;
		Combat.bInCombat = true;
		S.Acf->GetDirector()->SetCombatState(Combat);
		S.Value = AcfYaw(S.Acf.Get());
		return true;
	});
	MoveFor(Steps, 90, Along(150.f, 400.f));
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(FString::Printf(TEXT("In combat: still assisting (%.2f -> %s)"), S.Value, *DescribeAssist(S.Acf.Get())),
			AssistDebug(S.Acf.Get()).bHasCombat && AssistDebug(S.Acf.Get()).AssistanceState == EACFCameraAssistanceState::Assisting && AcfYaw(S.Acf.Get()) > S.Value + 5.f);
		S.Acf->GetDirector()->SetCombatState(FACFCameraCombatState());
		// Sequencer camera cut: ACF evaluates in the background (bypassed), the assistance stops.
		S.ActorA = SpawnCineCamera(S, CineLoc, CineRot);
		S.SequenceA = MakeSequence(S, S.ActorA.Get(), 1.0);
		SetReturnBlend(S, 0.f);
		Player(S.SequenceA)->Play();
		return true;
	});
	WaitFrames(Steps, 3);
	Steps.Add([](FScenario& S) { S.Value = AcfYaw(S.Acf.Get()); return S.Test->TestTrue(TEXT("Sequencer camera on screen"), S.Acf->IsOutputBypassed()); });
	MoveFor(Steps, 30, Along(-120.f, 400.f)); // a direction the camera would turn to (90 degrees off)
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(FString::Printf(TEXT("Sequencer: not assisted while bypassed (%.3f -> %s)"), S.Value, *DescribeAssist(S.Acf.Get())),
			AssistDebug(S.Acf.Get()).AssistanceReason == EACFCameraAssistanceReason::ViewNotShown && FMath::IsNearlyEqual(AcfYaw(S.Acf.Get()), static_cast<float>(S.Value), 1e-3f));
		S.CameraNear(CineLoc, 0.1, TEXT("Sequencer camera shown"));
		return true;
	});
	RetryUntil(Steps, [](FScenario& S) { return !S.Acf->IsOutputBypassed(); });
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(FString::Printf(TEXT("After the sequence: waits from the start (%s)"), *DescribeAssist(S.Acf.Get())),
			AssistDebug(S.Acf.Get()).AssistanceState != EACFCameraAssistanceState::Assisting && FMath::IsNearlyEqual(AcfYaw(S.Acf.Get()), static_cast<float>(S.Value), 1e-3f));
		return true;
	});
	MoveFor(Steps, 60, Along(-120.f, 400.f));
	Steps.Add([](FScenario& S)
	{
		S.Test->TestTrue(FString::Printf(TEXT("After the sequence: assisting again (%s)"), *DescribeAssist(S.Acf.Get())), AssistDebug(S.Acf.Get()).AssistanceState == EACFCameraAssistanceState::Assisting);
		S.Test->TestTrue(FString::Printf(TEXT("Debugger matches the camera every frame (%d frames checked, %d mismatches)"), S.AssistChecked, S.AssistMismatches),
			S.AssistChecked > 100 && S.AssistMismatches == 0);
		return true;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}
// Phase 7B-2: camera input through the subsystem with the real PCM and World: stick drift never blocks the assistance,
// clear input does (for that player only), and Set Global Time Dilation reaches the input rate (real time on player 1).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAssistInputWorldTest, "ActionCameraFramework.Sequencer.Assistance.InputNoiseDilationAndPlayers", ACFSeqTest::Flags)
bool FACFAssistInputWorldTest::RunTest(const FString& Parameters)
{
	using namespace ACFSeqTest;
	ACFTestEnvironment::ExpectHostWidgetErrorForExtraLocalPlayers(*this);
	auto SecondAcf = [](FScenario& S) { return S.SecondPlayer.IsValid() ? S.SecondPlayer->GetSubsystem<UACFCameraSubsystem>() : nullptr; };
	const TSharedRef<double> YawB = MakeShared<double>(0.0);
	TArray<FStep> Steps;
	Steps.Add([](FScenario& S)
	{
		S.Mover = SpawnPoint(S, Base);
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.Mover.Get();
		UACFCameraPreset* Preset = MakeAssistPreset(S);
		CastChecked<UACFCameraNode_OrbitalFollow>(Preset->Nodes[0].Get())->bInputRateIgnoresTimeDilation = true;
		S.AssistHandle = S.Acf->RequestCamera(Preset, Params);
		FString Error;
		S.SecondPlayer = S.World()->GetGameInstance()->CreateLocalPlayer(FPlatformUserId::CreateFromInternalId(1), Error, true);
		return S.Test->TestTrue(FString::Printf(TEXT("Second local player (%s)"), *Error), S.SecondPlayer.IsValid());
	});
	WaitFrames(Steps, 5);
	Steps.Add([SecondAcf](FScenario& S)
	{
		UACFCameraSubsystem* Acf2 = SecondAcf(S);
		if (!S.Test->TestNotNull(TEXT("Player 2 ACF"), Acf2) || !S.SecondPlayer->GetPlayerController(S.World()))
		{
			return false;
		}
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = S.Mover.Get(); // both players follow the same moving target
		S.HandleB = Acf2->RequestCamera(MakeAssistPreset(S), Params);
		return true;
	});
	WaitFrames(Steps, 3);
	MoveFor(Steps, 90, Along(90.f, 400.f));
	// Stick drift (2 deg/s) on player 1 every frame for 1 s while moving: noise.
	Steps.Add([](FScenario& S) { S.Counter = 0; S.Value = 0.0; return true; });
	Steps.Add([](FScenario& S)
	{
		if (S.Counter > 0 && AssistDebug(S.Acf.Get()).AssistanceReason == EACFCameraAssistanceReason::ManualInput)
		{
			S.Value += 1.0;
		}
		S.Acf->SetCameraInputRate(FVector2D(2.0, 0.0));
		if (AActor* Mover = S.Mover.Get())
		{
			Mover->SetActorLocation(Mover->GetActorLocation() + Along(90.f, 400.f) * FixedDt);
		}
		S.bRetry = ++S.Counter < 60;
		return true;
	});
	WaitFrames(Steps, 1);
	Steps.Add([](FScenario& S)
	{
		S.Test->TestEqual(TEXT("Drift through the subsystem: never manual input"), S.Value, 0.0);
		S.Test->TestTrue(FString::Printf(TEXT("Player 1 still assisting under drift (%s)"), *DescribeAssist(S.Acf.Get())),
			AssistDebug(S.Acf.Get()).AssistanceState == EACFCameraAssistanceState::Assisting);
		S.Acf->SetCameraInputRate(FVector2D(20.0, 0.0)); // one clear stick frame, player 1 only
		return true;
	});
	WaitFrames(Steps, 1);
	Steps.Add([SecondAcf](FScenario& S)
	{
		// The check runs a frame or two after the input frame: the clear input restarted player 1's wait (drift never did).
		S.Test->TestTrue(FString::Printf(TEXT("Player 1: clear input restarted the wait (%s)"), *DescribeAssist(S.Acf.Get())),
			AssistDebug(S.Acf.Get()).AssistanceReason == EACFCameraAssistanceReason::RecenterDelay && AssistDebug(S.Acf.Get()).AssistanceWaitTime < 0.06f);
		S.Test->TestTrue(FString::Printf(TEXT("Player 2: not affected, still assisting (%s)"), *DescribeAssist(SecondAcf(S))),
			AssistDebug(SecondAcf(S)).AssistanceState == EACFCameraAssistanceState::Assisting);
		UGameplayStatics::SetGlobalTimeDilation(S.World(), 0.25f); // slow motion
		return true;
	});
	WaitFrames(Steps, 2);
	Steps.Add([SecondAcf, YawB](FScenario& S)
	{
		S.Value = AcfYaw(S.Acf.Get());
		*YawB = AcfYaw(SecondAcf(S));
		S.Acf->SetCameraInputRate(FVector2D(60.0, 0.0));
		SecondAcf(S)->SetCameraInputRate(FVector2D(60.0, 0.0));
		return true;
	});
	WaitFrames(Steps, 1);
	Steps.Add([SecondAcf, YawB](FScenario& S)
	{
		const float Step1 = FRotator::NormalizeAxis(AcfYaw(S.Acf.Get()) - static_cast<float>(S.Value));
		const float Step2 = FRotator::NormalizeAxis(AcfYaw(SecondAcf(S)) - static_cast<float>(*YawB));
		UGameplayStatics::SetGlobalTimeDilation(S.World(), 1.f); // restore before anything can fail
		S.Test->TestTrue(FString::Printf(TEXT("Slow motion 0.25, real-time input rate: 60 deg/s x real 1/60 s = 1 (%.4f)"), Step1), FMath::IsNearlyEqual(Step1, 1.f, 1e-3f));
		S.Test->TestTrue(FString::Printf(TEXT("Slow motion 0.25, game-time input rate: 60 deg/s x game 1/240 s = 0.25 (%.4f)"), Step2), FMath::IsNearlyEqual(Step2, 0.25f, 1e-3f));
		ACFTestEnvironment::RemoveLocalPlayer(S.SecondPlayer.Get());
		return true;
	});
	WaitFrames(Steps, 2);
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, MoveTemp(Steps)));
	return true;
}

#endif // !UE_BUILD_SHIPPING

#endif // WITH_DEV_AUTOMATION_TESTS
