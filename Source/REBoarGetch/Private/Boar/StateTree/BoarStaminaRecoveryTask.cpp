#include "Boar/StateTree/BoarStaminaRecoveryTask.h"

#include "AIController.h"
#include "Boar/BoarBase.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FStateTreeBoarStaminaRecoveryTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());
	if (Boar == nullptr || Boar->IsCaptured())
		return EStateTreeRunStatus::Failed;
	if (!Boar->IsStaminaRecovering())
		return EStateTreeRunStatus::Succeeded;

	// 前Stateから残った移動要求があっても、回復中はその場に留める。
	if (AAIController* Controller = Cast<AAIController>(Boar->GetController()))
	{
		Controller->StopMovement();
	}
	if (UCharacterMovementComponent* Move = Boar->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeBoarStaminaRecoveryTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	(void)DeltaTime;

	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());
	if (Boar == nullptr || Boar->IsCaptured())
		return EStateTreeRunStatus::Failed;

	// StaminaRecoverExitRatioに達するまでRunningを返し、逃走Stateへ戻さない。
	return Boar->IsStaminaRecovering() ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}
