#include "Player/BoarPlayerController.h"
#include "REBoarGetch.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Component/HealthComponent.h"
#include "Component/CapturePresentationComponent.h"
#include "Component/StageClearPresentationComponent.h"
#include "Component/GameOverPresentationComponent.h"
#include "Component/BoarGameplayCameraComponent.h"
#include "Component/GadgetComponent.h"
#include "Core/BoarGameMode.h"
#include "Player/BoarPlayerCharacter.h"
#include "Stage/StageConfig.h"
#include "UI/BoarHUDWidget.h"
#include "UI/BoarResultWidget.h"
#include "UI/BoarGameOverWidget.h"
#include "GameMapsSettings.h"
#include "Misc/PackageName.h"
#include "UI/BoarUIFlowComponent.h"
#include "Core/BoarFacilityGameMode.h"
#include "Stage/HubPortal.h"
#include "Stage/StageEntrance.h"
#include "Stage/BoarLevelTransitionLibrary.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** Input Mapping Contextの優先度。数値が大きいほど優先され、同じキーを後から上書きできる。 */
	constexpr int32 DefaultMappingPriority = 0;
	constexpr int32 GlobalMappingPriority = 10;  // Pause等、どの画面でも有効な操作
	constexpr int32 FacilityMappingPriority = 15; // Lobby・施設だけの操作（Return等）

	/** Result・GameOver画面のZOrder。通常HUDより前面に出す。 */
	constexpr int32 EndScreenZOrder = 100;
}

ABoarPlayerController::ABoarPlayerController()
{
	CapturePresentation = CreateDefaultSubobject<UCapturePresentationComponent>(TEXT("CapturePresentation"));
	StageClearPresentation = CreateDefaultSubobject<UStageClearPresentationComponent>(TEXT("StageClearPresentation"));
	GameOverPresentation = CreateDefaultSubobject<UGameOverPresentationComponent>(TEXT("GameOverPresentation"));
	UIFlow = CreateDefaultSubobject<UBoarUIFlowComponent>(TEXT("UIFlow"));
	GameplayCamera = CreateDefaultSubobject<UBoarGameplayCameraComponent>(TEXT("GameplayCamera"));
}

void ABoarPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// RetryによるLevel再読み込みではControllerの入力状態が残る場合があるため、
	// Mapping ContextやHUDを準備する前に、必ずゲーム操作可能な初期状態へ戻す。
	RestoreGameplayInputState();

	if (const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
		SetStageInputBlocked(!Mode->CanAdvanceStage());

	// Mapping Contextは画面と入力デバイスを持つローカルControllerにだけ登録する。
	// Dedicated Serverや初期化途中ではLocalPlayerが存在しないため、先にnullを許容する。
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
		: nullptr;

	if (InputSubsystem && DefaultMappingContext)
	{
		// Priority 0をゲームプレイ入力の基準レイヤーとして使用する。
		// 将来、メニュー等を追加する場合は、より高いPriorityのContextで上書きできる。
		AddOwnedMappingContext(DefaultMappingContext, DefaultMappingPriority);
		UE_LOG(LogREBoarGetch, Log, TEXT("[Input] MappingContextを追加しました: %s"), *GetNameSafe(DefaultMappingContext));
	}
	else if (IsLocalController())
	{
		UE_LOG(LogREBoarGetch, Warning,
			TEXT("[Input] MappingContextを登録できませんでした。LocalPlayer=%s Subsystem=%s Context=%s"),
			*GetNameSafe(LocalPlayer), *GetNameSafe(InputSubsystem), *GetNameSafe(DefaultMappingContext));
	}

	AddOwnedMappingContext(GlobalMappingContext, GlobalMappingPriority);

	// Lobby・施設・施設TestのReturnで使用。通常Stageには登録しません。
	if (GetWorld() && !GetWorld()->GetAuthGameMode<ABoarGameMode>())
		AddOwnedMappingContext(FacilityMappingContext, FacilityMappingPriority);

	// Mapping Context登録後にHUDを生成し、現在Possess中のCharacterとGameModeへ接続する。
	CreatePlayerHUD();
}

void ABoarPlayerController::RestoreGameplayInputState()
{
	// PlayerControllerはLevel再読み込み後も再利用される場合があるため、Result表示時に変更した
	// ignore-input、押下中キー、Mouse/UI入力状態を明示的にゲーム開始状態へ戻す。
	bGameplayInputBlocked = false;
	bLevelTransitionRequested = false;
	bInitializationFailed = false;
	bGameOverUIUnavailable = false;
	bIsGadgetModifierHeld = false;

	// Result表示時に設定した移動・視点ロックを解除する。
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();

	// Level切替前に押されていたボタンを次のLevelへ持ち越さない。
	FlushPressedKeys();

	// Result用のUI Onlyから通常操作用のGame Onlyへ戻し、マウスカーソルを隠す。
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	SetShowMouseCursor(false);
}

void ABoarPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// RespawnやPawn切替後も古いCharacterのDelegateを購読し続けないよう、
	// HUDの監視対象を新しくPossessしたCharacterへ付け替える。
	ABoarPlayerCharacter* PlayerCharacter = Cast<ABoarPlayerCharacter>(InPawn);
	BindPlayerHealth(PlayerCharacter);
	BindPlayerStamina(PlayerCharacter);
	BindPlayerGadgets(PlayerCharacter);
}

void ABoarPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UIFlow)
		UIFlow->Shutdown();

	while (!OwnedMappingContexts.IsEmpty())
		RemoveOwnedMappingContext(OwnedMappingContexts.Last());
	// Controller破棄後にDynamic Delegateからコールバックされないよう、
	// BeginPlay/CreatePlayerHUD/OnPossessで登録した購読をすべて解除する。
	if (ABoarGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr)
	{
		GameMode->OnCapturedBoarCountChanged.RemoveDynamic(
			this,
			&ABoarPlayerController::HandleCapturedBoarCountChanged);
	}

	if (ObservedHealthComponent)
	{
		ObservedHealthComponent->OnHealthChanged.RemoveDynamic(
			this,
			&ABoarPlayerController::HandleHealthChanged);
		ObservedHealthComponent = nullptr;
	}

	if (ObservedStaminaCharacter)
	{
		ObservedStaminaCharacter->OnStaminaChanged.RemoveDynamic(
			this,
			&ABoarPlayerController::HandleStaminaChanged);
		ObservedStaminaCharacter = nullptr;
	}

	if (ObservedGadgetComponent)
	{
		ObservedGadgetComponent->OnGadgetLoadoutChanged.RemoveDynamic(
			this,
			&ABoarPlayerController::HandleGadgetLoadoutChanged);
		ObservedGadgetComponent = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void ABoarPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Project側のInputComponent classがEnhanced Inputでない場合、ActionをBindできない。
	// Cast失敗を許容しておくことで、設定ミスでもController生成自体は継続できる。
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (EnhancedInput == nullptr)
	{
		UE_LOG(LogREBoarGetch, Warning, TEXT("[Input] InputComponentがUEnhancedInputComponentではありません: %s"),
			*GetNameSafe(InputComponent));
		return;
	}

	if (MoveAction)
	{
		// Axis2Dの値が変化している間、毎Frame Moveへ移動方向を渡す。
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ABoarPlayerController::Move);
	}

	if (InteractAction)
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this,
			&ABoarPlayerController::InteractWithFacility);

	if (LookAction)
	{
		// Mouse/GamepadのAxis2D入力が続く間、Camera入力をCharacterへ渡す。
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &ABoarPlayerController::Look);
	}

	if (JumpAction)
	{
		// Startedは押した瞬間だけ発火し、Character側でジャンプ可能回数を判定する。
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &ABoarPlayerController::JumpStarted);

		// Completedは離した瞬間にJump入力を止め、長押しジャンプを正しく終了させる。
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &ABoarPlayerController::JumpCompleted);
	}

	if (GadgetAction)
	{
		// Started/Completed/Canceled を分けて、ガジェット使用ライフサイクルを管理する。
		EnhancedInput->BindAction(GadgetAction, ETriggerEvent::Started, this, &ABoarPlayerController::GadgetStarted);
		EnhancedInput->BindAction(GadgetAction, ETriggerEvent::Completed, this,
			&ABoarPlayerController::GadgetCompleted);
		EnhancedInput->BindAction(GadgetAction, ETriggerEvent::Canceled, this, &ABoarPlayerController::GadgetCompleted);
	}

	if (DashAction)
	{
		// Dashは押下中だけ継続するため、開始と終了を別のCharacter APIへ渡す。
		EnhancedInput->BindAction(DashAction, ETriggerEvent::Started, this, &ABoarPlayerController::DashStarted);
		EnhancedInput->BindAction(DashAction, ETriggerEvent::Completed, this, &ABoarPlayerController::DashCompleted);

		// フォーカス外れ等で入力がキャンセルされた場合も必ずダッシュ解除する。
		EnhancedInput->BindAction(DashAction, ETriggerEvent::Canceled, this, &ABoarPlayerController::DashCompleted);
	}

	if (GadgetModifierAction)
	{
		// Modifierの押下状態をControllerに保持し、Face Button単独入力との競合を避ける。
		EnhancedInput->BindAction(GadgetModifierAction, ETriggerEvent::Started, this,
			&ABoarPlayerController::GadgetModifierStarted);
		EnhancedInput->BindAction(GadgetModifierAction, ETriggerEvent::Completed, this,
			&ABoarPlayerController::GadgetModifierCompleted);
		EnhancedInput->BindAction(GadgetModifierAction, ETriggerEvent::Canceled, this,
			&ABoarPlayerController::GadgetModifierCompleted);
	}

	if (GadgetSlot1Action)
	{
		// Enhanced Inputへ引数付き関数を直接Bindしないため、各入口から0始まりのIndexへ変換する。
		EnhancedInput->BindAction(GadgetSlot1Action, ETriggerEvent::Started, this,
			&ABoarPlayerController::SwitchGadgetSlot1);
	}

	if (GadgetSlot2Action)
	{
		EnhancedInput->BindAction(GadgetSlot2Action, ETriggerEvent::Started, this,
			&ABoarPlayerController::SwitchGadgetSlot2);
	}

	if (GadgetSlot3Action)
	{
		EnhancedInput->BindAction(GadgetSlot3Action, ETriggerEvent::Started, this,
			&ABoarPlayerController::SwitchGadgetSlot3);
	}

	if (GadgetSlot4Action)
	{
		EnhancedInput->BindAction(GadgetSlot4Action, ETriggerEvent::Started, this,
			&ABoarPlayerController::SwitchGadgetSlot4);
	}

	if (PauseAction)
	{
		EnhancedInput->BindAction(PauseAction, ETriggerEvent::Started, this, &ABoarPlayerController::TogglePauseMenu);
	}

	if (UIBackAction)
	{
		EnhancedInput->BindAction(UIBackAction, ETriggerEvent::Started, this, &ABoarPlayerController::HandleMenuBack);
	}
}

