#include "BoarAIController.h"
#include "Components/StateTreeAIComponent.h"
#include "Core/BoarGameMode.h"
#include "Engine/World.h"

ABoarAIController::ABoarAIController()
{
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
	check(StateTreeAI);

	bStartAILogicOnPossess = false;
	StateTreeAI->SetStartLogicAutomatically(false);
	bAttachToPawn = true;
}

// PawnのPossessが完了してからStateTreeを開始する。
// BeginPlayより後に開始することで、Pawnの初期化完了後にAIが動く。
void ABoarAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Preparing中はGameModeのPlaying遷移で開始します。
	if (StateTreeAI && ABoarGameMode::IsStageProgressAllowed(this))
	{
		StateTreeAI->StartLogic();
	}
}
