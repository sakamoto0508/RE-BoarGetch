#include "Boar/StateTree/BoarStaminaRecoveringCondition.h"

#include "Boar/BoarBase.h"

#include "StateTreeExecutionContext.h"

bool FStateTreeBoarStaminaRecoveringCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());

	const bool bRecovering = Boar != nullptr
		&& !Boar->IsCaptured()
		&& Boar->ShouldStopWhileRecoveringStamina()
		&& Boar->IsStaminaRecovering();

	return bRecovering ^ bInvert;
}