void ABoarPlayerController::CreatePlayerHUD()
{
	// HUDはローカル画面専用。Server Controller、生成済み、BP Class未設定の場合は何もしない。
	if (!IsLocalController() || PlayerHUDWidget || PlayerHUDWidgetClass == nullptr)
		return;

	// Owning Playerをthisにすることで、Widgetが正しいLocalPlayer/Inputを参照できるようにする。
	PlayerHUDWidget = CreateWidget<UBoarHUDWidget>(this, PlayerHUDWidgetClass);
	if (PlayerHUDWidget == nullptr)
	{
		UE_LOG(LogREBoarGetch, Warning, TEXT("[HUD] PlayerのHUD Widgetを生成できませんでした"));
		return;
	}

	// 通常HUDはResultより低いZOrderに置き、SelfHitTestInvisibleでゲーム入力を遮らない。
	PlayerHUDWidget->AddToViewport(0);
	PlayerHUDWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (ABoarGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr)
	{
		// 捕獲数はGameModeが正として管理するため、変更通知だけを購読する。
		GameMode->OnCapturedBoarCountChanged.AddUniqueDynamic(
			this,
			&ABoarPlayerController::HandleCapturedBoarCountChanged);

		// Delegateは次回変更時まで発火しないため、生成直後の初期値は明示的に反映する。
		const UStageConfig* StageConfig = GameMode->GetStageConfig();
		HandleCapturedBoarCountChanged(
			GameMode->GetCapturedBoarCount(),
			StageConfig ? StageConfig->TargetCaptureCount : 0);
	}

	// HUDがOnPossessより後に生成された場合にも対応できるよう、現在のPawnへここでも接続する。
	BindPlayerHealth(GetBoarCharacter());
	BindPlayerStamina(GetBoarCharacter());
	BindPlayerGadgets(GetBoarCharacter());
}

void ABoarPlayerController::BindPlayerStamina(ABoarPlayerCharacter* PlayerCharacter)
{
	if (ObservedStaminaCharacter != PlayerCharacter)
	{
		// 破棄済み/非PossessのCharacterからスタミナ変更を受け取らないよう購読解除する。
		if (ObservedStaminaCharacter)
		{
			ObservedStaminaCharacter->OnStaminaChanged.RemoveDynamic(
				this,
				&ABoarPlayerController::HandleStaminaChanged);
		}

		ObservedStaminaCharacter = PlayerCharacter;
		if (ObservedStaminaCharacter)
		{
			// AddUniqueDynamicでOnPossess/CreatePlayerHUDの二経路から呼ばれても重複登録しない。
			ObservedStaminaCharacter->OnStaminaChanged.AddUniqueDynamic(
				this,
				&ABoarPlayerController::HandleStaminaChanged);
		}
	}

	// HUDの作り直しやRetry直後でも古い表示を残さないよう、現在値を即時反映する。
	if (ObservedStaminaCharacter)
	{
		HandleStaminaChanged(
			ObservedStaminaCharacter->GetCurrentStamina(),
			ObservedStaminaCharacter->GetMaxStamina(),
			ObservedStaminaCharacter->IsDashLockedByExhaustion());
	}
}

