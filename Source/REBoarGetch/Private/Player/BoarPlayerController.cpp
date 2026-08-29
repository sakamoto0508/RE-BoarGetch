// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/BoarPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Component/HealthComponent.h"
#include "Core/BoarGameMode.h"
#include "Player/BoarPlayerCharacter.h"
#include "Stage/StageConfig.h"
#include "UI/BoarHUDWidget.h"
#include "UI/BoarResultWidget.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"


////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
////////////////////////////////////////////////////////////////////////////////////////////////////

ABoarPlayerController::ABoarPlayerController()
{
	// Controllerは「入力の管理」が責務なので、
	// CharacterのようにComponentを生成することは基本的にありません。
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// BeginPlay
////////////////////////////////////////////////////////////////////////////////////////////////////

void ABoarPlayerController::BeginPlay()
{
	Super::BeginPlay();
	RestoreGameplayInputState();

	// LocalPlayerが持つEnhanced Input Subsystemを取得する。
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		/*
		 * Mapping Contextを登録する。
		 * Priority数字が大きいほど優先順位が高くなる。
		 */
		InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
		UE_LOG(LogTemp, Log, TEXT("[Input] MappingContext added: %s"), *GetNameSafe(DefaultMappingContext));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Input] EnhancedInputLocalPlayerSubsystem is null"));
	}

	CreatePlayerHUD();
}

void ABoarPlayerController::RestoreGameplayInputState()
{
	bResultScreenActive = false;
	bResultTransitionRequested = false;
	bIsGadgetModifierHeld = false;

	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();
	FlushPressedKeys();

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	SetShowMouseCursor(false);
}

void ABoarPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	BindPlayerHealth(Cast<ABoarPlayerCharacter>(InPawn));
}

void ABoarPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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

	Super::EndPlay(EndPlayReason);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SetupInputComponent
////////////////////////////////////////////////////////////////////////////////////////////////////

void ABoarPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	/*
	 * APlayerControllerのInputComponentは UInputComponent型。
	 * Enhanced Inputを使用するため、UEnhancedInputComponentへキャストする。
	 */
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);

	if (EnhancedInput == nullptr)	return;
	
	////////////////////////////////////////////////////////////
	// Move
	////////////////////////////////////////////////////////////

	if (MoveAction)
	{
		EnhancedInput->BindAction(MoveAction,ETriggerEvent::Triggered,this,&ABoarPlayerController::Move);
	}

	////////////////////////////////////////////////////////////
	// Look
	////////////////////////////////////////////////////////////

	if (LookAction)
	{
		EnhancedInput->BindAction(LookAction,ETriggerEvent::Triggered,this,&ABoarPlayerController::Look);
	}

	////////////////////////////////////////////////////////////
	// Jump
	////////////////////////////////////////////////////////////

	if (JumpAction)
	{
		//Startedボタンを押した瞬間
		EnhancedInput->BindAction(JumpAction,ETriggerEvent::Started,this,&ABoarPlayerController::JumpStarted);

		//Completedボタンを離した瞬間
		EnhancedInput->BindAction(JumpAction,ETriggerEvent::Completed,this,&ABoarPlayerController::JumpCompleted);
	}

	////////////////////////////////////////////////////////////
	// Gadget
	////////////////////////////////////////////////////////////

	if (GadgetAction)
	{
		// Started/Completed/Canceled を分けて、ガジェット使用ライフサイクルを管理する。
		EnhancedInput->BindAction(GadgetAction, ETriggerEvent::Started, this, &ABoarPlayerController::GadgetStarted);
		EnhancedInput->BindAction(GadgetAction, ETriggerEvent::Completed, this, &ABoarPlayerController::GadgetCompleted);
		EnhancedInput->BindAction(GadgetAction, ETriggerEvent::Canceled, this, &ABoarPlayerController::GadgetCompleted);
	}

	////////////////////////////////////////////////////////////
	// Dash
	////////////////////////////////////////////////////////////

	if (DashAction)
	{
		EnhancedInput->BindAction(DashAction, ETriggerEvent::Started, this, &ABoarPlayerController::DashStarted);
		EnhancedInput->BindAction(DashAction, ETriggerEvent::Completed, this, &ABoarPlayerController::DashCompleted);
		// フォーカス外れ等で入力がキャンセルされた場合も必ずダッシュ解除する。
		EnhancedInput->BindAction(DashAction, ETriggerEvent::Canceled, this, &ABoarPlayerController::DashCompleted);
	}

	////////////////////////////////////////////////////////////
	// Gadget Slot Switch (R1 + face button)
	////////////////////////////////////////////////////////////

	if (GadgetModifierAction)
	{
		EnhancedInput->BindAction(GadgetModifierAction, ETriggerEvent::Started, this, &ABoarPlayerController::GadgetModifierStarted);
		EnhancedInput->BindAction(GadgetModifierAction, ETriggerEvent::Completed, this, &ABoarPlayerController::GadgetModifierCompleted);
		EnhancedInput->BindAction(GadgetModifierAction, ETriggerEvent::Canceled, this, &ABoarPlayerController::GadgetModifierCompleted);
	}

	if (GadgetSlot1Action)
	{
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

////////////////////////////////////////////////////////////////////////////////////////////////////
// Gameplay HUD
////////////////////////////////////////////////////////////////////////////////////////////////////

void ABoarPlayerController::CreatePlayerHUD()
{
	if (!IsLocalController() || PlayerHUDWidget || PlayerHUDWidgetClass == nullptr)
	{
		return;
	}

	PlayerHUDWidget = CreateWidget<UBoarHUDWidget>(this, PlayerHUDWidgetClass);
	if (PlayerHUDWidget == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HUD] Failed to create player HUD widget"));
		return;
	}

	PlayerHUDWidget->AddToViewport(0);
	PlayerHUDWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (ABoarGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr)
	{
		GameMode->OnCapturedBoarCountChanged.AddUniqueDynamic(
			this,
			&ABoarPlayerController::HandleCapturedBoarCountChanged);

		const UStageConfig* StageConfig = GameMode->GetStageConfig();
		HandleCapturedBoarCountChanged(
			GameMode->GetCapturedBoarCount(),
			StageConfig ? StageConfig->TargetCaptureCount : 0);
	}

	BindPlayerHealth(GetBoarCharacter());
}

