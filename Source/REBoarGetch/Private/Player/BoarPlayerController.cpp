// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/BoarPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Component/HealthComponent.h"
#include "Component/GadgetComponent.h"
#include "Core/BoarGameMode.h"
#include "Player/BoarPlayerCharacter.h"
#include "Stage/StageConfig.h"
#include "UI/BoarHUDWidget.h"
#include "UI/BoarResultWidget.h"
#include "UI/BoarGameOverWidget.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"


void ABoarPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// RetryによるLevel再読み込みではControllerの入力状態が残る場合があるため、
	// Mapping ContextやHUDを準備する前に、必ずゲーム操作可能な初期状態へ戻す。
	RestoreGameplayInputState();

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
		InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
		UE_LOG(LogTemp, Log, TEXT("[Input] MappingContext added: %s"), *GetNameSafe(DefaultMappingContext));
	}
	else if (IsLocalController())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Input] MappingContext was not registered. LocalPlayer=%s Subsystem=%s Context=%s"),
			*GetNameSafe(LocalPlayer), *GetNameSafe(InputSubsystem), *GetNameSafe(DefaultMappingContext));
	}

	// Mapping Context登録後にHUDを生成し、現在Possess中のCharacterとGameModeへ接続する。
	CreatePlayerHUD();
}

void ABoarPlayerController::RestoreGameplayInputState()
{
	// PlayerControllerはLevel再読み込み後も再利用される場合があるため、Result表示時に変更した
	// ignore-input、押下中キー、Mouse/UI入力状態を明示的にゲーム開始状態へ戻す。
	bResultScreenActive = false;
	bResultTransitionRequested = false;
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
	BindPlayerGadgets(PlayerCharacter);
}

void ABoarPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
		UE_LOG(LogTemp, Warning, TEXT("[Input] InputComponent is not UEnhancedInputComponent: %s"),
			*GetNameSafe(InputComponent));
		return;
	}

	if (MoveAction)
	{
		// Axis2Dの値が変化している間、毎Frame Moveへ移動方向を渡す。
		EnhancedInput->BindAction(MoveAction,ETriggerEvent::Triggered,this,&ABoarPlayerController::Move);
	}

	if (LookAction)
	{
		// Mouse/GamepadのAxis2D入力が続く間、Camera入力をCharacterへ渡す。
		EnhancedInput->BindAction(LookAction,ETriggerEvent::Triggered,this,&ABoarPlayerController::Look);
	}

	if (JumpAction)
	{
		// Startedは押した瞬間だけ発火し、Character側でジャンプ可能回数を判定する。
		EnhancedInput->BindAction(JumpAction,ETriggerEvent::Started,this,&ABoarPlayerController::JumpStarted);

		// Completedは離した瞬間にJump入力を止め、長押しジャンプを正しく終了させる。
		EnhancedInput->BindAction(JumpAction,ETriggerEvent::Completed,this,&ABoarPlayerController::JumpCompleted);
	}

	if (GadgetAction)
	{
		// Started/Completed/Canceled を分けて、ガジェット使用ライフサイクルを管理する。
		EnhancedInput->BindAction(GadgetAction, ETriggerEvent::Started, this, &ABoarPlayerController::GadgetStarted);
		EnhancedInput->BindAction(GadgetAction, ETriggerEvent::Completed, this, &ABoarPlayerController::GadgetCompleted);
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
		EnhancedInput->BindAction(GadgetModifierAction, ETriggerEvent::Started, this, &ABoarPlayerController::GadgetModifierStarted);
		EnhancedInput->BindAction(GadgetModifierAction, ETriggerEvent::Completed, this, &ABoarPlayerController::GadgetModifierCompleted);
		EnhancedInput->BindAction(GadgetModifierAction, ETriggerEvent::Canceled, this, &ABoarPlayerController::GadgetModifierCompleted);
	}

	if (GadgetSlot1Action)
	{
		// Enhanced Inputへ引数付き関数を直接Bindしないため、各入口から0始まりのIndexへ変換する。
		EnhancedInput->BindAction(GadgetSlot1Action, ETriggerEvent::Started, this, &ABoarPlayerController::SwitchGadgetSlot1);
	}
	if (GadgetSlot2Action)
	{
		EnhancedInput->BindAction(GadgetSlot2Action, ETriggerEvent::Started, this, &ABoarPlayerController::SwitchGadgetSlot2);
	}
	if (GadgetSlot3Action)
	{
		EnhancedInput->BindAction(GadgetSlot3Action, ETriggerEvent::Started, this, &ABoarPlayerController::SwitchGadgetSlot3);
	}
	if (GadgetSlot4Action)
	{
		EnhancedInput->BindAction(GadgetSlot4Action, ETriggerEvent::Started, this, &ABoarPlayerController::SwitchGadgetSlot4);
	}
}