void ABoarPlayerController::BindPlayerGadgets(ABoarPlayerCharacter* PlayerCharacter)
{
	// CharacterがBoarPlayerCharacterでない場合はnullptrとなり、既存購読だけを解除する。
	UGadgetComponent* NewGadgetComponent =
		PlayerCharacter ? PlayerCharacter->GetGadgetComponent() : nullptr;

	if (ObservedGadgetComponent != NewGadgetComponent)
	{
		// Pawn切替前のComponentから通知が来ないよう、先に古いDelegateを外す。
		if (ObservedGadgetComponent)
		{
			ObservedGadgetComponent->OnGadgetLoadoutChanged.RemoveDynamic(
				this,
				&ABoarPlayerController::HandleGadgetLoadoutChanged);
		}

		ObservedGadgetComponent = NewGadgetComponent;
		if (ObservedGadgetComponent)
		{
			// AddUniqueDynamicでOnPossess/CreatePlayerHUDの二経路から呼ばれても重複登録しない。
			ObservedGadgetComponent->OnGadgetLoadoutChanged.AddUniqueDynamic(
				this,
				&ABoarPlayerController::HandleGadgetLoadoutChanged);
		}
	}

	// Component切替直後のスロット内容と選択Indexを、次の変更を待たずHUDへ反映する。
	HandleGadgetLoadoutChanged();
}

void ABoarPlayerController::HandleGadgetLoadoutChanged()
{
	if (!PlayerHUDWidget || !ObservedGadgetComponent)
		return;

	TArray<TSubclassOf<AGadgetBase>> GadgetSlots;

	// HUD仕様は4枠固定。空スロットはnullptrのClassとしてそのままWidgetへ渡す。
	GadgetSlots.Reserve(ObservedGadgetComponent->GetGadgetSlotCount());

	for (int32 SlotIndex = 0; SlotIndex < ObservedGadgetComponent->GetGadgetSlotCount(); ++SlotIndex)
	{
		GadgetSlots.Add(ObservedGadgetComponent->GetGadgetSlotClass(SlotIndex));
	}

	PlayerHUDWidget->UpdateGadgetSlots(
		GadgetSlots,
		ObservedGadgetComponent->GetCurrentGadgetSlotIndex());
}

void ABoarPlayerController::BindPlayerHealth(ABoarPlayerCharacter* PlayerCharacter)
{
	// Possess対象が変わったときに、HP通知元を古いCharacterから新しいCharacterへ切り替える。
	UHealthComponent* NewHealthComponent =
		PlayerCharacter ? PlayerCharacter->GetHealthComponent() : nullptr;

	if (ObservedHealthComponent == NewHealthComponent)
	{
		// 同じComponentでもHUDが作り直された可能性があるため、現在値だけは再送する。
		if (ObservedHealthComponent)
		{
			HandleHealthChanged(
				ObservedHealthComponent->GetCurrentHealth(),
				ObservedHealthComponent->GetMaxHealth());
		}

		return;
	}

	if (ObservedHealthComponent)
	{
		// 破棄済み/非PossessのCharacterからHP変更を受け取らないよう購読解除する。
		ObservedHealthComponent->OnHealthChanged.RemoveDynamic(
			this,
			&ABoarPlayerController::HandleHealthChanged);
	}

	//監視対象を新しいHPへ変更。
	ObservedHealthComponent = NewHealthComponent;
	if (ObservedHealthComponent)
	{
		ObservedHealthComponent->OnHealthChanged.AddUniqueDynamic(
			this,
			&ABoarPlayerController::HandleHealthChanged);
		//現在のHPをすぐHUDへ反映。
		HandleHealthChanged(
			ObservedHealthComponent->GetCurrentHealth(),
			ObservedHealthComponent->GetMaxHealth());
	}
}

void ABoarPlayerController::HandleCapturedBoarCountChanged(int32 CurrentCount, int32 TargetCount)
{
	if (PlayerHUDWidget)
	{
		// Controllerは値を加工せず、GameModeが通知した現在数と目標数を表示層へ中継する。
		PlayerHUDWidget->UpdateCaptureCount(CurrentCount, TargetCount);
	}
}

void ABoarPlayerController::HandleHealthChanged(float CurrentHealth, float MaxHealth)
{
	if (PlayerHUDWidget)
	{
		// HealthComponentのfloat値をそのままWidgetへ渡し、表示形式はWidget側へ任せる。
		PlayerHUDWidget->UpdateHealth(CurrentHealth, MaxHealth);
	}
}

void ABoarPlayerController::HandleStaminaChanged(float CurrentStamina, float MaxStamina, bool bDashLocked)
{
	if (PlayerHUDWidget)
	{
		// 表示形式（割合・色・透明度）はWidget側へ任せる。
		PlayerHUDWidget->UpdateStamina(CurrentStamina, MaxStamina, bDashLocked);
	}
}

