#include "Boar/StateTree/BoarAttackTask.h"

#include "Boar/BoarBase.h"
#include "Cage/Cage.h"

#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FStateTreeBoarAttackTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData =
		Context.GetInstanceData(*this);
	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());

	ACage* Cage = InstanceData.TargetCage.Get();

	if (Cage == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	Cage->ApplyDamage(Damage);
	if (Boar)
	{
		Boar->PrintAIStateDebug(TEXT("AttackCage"), Cage->GetActorLocation());
	}

	return EStateTreeRunStatus::Succeeded;
}
