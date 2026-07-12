// Fill out your copyright notice in the Description page of Project Settings.


#include "BoarAIController.h"
#include "Components/StateTreeAIComponent.h"

ABoarAIController::ABoarAIController()
{
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
	check(StateTreeAI);

	bStartAILogicOnPossess = false;
	StateTreeAI->SetStartLogicAutomatically(false);
	bAttachToPawn = true;
}
// Pawnを所持したときにAIを開始する
void ABoarAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (StateTreeAI)
	{
		StateTreeAI->StartLogic();
	}
}