void ABoarPlayerController::HandleStageCleared(const FStageRunData& Run, int32 TargetCount)
{
	// Server側、二重通知、Widget Class未設定ではResult画面を生成しない。
	if (!IsLocalController() || ResultWidget || !Run.bResultFrozen)
		return;

	// Enhanced Inputの各コールバックもこのフラグを見るため、Widget生成より先に入力を遮断する。
	SetStageInputBlocked(true);

	if (PlayerHUDWidget)
	{
		// Resultと通常HUDが重ならないよう非表示にする。Retry後はLevel開始時に再生成される。
		PlayerHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		// IgnoreMoveInputだけでは既存速度やDash状態が残るため、両方を明示的に停止する。
		PlayerCharacter->StopDash();

		if (UCharacterMovementComponent* Movement = PlayerCharacter->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}

	if (ResultWidgetClass)
		ResultWidget = CreateWidget<UBoarResultWidget>(this, ResultWidgetClass);

	if (!ResultWidget)
	{
		// C++で代替画面は作りません。Actor停止済みのStageは再開せず、そのままLobbyへ戻します。
		UE_LOG(LogREBoarGetch, Error, TEXT("[Result] %s: ResultWidgetClassが未設定か生成できません。Result画面を省いてLobbyへ戻ります。"),
			*GetNameSafe(GetClass()));
		ReturnToLobby();
		return;
	}

	// Widgetは任意入力だけを通知し、実際のLobby遷移はControllerが担当する。
	ResultWidget->OnDismissRequested.AddUniqueDynamic(this, &ABoarPlayerController::ReturnToLobby);

	// 通常HUDより前面へ表示し、確定した捕獲数を渡す。
	ResultWidget->AddToViewport(EndScreenZOrder);
	ResultWidget->InitializeResult(Run, TargetCount);

	// Gameplay入力をUI Onlyへ切り替え、任意入力を受け取るResult WidgetへFocusを設定する。
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(ResultWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	SetShowMouseCursor(false);
	ResultWidget->FocusForDismissInput();

	if (StageClearPresentation)
		StageClearPresentation->NotifyResultShown();
}

void ABoarPlayerController::SetPlayerHUDHidden(bool bHideHUD)
{
	if (PlayerHUDWidget)
		PlayerHUDWidget->SetVisibility(bHideHUD ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}

void ABoarPlayerController::SetPlayerHUDOpacity(float Opacity)
{
	if (PlayerHUDWidget)
		PlayerHUDWidget->SetRenderOpacity(FMath::Clamp(Opacity, 0.f, 1.f));
}

void ABoarPlayerController::ReturnToLobby()
{
	// 任意入力の連打中もLobbyを複数回Openしないよう、最初の要求だけ受け付ける。
	if (bLevelTransitionRequested)
		return;

	const TSoftObjectPtr<UWorld> Destination = GetLobbyLevel();
	if (Destination.IsNull())
	{
		UE_LOG(LogREBoarGetch, Error, TEXT("[Lobby] %s: 有効なLobbyのLevelがありません（LobbyLevelとGameDefaultMapがどちらも未設定か存在しません）。Lobbyへの遷移を中止します。"),
			*GetNameSafe(GetClass()));
		return;
	}

	// 遷移先が決まってから二重遷移防止を立てる。
	bLevelTransitionRequested = true;

	if (ResultWidget)
	{
		// Levelロード開始までの短い間も追加入力を受けないようWidget全体を無効化する。
		ResultWidget->SetIsEnabled(false);
	}

	UBoarLevelTransitionLibrary::OpenLevelBySoftObjectPtrWithTransition(this, Destination, EBoarLevelTransitionStyle::ReturnToLobby);
}

TSoftObjectPtr<UWorld> ABoarPlayerController::ResolveLobbyLevel(const TSoftObjectPtr<UWorld>& Configured,
                                                                const FSoftObjectPath& DefaultMap)
{
	if (!Configured.IsNull())
		return Configured;
	// 未設定時は、実在するDefaultMapだけを代替にします。存在しないMapを推測して開きません。
	if (DefaultMap.IsValid() && FPackageName::DoesPackageExist(DefaultMap.GetLongPackageName()))
		return TSoftObjectPtr<UWorld>(DefaultMap);

	return TSoftObjectPtr<UWorld>();
}

TSoftObjectPtr<UWorld> ABoarPlayerController::GetLobbyLevel() const
{
	// LobbyLevel未設定時は、ゲーム開始時のMap（Project Settings > Maps & Modes > Game Default Map）をLobbyの正本として使います。
	const TSoftObjectPtr<UWorld> Destination =
		ResolveLobbyLevel(LobbyLevel, FSoftObjectPath(UGameMapsSettings::GetGameDefaultMap()));
	if (LobbyLevel.IsNull() && !bLobbyLevelFallbackReported)
	{
		bLobbyLevelFallbackReported = true;
		const FString Detail = Destination.IsNull()
			? FString(TEXT("GameDefaultMapも未設定か存在しません。"))
			: FString::Printf(TEXT("代わりにGameDefaultMap '%s' を使います。"), *Destination.ToString());
		// 自動テスト（AddExpectedError）が「LobbyLevel is unset」を照合しているため、この部分は英語のまま。
		UE_LOG(LogREBoarGetch, Error, TEXT("[Lobby] %s: LobbyLevel is unset. %s"), *GetNameSafe(GetClass()), *Detail);
	}

	return Destination;
}

void ABoarPlayerController::SetStageInputBlocked(bool bBlocked)
{
	// Stageの進行可否を入力へ反映する窓口。メニュー独自のロックまで一括解除しない。
	if (bBlocked)
		ResumeFromPause();
	// Ignore入力は加算式なので、状態が変わるときだけ1回設定します。
	if (bGameplayInputBlocked == bBlocked)
		return;

	bGameplayInputBlocked = bBlocked;

	if (bBlocked)
		bIsGadgetModifierHeld = false;

	SetIgnoreMoveInput(bBlocked);
	SetIgnoreLookInput(bBlocked);
	FlushPressedKeys();
}

void ABoarPlayerController::HandleStageGameOver()
{
	if (!IsLocalController() || GameOverWidget || bGameOverUIUnavailable)
		return;

	SetStageInputBlocked(true);

	if (PlayerHUDWidget)
		PlayerHUDWidget->SetVisibility(ESlateVisibility::Collapsed);

	UBoarGameOverWidget* Widget = GameOverWidgetClass
			? CreateWidget<UBoarGameOverWidget>(this, GameOverWidgetClass)
			: nullptr;
	// 必須ボタンが欠けた画面は表示しません。C++で代替画面は作らず、GameOver状態のまま止めます（自動Retry・Level遷移なし）。
	const TArray<FString> Missing = Widget ? Widget->GetMissingRequiredWidgets() : TArray<FString>();
	if (!Widget || !Missing.IsEmpty())
	{
		bGameOverUIUnavailable = true;

		// UE_LOGは複文に展開されるため、if/elseは波括弧で囲む。
		if (!Widget)
		{
			UE_LOG(LogREBoarGetch, Error, TEXT("[GameOver] %s: GameOverWidgetClass（%s）が未設定か生成できません。GameOver画面は表示しません。"),
				*GetNameSafe(GetClass()), *GetNameSafe(GameOverWidgetClass.Get()));
		}
		else
		{
			UE_LOG(LogREBoarGetch, Error, TEXT("[GameOver] %s（%s.GameOverWidgetClass）: 必須のWidgetがありません: %s。GameOver画面は表示しません。"),
				*GetNameSafe(Widget->GetClass()), *GetNameSafe(GetClass()), *FString::Join(Missing, TEXT(", ")));
		}

		// Viewport未追加・Delegate未登録なので、参照を持たなければGCで破棄されます。
		return;
	}

	GameOverWidget = Widget;
	GameOverWidget->OnRetryRequested.AddUniqueDynamic(this, &ABoarPlayerController::RetryStage);
	GameOverWidget->OnLobbyRequested.AddUniqueDynamic(this, &ABoarPlayerController::ReturnToLobby);
	GameOverWidget->AddToViewport(EndScreenZOrder);
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(GameOverWidget->TakeWidget());
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
	GameOverWidget->FocusInitialChoice();

	if (GameOverPresentation)
		GameOverPresentation->NotifyGameOverUIShown();
}

void ABoarPlayerController::RetryStage()
{
	const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>();

	if (bLevelTransitionRequested || (!bInitializationFailed &&
		(!Mode || Mode->GetStageState() != EBoarStageState::GameOver)))
		return;

	bLevelTransitionRequested = true;

	// Map再読込でHP・檻・Boar・Timerを初期化し、GameInstanceの保存済み装備は維持します。
	UBoarLevelTransitionLibrary::OpenLevelWithTransition(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)), EBoarLevelTransitionStyle::Menu);
}

void ABoarPlayerController::HandleStageInitializationFailed(const FText& Reason)
{
	// 失敗理由はErrorログだけで報告します。専用画面・自動Retry・Level遷移は行わず、失敗状態のまま操作を止めます。
	if (!IsLocalController() || bInitializationFailed)
		return;

	bInitializationFailed = true;
	SetStageInputBlocked(true);

	if (PlayerHUDWidget)
		PlayerHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	// 理由の文言は呼び出し元（GameMode / FacilityGameMode）が直前にErrorで出力済みなので、ここでは状態だけを記録します。
	UE_LOG(LogREBoarGetch, Error, TEXT("[WorldInit] %s: 初期化に失敗しました。エラー画面は出さず、ゲームは停止したままにします（自動リトライやLevel遷移はしません）。"),
		*GetNameSafe(GetClass()));
}

void ABoarPlayerController::TogglePauseMenu()
{
	if (UIFlow)
		UIFlow->TogglePauseMenu();
}

void ABoarPlayerController::HandleMenuBack()
{
	if (UIFlow)
		UIFlow->HandleMenuBack();
}

void ABoarPlayerController::OpenLoadoutMenu()
{
	if (UIFlow)
		UIFlow->OpenLoadoutMenu();
}

void ABoarPlayerController::OpenEncyclopedia(UUserWidget* ParentMenu, UWidget* ReturnFocus)
{
	if (UIFlow)
		UIFlow->OpenEncyclopedia(ParentMenu, ReturnFocus);
}

void ABoarPlayerController::CloseEncyclopedia()
{
	if (UIFlow)
		UIFlow->CloseEncyclopedia();
}

void ABoarPlayerController::CloseEncyclopediaFrom(UUserWidget* ParentMenu)
{
	if (UIFlow)
		UIFlow->CloseEncyclopediaFrom(ParentMenu);
}

void ABoarPlayerController::OpenSettingsMenu(UUserWidget* ParentMenu, UWidget* ReturnFocus)
{
	if (UIFlow)
		UIFlow->OpenSettingsMenu(ParentMenu, ReturnFocus);
}

void ABoarPlayerController::CloseSettingsMenu()
{
	if (UIFlow)
		UIFlow->CloseSettingsMenu();
}

void ABoarPlayerController::CloseLoadoutMenu()
{
	if (UIFlow)
		UIFlow->CloseLoadoutMenu();
}

void ABoarPlayerController::OpenPauseMenu()
{
	if (UIFlow)
		UIFlow->OpenPauseMenu();
}

void ABoarPlayerController::ResumeFromPause()
{
	if (UIFlow)
		UIFlow->ResumeFromPause();
}

void ABoarPlayerController::LeaveStageFromPause()
{
	if (UIFlow)
		UIFlow->LeaveStageFromPause();
}

void ABoarPlayerController::RemoveLobbyExitConfirmation()
{
	if (UIFlow)
		UIFlow->RemoveLobbyExitConfirmation();
}

void ABoarPlayerController::ResolveLobbyExit(bool bConfirmed)
{
	if (UIFlow)
		UIFlow->ResolveLobbyExit(bConfirmed);
}

void ABoarPlayerController::Move(const FInputActionValue& Value)
{
	// Result表示中はInput Modeとは別に入口でも遮断し、入力状態変更の隙間を防ぐ。
	if (!CanProcessGameplayInput())
		return;

	// Axis2DをCharacterへ渡す。移動方向のWorld変換やAction Lock判定はCharacter側の責務。
	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->Move(Value.Get<FVector2D>());
	}
}

