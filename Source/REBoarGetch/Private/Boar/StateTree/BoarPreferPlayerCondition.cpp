#include "Boar/StateTree/BoarPreferPlayerCondition.h"

#include "Boar/BoarBase.h"

#include "StateTreeExecutionContext.h"

bool FStateTreeBoarPreferPlayerCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());

	bool bPreferPlayer = false;
	if (Boar != nullptr && !Boar->IsCaptured())
	{
		bool bPreferCage = false;
		bool bPreferEscape = false;
		Boar->GetTargetPreference(bPreferPlayer, bPreferCage, bPreferEscape);
	}

	return bPreferPlayer ^ bInvert;
}
