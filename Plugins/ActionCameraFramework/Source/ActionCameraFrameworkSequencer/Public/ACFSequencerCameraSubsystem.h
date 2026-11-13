#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ACFSequencerTypes.h"
#include "ACFSequencerCameraSubsystem.generated.h"

class AActor;
class ALevelSequenceActor;
class APlayerCameraManager;
class APlayerController;
class ULevel;
class ULocalPlayer;
class UACFCameraSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FACFSequencerCameraStartedSignature, APlayerController*, PlayerController, ALevelSequenceActor*, SequenceActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FACFSequencerCameraEndedSignature, APlayerController*, PlayerController, ALevelSequenceActor*, SequenceActor, EACFSequencerReturnKind, ReturnKind);

/**
 * Level Sequencer と ACF の橋渡し（ワールドごと。状態はローカルプレイヤーごとに独立）。
 *
 * 役割は「誰が画面を握っているか」を毎フレーム確定し、ACF に必要な最小限の指示を出すことだけ:
 *  - Sequencer を ACF 独自の Sequence へ変換しない。Camera Cut Track / PCM の ViewTarget はエンジンの動作のまま。
 *  - ACF の要求・Rig の状態には触れない（ゲームプレイカメラを破棄しない）。ACF は Sequencer 中も裏で評価を続ける。
 *
 * カメラの所有権（UE 5.8 の実装に基づく）:
 *  - Camera Cut は PlayerController::SetViewTarget で PCM の ViewTarget を差し替える。対象の PlayerController は
 *    Level Sequence Actor の Owner（PlayerController のとき）、無ければ最初のローカルプレイヤー。
 *  - UE 5.8 の PCM は ViewTarget が Camera Actor（Cine Camera を含む）でも Camera Modifier を適用するので、
 *    何もしなければ ACF が Sequencer のカメラを上書きする。そこで Sequencer のカメラを ACF の「外部 ViewTarget」として
 *    登録する（UACFCameraSubsystem::SetExternalViewTargets）。ACF はその ViewTarget にだけ出力しない。
 *  - 判定が ViewTarget 単位なので、Camera Cut の Ease In / Ease Out（PCM の ViewTarget Blend）の間も、ゲームプレイ側の
 *    ViewTarget には ACF が 100% で乗り続ける。2 つを混ぜるのは PCM のブレンドだけで、ACF の Weight は動かさない
 *    （動かすと PCM のブレンドと二重になる）。
 *
 * 検出は PCM の状態（ViewTarget / PendingViewTarget / bClientSimulatingViewTarget = Camera Cut が立てるフラグ）と、
 * 追跡している Level Sequence Actor の再生状態から行う。Sequence の終わり方（最後まで再生・Stop・途中停止・
 * Actor の破棄・レベル遷移）に依存せず、「ViewTarget がゲームプレイ側へ戻った」ことで復帰を処理する。
 */