void ABoarPlayerController::Look(const FInputActionValue& Value)
{
	// Camera感度やYaw/Pitch反映はCharacter側へ委譲する。
	if (!CanProcessGameplayInput())
		return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->Look(Value.Get<FVector2D>());
	}
}

void ABoarPlayerController::JumpStarted()
{
	// Jump可能回数、Stun、Gadget使用中などの可否判定はCharacter側で行う。
	if (!CanProcessGameplayInput())
		return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StartJump();
	}
}

void ABoarPlayerController::JumpCompleted()
{
	// ボタンを離したことだけを通知し、CharacterMovementのJump保持を終了する。
	if (!CanProcessGameplayInput())
		return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StopJump();
	}
}

void ABoarPlayerController::GadgetStarted()
{
	// Cooldownや装備有無、Action LockはGadget/Character側で検証する。
	if (!CanProcessGameplayInput())
		return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		UE_LOG(LogREBoarGetch, Log, TEXT("[Input] ガジェット入力開始"));
		PlayerCharacter->StartGadgetUse();
	}
}

void ABoarPlayerController::GadgetCompleted()
{
	// Canceledもこの関数へBindされるため、フォーカス喪失時にも押下状態を終了できる。
	if (!CanProcessGameplayInput())
		return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		UE_LOG(LogREBoarGetch, Log, TEXT("[Input] ガジェット入力終了"));
		PlayerCharacter->StopGadgetUse();
	}
}