void ABoarPlayerController::CreatePlayerHUD()
{
	// HUDはローカル画面専用。Server Controller、生成済み、BP Class未設定の場合は何もしない。
	if (!IsLocalController() || PlayerHUDWidget || PlayerHUDWidgetClass == nullptr)
	{
		return;
	}

	// Owning Playerをthisにすることで、Widgetが正しいLocalPlayer/Inputを参照できるようにする。
	PlayerHUDWidget = CreateWidget<UBoarHUDWidget>(this, PlayerHUDWidgetClass);
	if (PlayerHUDWidget == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HUD] Failed to create player HUD widget"));
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
	BindPlayerGadgets(GetBoarCharacter());
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
	{
		return;
	}

	TArray<TSubclassOf<AGadgetBase>> GadgetSlots;
	// HUD仕様は4枠固定。空スロットはnullptrのClassとしてそのままWidgetへ渡す。
	GadgetSlots.Reserve(4);
	for (int32 SlotIndex = 0; SlotIndex < 4; ++SlotIndex)
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


void ABoarPlayerController::HandleStageCleared(int32 CapturedCount, int32 TargetCount)
{
	// Server側、二重通知、Widget Class未設定ではResult画面を生成しない。
	if (!IsLocalController() || ResultWidget || ResultWidgetClass == nullptr)
	{
		return;
	}

	// Enhanced Inputの各コールバックもこのフラグを見るため、Widget生成より先に入力を遮断する。
	bResultScreenActive = true;
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
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

	ResultWidget = CreateWidget<UBoarResultWidget>(this, ResultWidgetClass);
	if (ResultWidget == nullptr)
	{
		bResultScreenActive = false;
		// Widget生成失敗時にゲームが操作不能にならないよう、直前の入力ロックを巻き戻す。
		SetIgnoreMoveInput(false);
		SetIgnoreLookInput(false);
		UE_LOG(LogTemp, Warning, TEXT("[Result] Failed to create result widget"));
		return;
	}

	// Widgetは任意入力だけを通知し、実際のLobby遷移はControllerが担当する。
	ResultWidget->OnDismissRequested.AddUniqueDynamic(this, &ABoarPlayerController::ReturnToLobby);
	// ZOrder 100で通常HUDより前面へ表示し、確定した捕獲数を渡す。
	ResultWidget->AddToViewport(100);
	ResultWidget->InitializeResult(CapturedCount, TargetCount);
	if (const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
		ResultWidget->InitializeRunResult(Mode->GetStageRunData());

	// Gameplay入力をUI Onlyへ切り替え、任意入力を受け取るResult WidgetへFocusを設定する。
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(ResultWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	SetShowMouseCursor(false);
	ResultWidget->FocusForDismissInput();
}

void ABoarPlayerController::ReturnToLobby()
{
	// 任意入力の連打中もLobbyを複数回Openしないよう、最初の要求だけ受け付ける。
	if (bResultTransitionRequested || LobbyLevelName.IsNone())
	{
		return;
	}

	bResultTransitionRequested = true;
	if (ResultWidget)
	{
		// Levelロード開始までの短い間も追加入力を受けないようWidget全体を無効化する。
		ResultWidget->SetIsEnabled(false);
	}

	UGameplayStatics::OpenLevel(this, LobbyLevelName);
}

void ABoarPlayerController::SetStageInputBlocked(bool bBlocked)
{
	bResultScreenActive = bBlocked;
	SetIgnoreMoveInput(bBlocked);
	SetIgnoreLookInput(bBlocked);
	FlushPressedKeys();
}

void ABoarPlayerController::HandleStageGameOver()
{
	if (!IsLocalController() || GameOverWidget) return;
	SetStageInputBlocked(true);
	if (PlayerHUDWidget) PlayerHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (!GameOverWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[GameOver] GameOverWidgetClass is not configured. Result is not used for GameOver."));
		return;
	}
	GameOverWidget = CreateWidget<UBoarGameOverWidget>(this, GameOverWidgetClass);
	if (!GameOverWidget) return;
	GameOverWidget->OnRetryRequested.AddUniqueDynamic(this, &ABoarPlayerController::RetryStage);
	GameOverWidget->OnLobbyRequested.AddUniqueDynamic(this, &ABoarPlayerController::ReturnToLobby);
	GameOverWidget->AddToViewport(100);
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(GameOverWidget->TakeWidget());
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
	GameOverWidget->FocusInitialChoice();
}

void ABoarPlayerController::RetryStage()
{
	const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>();
	if (bResultTransitionRequested || !Mode || Mode->GetStageState() != EBoarStageState::GameOver) return;
	bResultTransitionRequested = true;
	// Map再読込でHP・檻・Boar・Timerを初期化し、GameInstanceの保存済み装備は維持します。
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Input
////////////////////////////////////////////////////////////////////////////////////////////////////

void ABoarPlayerController::Move(const FInputActionValue& Value)
{
	// Result表示中はInput Modeとは別に入口でも遮断し、入力状態変更の隙間を防ぐ。
	if (bResultScreenActive) return;

	// Axis2DをCharacterへ渡す。移動方向のWorld変換やAction Lock判定はCharacter側の責務。
	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->Move(Value.Get<FVector2D>());
	}
}

void ABoarPlayerController::Look(const FInputActionValue& Value)
{
	// Camera感度やYaw/Pitch反映はCharacter側へ委譲する。
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->Look(Value.Get<FVector2D>());
	}
}

void ABoarPlayerController::JumpStarted()
{
	// Jump可能回数、Stun、Gadget使用中などの可否判定はCharacter側で行う。
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StartJump();
	}
}

void ABoarPlayerController::JumpCompleted()
{
	// ボタンを離したことだけを通知し、CharacterMovementのJump保持を終了する。
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StopJump();
	}
}