UCLASS()
class ACTIONCAMERAFRAMEWORKSEQUENCER_API UACFSequencerCameraSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** WorldContextObject のワールドのサブシステム（ゲームワールド以外では null）。 */
	static UACFSequencerCameraSubsystem* Get(const UObject* WorldContextObject);

	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ UWorldSubsystem
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ FTickableGameObject（カメラ更新より前に毎フレーム 1 回。評価直前にも ACF 側のフックからもう一度確定する）
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableWhenPaused() const override;
	virtual TStatId GetStatId() const override;

	/** PlayerController のローカルプレイヤーについての現在の状態。 */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Sequencer", meta = (DisplayName = "Get ACF Sequencer Camera State (Subsystem)"))
	FACFSequencerCameraState GetCameraState(const APlayerController* PlayerController) const;

	/** Sequencer の Camera Cut がこのプレイヤーの画面を握っているか（ブレンド中を含む）。 */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Sequencer")
	bool IsSequencerControllingCamera(const APlayerController* PlayerController) const;

	/** プレイヤー単位の設定の上書き（解除するまで有効）。 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequencer")
	void SetPlayerOptions(APlayerController* PlayerController, const FACFSequencerCameraOptions& Options);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequencer")
	void ClearPlayerOptions(APlayerController* PlayerController);

	/** Level Sequence Actor 単位の設定の上書き（Actor が破棄されると自動で消える）。 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequencer")
	void SetSequenceOptions(ALevelSequenceActor* SequenceActor, const FACFSequencerCameraOptions& Options);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequencer")
	void ClearSequenceOptions(ALevelSequenceActor* SequenceActor);

	/** いま有効な設定（SequenceActor が null ならプレイヤー単位 → Project Settings）。 */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Sequencer")
	FACFSequencerCameraOptions GetEffectiveOptions(const APlayerController* PlayerController, const ALevelSequenceActor* SequenceActor) const;

	/**
	 * Level Sequence Actor を追跡対象に加える。レベルに置かれた Actor・スポーンされた Actor（Create Level Sequence Player を
	 * 含む）・ストリーミングで追加された Actor は自動で追跡されるので、通常は呼ぶ必要はない。
	 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequencer")
	void RegisterSequenceActor(ALevelSequenceActor* SequenceActor);

	/** 状態を今すぐ確定する（Play / Stop を呼んだ直後に同じフレームで状態を読みたいとき）。通常は不要。 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequencer")
	void RefreshNow();

	/** Phase 6B / B-6: notifications detected but not delivered yet (delivered outside the camera update). */
	int32 GetNumPendingEvents() const { return PendingEvents.Num(); }

	/** Testing only (B-6): queues a notification exactly as a detected change would (delivered at the next delivery point). */
	void QueueNotificationForTesting(bool bStarted, ULocalPlayer* LocalPlayer, APlayerController* Controller, ALevelSequenceActor* Sequence, EACFSequencerReturnKind ReturnKind)
	{
		QueueEvent(bStarted, LocalPlayer, Controller, Sequence, ReturnKind);
	}

	/**
	 * 配信のタイミング（Phase 6B / B-6）: 開始・終了は状態の変化を検出した時点でキューに入り、カメラ更新の外で配信される。
	 *   - World の Tick で検出した変化: このサブシステムの Tick の終わり（同じフレーム、カメラ更新より前）。
	 *   - カメラ更新の直前（ACF の評価前フック）で検出した変化: 同じフレームのカメラ更新の後（OnWorldPostActorTick）。
	 * ハンドラは PCM の更新中には呼ばれないので、SetViewTarget などを安全に呼べる。ACF の内部処理（出力の抑制・外部 ViewTarget・
	 * Return Blend の開始）は従来どおり検出した時点で行うので、カメラの結果は変わらない。順序は検出順（開始 → 終了）で、
	 * 各変化につき 1 回。ローカルプレイヤーが削除された・別の World になったイベントは配信しない。
	 */
	/** Sequencer の Camera Cut がプレイヤーの画面を握り始めた。 */
	UPROPERTY(BlueprintAssignable, Category = "Action Camera|Sequencer")
	FACFSequencerCameraStartedSignature OnSequencerCameraStarted;

	/** 画面がゲームプレイカメラへ戻った（戻り方つき）。ACF の復帰ブレンドはこの時点で開始済み。 */
	UPROPERTY(BlueprintAssignable, Category = "Action Camera|Sequencer")
	FACFSequencerCameraEndedSignature OnSequencerCameraEnded;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** ローカルプレイヤー 1 人分の状態。プレイヤー間で共有するものは無い。 */
	struct FPlayerState
	{
		TWeakObjectPtr<ULocalPlayer> LocalPlayer;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<APlayerCameraManager> CameraManager;
		/** Sequencer が握っていない間の ViewTarget（= 戻り先）。 */
		TWeakObjectPtr<AActor> GameplayViewTarget;
		TWeakObjectPtr<AActor> SequencerViewTarget;
		TWeakObjectPtr<ALevelSequenceActor> ControllingSequence;
		EACFSequencerCameraPhase Phase = EACFSequencerCameraPhase::None;
		int32 NumActiveSequences = 0;
		/** このサブシステムが Source となって ACF の出力を抑制中か（解除は必ず同じ Source で行う）。 */
		bool bSuppressing = false;
		/** 抑制の理由が Camera Cut（Modifier を通るカメラ）か。false なら bSuppressOutputWhilePlaying。 */
		bool bSuppressingForCameraCut = false;
		/** bSuppressOutputWhilePlaying で抑制に入ったときのフェード秒数（戻るときも同じ時間を使う）。 */
		float WholeSequenceBlendTime = 0.f;
		bool bHasOptionsOverride = false;
		FACFSequencerCameraOptions OptionsOverride;
	};

	FPlayerState* FindState(const ULocalPlayer* LocalPlayer);
	const FPlayerState* FindState(const APlayerController* PlayerController) const;
	FPlayerState& FindOrAddState(ULocalPlayer* LocalPlayer);

	void RefreshAll();
	void RefreshPlayer(ULocalPlayer* LocalPlayer);
	void ApplySuppression(FPlayerState& State, UACFCameraSubsystem* Acf, bool bWant, bool bForCameraCut, float BlendTime);
	void ApplyControlRotationPolicy(const FPlayerState& State, UACFCameraSubsystem* Acf, const FACFSequencerCameraOptions& Options) const;
	FACFSequencerCameraOptions ResolveOptions(const FPlayerState* State, const ALevelSequenceActor* SequenceActor) const;
	/** Camera Cut が対象にする PlayerController（エンジンの GetPlaybackController と同じ規則）。 */
	APlayerController* ResolveSequenceController(const ALevelSequenceActor& SequenceActor) const;

	void HandleActorSpawned(AActor* Actor);
	void HandleLevelAdded(ULevel* Level, UWorld* World);
	void HandlePreEvaluate(UACFCameraSubsystem& Subsystem, float DeltaTime);
	void LogStateChange(const FString& Message) const;

	/** B-6: a start / end waiting for a safe point (outside the camera update). */
	struct FPendingEvent
	{
		bool bStarted = false;
		TWeakObjectPtr<ULocalPlayer> LocalPlayer;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<ALevelSequenceActor> Sequence;
		EACFSequencerReturnKind ReturnKind = EACFSequencerReturnKind::Cut;
	};
	void QueueEvent(bool bStarted, ULocalPlayer* LocalPlayer, APlayerController* Controller, ALevelSequenceActor* Sequence, EACFSequencerReturnKind ReturnKind);
	void DispatchPendingEvents();
	void HandleWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	TArray<FPlayerState> PlayerStates;
	TArray<TWeakObjectPtr<ALevelSequenceActor>> SequenceActors;
	TMap<TWeakObjectPtr<const ALevelSequenceActor>, FACFSequencerCameraOptions> SequenceOptions;

	FDelegateHandle ActorSpawnedHandle;
	FDelegateHandle LevelAddedHandle;
	FDelegateHandle PreEvaluateHandle;
	FDelegateHandle PostActorTickHandle;
	TArray<FPendingEvent, TInlineAllocator<4>> PendingEvents;
	bool bDispatchingEvents = false;
};