void ABoarPlayerController::DashStarted()
{
	// Controllerは入力開始だけを通知し、速度変更とAction StateはCharacterが管理する。
	if (!CanProcessGameplayInput())
		return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StartDash();
	}
}

void ABoarPlayerController::DashCompleted()
{
	// Completed/Canceledの両方から呼ばれ、Dash速度が残らないよう必ず終了通知する。
	if (!CanProcessGameplayInput())
		return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StopDash();
	}
}

void ABoarPlayerController::GadgetModifierStarted()
{
	// Face Buttonを通常操作ではなくガジェット選択として解釈する期間を開始する。
	if (!CanProcessGameplayInput())
		return;

	bIsGadgetModifierHeld = true;
}

void ABoarPlayerController::GadgetModifierCompleted()
{
	// Canceled時にもfalseへ戻し、フォーカス復帰後に選択モードが残るのを防ぐ。
	if (!CanProcessGameplayInput())
		return;

	bIsGadgetModifierHeld = false;
}

void ABoarPlayerController::SwitchGadgetSlot1()
{
	TrySwitchGadgetSlot(0);
}

void ABoarPlayerController::SwitchGadgetSlot2()
{
	TrySwitchGadgetSlot(1);
}

void ABoarPlayerController::SwitchGadgetSlot3()
{
	TrySwitchGadgetSlot(2);
}

