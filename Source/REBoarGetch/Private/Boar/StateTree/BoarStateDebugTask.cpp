#include "Boar/StateTree/BoarStateDebugTask.h"

#include "Boar/BoarBase.h"
#include "StateTreeExecutionContext.h"

namespace
{
	void PrintStateTreeDebug(
		ABoarBase* Boar,
		const FName StateName,
		const TCHAR* EventName,
		const FLinearColor Color)
	{
		(void)Boar;
		(void)StateName;
		(void)EventName;
		(void)Color;
	}
}

EStateTreeRunStatus FStateTreeBoarStateDebugTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	PrintStateTreeDebug(
		Cast<ABoarBase>(InstanceData.NPC.Get()),
		InstanceData.StateName,
		TEXT("ENTER"),
		FLinearColor::Green);
	return EStateTreeRunStatus::Running;
}

void FStateTreeBoarStateDebugTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	PrintStateTreeDebug(
		Cast<ABoarBase>(InstanceData.NPC.Get()),
		InstanceData.StateName,
		TEXT("EXIT"),
		FLinearColor::Yellow);
}

void FStateTreeBoarStateDebugTask::StateCompleted(
	FStateTreeExecutionContext& Context,
	const EStateTreeRunStatus CompletionStatus,
	const FStateTreeActiveStates& CompletedActiveStates) const
{
	(void)CompletedActiveStates;
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const TCHAR* EventName =
		CompletionStatus == EStateTreeRunStatus::Succeeded
			? TEXT("COMPLETED_SUCCEEDED")
			: TEXT("COMPLETED_FAILED");
	PrintStateTreeDebug(
		Cast<ABoarBase>(InstanceData.NPC.Get()),
		InstanceData.StateName,
		EventName,
		CompletionStatus == EStateTreeRunStatus::Succeeded
			? FLinearColor::Blue
			: FLinearColor::Red);
}
