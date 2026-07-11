// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/BoarPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
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
		//Startedボタンを押した瞬間
		EnhancedInput->BindAction(GadgetAction,ETriggerEvent::Started,this,&ABoarPlayerController::UseGadget);
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

void ABoarPlayerController::UseGadget()
{
	if (ABoarPlayerCharacter* PlayerCharacter = GetBoarCharacter())
	{
		PlayerCharacter->UseGadget();
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