void ABoarPlayerController::SwitchGadgetSlot4()
{
	TrySwitchGadgetSlot(3);
}

void ABoarPlayerController::TrySwitchGadgetSlot(int32 SlotIndex)
{
	// Result中、またはModifierなしのFace Button入力ではガジェットを切り替えない。
	if (!CanProcessGameplayInput())
		return;

	if (!bIsGadgetModifierHeld)
		return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->SwitchGadgetSlot(SlotIndex);
	}
}

ABoarPlayerCharacter* ABoarPlayerController::GetBoarCharacter() const
{
	// CharacterをメンバにキャッシュするとRespawn/Possess変更時に古い参照が残り得るため、
	// 必要な時点のGetPawn()を毎回Castして常に現在の操作対象を返す。
	return Cast<ABoarPlayerCharacter>(GetPawn());
}

bool ABoarPlayerController::CanProcessGameplayInput() const
{
	return !bGameplayInputBlocked && (!UIFlow || !UIFlow->BlocksGameplayInput()) && !bLevelTransitionRequested;
}

void ABoarPlayerController::AddOwnedMappingContext(UInputMappingContext* Context, int32 Priority)
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	auto* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Subsystem || !Context || Subsystem->HasMappingContext(Context))
		return;

	Subsystem->AddMappingContext(Context, Priority);
	OwnedMappingContexts.AddUnique(Context);
}

void ABoarPlayerController::RemoveOwnedMappingContext(UInputMappingContext* Context)
{
	if (!Context || !OwnedMappingContexts.Remove(Context))
		return;

	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (auto* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr)
		Subsystem->RemoveMappingContext(Context);
}

void ABoarPlayerController::OpenFacilityMenu(bool bArchive)
{
	if (UIFlow)
		UIFlow->OpenFacilityMenu(bArchive);
}

void ABoarPlayerController::InteractWithFacility()
{
	if (!CanProcessGameplayInput() || !GetPawn() || bIsGadgetModifierHeld ||
		GetWorld()->GetAuthGameMode<ABoarGameMode>())
		return;

	TArray<AActor*> Nearby;
	GetPawn()->GetOverlappingActors(Nearby);
	AActor* Nearest = nullptr;

	for (AActor* Actor : Nearby)
	{
		const auto* Portal = Cast<AHubPortal>(Actor);
		const auto* Entrance = Cast<AStageEntrance>(Actor);
		const bool bEligible = (Portal && Portal->bRequiresInteraction) || (Entrance && Entrance->CanInteract(this));
		if (bEligible && (!Nearest || GetPawn()->GetSquaredDistanceTo(Actor) < GetPawn()->
			GetSquaredDistanceTo(Nearest)))
			Nearest = Actor;
	}

	if (auto* Portal = Cast<AHubPortal>(Nearest))
		Portal->Interact(this);
	else if (auto* Entrance = Cast<AStageEntrance>(Nearest))
		Entrance->Interact(this);
}

void ABoarPlayerController::OpenArchiveDisplaySelection()
{
	if (UIFlow)
		UIFlow->OpenArchiveDisplaySelection();
}

void ABoarPlayerController::CloseArchiveDisplaySelection()
{
	if (UIFlow)
		UIFlow->CloseArchiveDisplaySelection();
}

void ABoarPlayerController::CloseFacilityMenuInput()
{
	if (UIFlow)
		UIFlow->CloseFacilityMenuInput();
}

bool ABoarPlayerController::IsPauseMenuOpen() const
{
	return UIFlow && UIFlow->IsPauseMenuOpen();
}

// ゲーム入力の機器だけを記録し、処理は通常どおり親へ渡します（Gameplayの入力挙動は変えません）。
bool ABoarPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	bLastInputGamepad = Params.Key.IsGamepadKey();
	return Super::InputKey(Params);
}
