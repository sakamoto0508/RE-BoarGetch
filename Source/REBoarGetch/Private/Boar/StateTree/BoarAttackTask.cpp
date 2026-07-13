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

	ACage* Cage = InstanceData.TargetCage.Get();

	if (Cage == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	Cage->ApplyDamage(Damage);

	return EStateTreeRunStatus::Succeeded;
}