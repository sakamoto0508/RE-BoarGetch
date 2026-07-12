// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/BoarPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Player/BoarPlayerCharacter.h"


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
// Input
////////////////////////////////////////////////////////////////////////////////////////////////////

void ABoarPlayerController::Move(const FInputActionValue& Value)
{
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
	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->Look(Value.Get<FVector2D>());
	}
}

void ABoarPlayerController::JumpStarted()
{
	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StartJump();
	}
}

void ABoarPlayerController::JumpCompleted()
{
	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StopJump();
	}
}

void ABoarPlayerController::GadgetStarted()
{
	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		UE_LOG(LogTemp, Log, TEXT("[Input] Gadget started"));
		PlayerCharacter->StartGadgetUse();
	}
}

void ABoarPlayerController::GadgetCompleted()
{
	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		UE_LOG(LogTemp, Log, TEXT("[Input] Gadget completed"));
		PlayerCharacter->StopGadgetUse();
	}
}

void ABoarPlayerController::DashStarted()
{
	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StartDash();
	}
}

void ABoarPlayerController::DashCompleted()
{
	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->StopDash();
	}
}

void ABoarPlayerController::GadgetModifierStarted()
{
	bIsGadgetModifierHeld = true;
}

void ABoarPlayerController::GadgetModifierCompleted()
{
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
