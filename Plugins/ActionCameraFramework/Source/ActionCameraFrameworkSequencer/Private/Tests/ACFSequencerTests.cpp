#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFSequencerBlueprintLibrary.h"
#include "ACFSequencerCameraSubsystem.h"
#include "ACFSequencerTestListener.h"
#include "ACFSequencerTestProbeModifier.h"
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
				Cleanup();
				return true;
			}
			State->Sample();
			State->bRetry = false;
			if (!Steps[Index](*State))
			{
				Cleanup();
				return true;
			}
			if (State->bRetry)
			{
				if (++RetryFrames > 1800)
				{
					State->Test->AddError(FString::Printf(TEXT("Scenario step %d timed out (phase history: %s)."), Index, *State->PhaseHistory()));
					Cleanup();
					return true;
				}
				return false;
			}
			RetryFrames = 0;
			if (++Index >= Steps.Num())
			{
				Cleanup();
				return true;
			}
			return false;
		}

	private:
		void Cleanup()
		{
			FScenario& S = *State;
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
					GameInstance->RemoveLocalPlayer(Second);
				}
			}
			if (S.Acf.IsValid())
			{
				S.Test->TestEqual(TEXT("Cleanup: no output suppression left behind by the sequencer bridge"), S.Acf->GetNumOutputSuppressions(), 0);
				S.Acf->GetDirector()->StopAllSequences(true);
				S.Acf->ReleaseAllCameras(true);
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
	// 2 人目の PlayerController は GameMode の PlayerController Class で生成され、エンジンの順序上 BeginPlay の時点では
	// まだ LocalPlayer が付いていない。ホストプロジェクト（REBoarGetch の BP_PC_Lobby）は BeginPlay で HUD を CreateWidget
	// するため UMG がエラーを出す。ACF とは無関係（ACF は UMG を使わない）なので、このメッセージだけを無視する。
	// 出ない GameMode もあるので「出なくても失敗にしない」（Occurrences < 0）。ACF の検証項目は変えない。
	AddExpectedErrorPlain(TEXT("CreateWidget cannot be used on Player Controller with no attached player"), EAutomationExpectedErrorFlags::Contains, -1);
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

#endif // WITH_DEV_AUTOMATION_TESTS