void ABoarPlayerController::BindPlayerHealth(ABoarPlayerCharacter* PlayerCharacter)
{
	UHealthComponent* NewHealthComponent =
		PlayerCharacter ? PlayerCharacter->GetHealthComponent() : nullptr;

	if (ObservedHealthComponent == NewHealthComponent)
	{
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
		ObservedHealthComponent->OnHealthChanged.RemoveDynamic(
			this,
			&ABoarPlayerController::HandleHealthChanged);
	}

	ObservedHealthComponent = NewHealthComponent;
	if (ObservedHealthComponent)
	{
		ObservedHealthComponent->OnHealthChanged.AddUniqueDynamic(
			this,
			&ABoarPlayerController::HandleHealthChanged);
		HandleHealthChanged(
			ObservedHealthComponent->GetCurrentHealth(),
			ObservedHealthComponent->GetMaxHealth());
	}
}

void ABoarPlayerController::HandleCapturedBoarCountChanged(int32 CurrentCount, int32 TargetCount)
{
	if (PlayerHUDWidget)
	{
		PlayerHUDWidget->UpdateCaptureCount(CurrentCount, TargetCount);
	}
}

void ABoarPlayerController::HandleHealthChanged(float CurrentHealth, float MaxHealth)
{
	if (PlayerHUDWidget)
	{
		PlayerHUDWidget->UpdateHealth(CurrentHealth, MaxHealth);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Result UI
////////////////////////////////////////////////////////////////////////////////////////////////////

void ABoarPlayerController::HandleStageCleared(int32 CapturedCount, int32 TargetCount)
{
	if (!IsLocalController() || bResultScreenActive || ResultWidgetClass == nullptr)
	{
		return;
	}

	bResultScreenActive = true;
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	if (PlayerHUDWidget)
	{
		PlayerHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
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
		SetIgnoreMoveInput(false);
		SetIgnoreLookInput(false);
		UE_LOG(LogTemp, Warning, TEXT("[Result] Failed to create result widget"));
		return;
	}

	ResultWidget->OnRetryRequested.AddUniqueDynamic(this, &ABoarPlayerController::RetryCurrentStage);
	ResultWidget->OnTitleRequested.AddUniqueDynamic(this, &ABoarPlayerController::ReturnToTitle);
	ResultWidget->AddToViewport(100);
	ResultWidget->InitializeResult(CapturedCount, TargetCount);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(ResultWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
	ResultWidget->FocusInitialControl();
}

void ABoarPlayerController::RetryCurrentStage()
{
	if (bResultTransitionRequested)
	{
		return;
	}

	bResultTransitionRequested = true;
	if (ResultWidget)
	{
		ResultWidget->SetIsEnabled(false);
	}

	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(this, true);
	if (CurrentLevelName.IsEmpty())
	{
		bResultTransitionRequested = false;
		return;
	}

	UGameplayStatics::OpenLevel(this, FName(*CurrentLevelName));
}

void ABoarPlayerController::ReturnToTitle()
{
	if (bResultTransitionRequested || TitleLevelName.IsNone())
	{
		return;
	}

	bResultTransitionRequested = true;
	if (ResultWidget)
	{
		ResultWidget->SetIsEnabled(false);
	}

	UGameplayStatics::OpenLevel(this, TitleLevelName);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Input
////////////////////////////////////////////////////////////////////////////////////////////////////

void ABoarPlayerController::Move(const FInputActionValue& Value)
{
	if (bResultScreenActive) return;

	/*
	 * Character取得毎回GetPawn()から取得する。
	 * Possessが切り替わっても 常に最新のCharacterになる。
	 */
	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->Move(Value.Get<FVector2D>());
	}
}

void ABoarPlayerController::Look(const FInputActionValue& Value)
{
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->Look(Value.Get<FVector2D>());
	}
}

void ABoarPlayerController::JumpStarted()
{
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StartJump();
	}
}

void ABoarPlayerController::JumpCompleted()
{
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StopJump();
	}
}

void ABoarPlayerController::GadgetStarted()
{
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		UE_LOG(LogTemp, Log, TEXT("[Input] Gadget started"));
		PlayerCharacter->StartGadgetUse();
	}
}

void ABoarPlayerController::GadgetCompleted()
{
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		UE_LOG(LogTemp, Log, TEXT("[Input] Gadget completed"));
		PlayerCharacter->StopGadgetUse();
	}
}

void ABoarPlayerController::DashStarted()
{
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StartDash();
	}
}

void ABoarPlayerController::DashCompleted()
{
	if (bResultScreenActive) return;

	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StopDash();
	}
}

void ABoarPlayerController::GadgetModifierStarted()
{
	if (bResultScreenActive) return;

	bIsGadgetModifierHeld = true;
}

void ABoarPlayerController::GadgetModifierCompleted()
{
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
	/*
	 * 現在PossessしているPawnを取得する。
	 *
	 * APlayerController > GetPawn() > ABoarPlayerCharacterへCast
	 */
	return Cast<ABoarPlayerCharacter>(GetPawn());
}