void ABoarPlayerController::GadgetStarted()
{
	// Cooldownや装備有無、Action LockはGadget/Character側で検証する。
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		UE_LOG(LogTemp, Log, TEXT("[Input] Gadget started"));
		PlayerCharacter->StartGadgetUse();
	}
}

void ABoarPlayerController::GadgetCompleted()
{
	// Canceledもこの関数へBindされるため、フォーカス喪失時にも押下状態を終了できる。
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		UE_LOG(LogTemp, Log, TEXT("[Input] Gadget completed"));
		PlayerCharacter->StopGadgetUse();
	}
}

void ABoarPlayerController::DashStarted()
{
	// Controllerは入力開始だけを通知し、速度変更とAction StateはCharacterが管理する。
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StartDash();
	}
}

void ABoarPlayerController::DashCompleted()
{
	// Completed/Canceledの両方から呼ばれ、Dash速度が残らないよう必ず終了通知する。
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StopDash();
	}
}

void ABoarPlayerController::GadgetModifierStarted()
{
	// Face Buttonを通常操作ではなくガジェット選択として解釈する期間を開始する。
	if (bResultScreenActive) return;

	bIsGadgetModifierHeld = true;
}

void ABoarPlayerController::GadgetModifierCompleted()
{
	// Canceled時にもfalseへ戻し、フォーカス復帰後に選択モードが残るのを防ぐ。
	if (bResultScreenActive) return;

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
	if (bResultScreenActive) return;

	if (!bIsGadgetModifierHeld)
	{
		return;
	}

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->SwitchGadgetSlot(SlotIndex);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility
////////////////////////////////////////////////////////////////////////////////////////////////////

ABoarPlayerCharacter* ABoarPlayerController::GetBoarCharacter() const
{
	// CharacterをメンバにキャッシュするとRespawn/Possess変更時に古い参照が残り得るため、
	// 必要な時点のGetPawn()を毎回Castして常に現在の操作対象を返す。
	return Cast<ABoarPlayerCharacter>(GetPawn());
}
