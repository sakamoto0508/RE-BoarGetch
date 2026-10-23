#include "Boar/StateTree/BoarEscapeTask.h"

#include "Boar/BoarBase.h"

#include "NavigationSystem.h"
#include "StateTreeExecutionContext.h"

namespace
{
	/** 真後ろから左右交互に広げる候補角度です。先頭は従来の真後ろ方向です。 */
	constexpr float EscapeCandidateAngles[] = {0.0f, 35.0f, -35.0f, 70.0f, -70.0f, 105.0f, -105.0f};

	/** 方向候補がすべて使えない場合に試すランダム地点の数です。 */
	constexpr int32 RandomEscapeAttempts = 8;

	/**
	 * プレイヤーから離れる逃走先をNavMesh上で探す。
	 * NavMesh端で現在地付近へ投影された候補や、プレイヤーへ近づく候補は除外する。
	 */
	bool FindEscapeLocation(const ABoarBase& Boar, const FVector& ThreatLocation,
		UNavigationSystemV1& Nav, float MinEscapeDistance, float MinEscapeDistanceRatio, FVector& OutLocation)
	{
		const float EscapeDistance = Boar.GetEscapeRange();
		if (EscapeDistance <= KINDA_SMALL_NUMBER)
			return false;

		const FVector BoarLocation = Boar.GetActorLocation();
		FVector AwayDirection = (BoarLocation - ThreatLocation).GetSafeNormal2D();
		// プレイヤーと重なっている場合は正面へ抜ける。
		if (AwayDirection.IsNearlyZero())
		{
			AwayDirection = Boar.GetActorForwardVector().GetSafeNormal2D();
		}

		// 即座に移動完了しないよう、近すぎる投影結果は採用しない。
		const float MinimumEscapeDistanceSq = FMath::Square(FMath::Min(MinEscapeDistance, EscapeDistance * MinEscapeDistanceRatio));
		const float CurrentThreatDistanceSq = FVector::DistSquared2D(BoarLocation, ThreatLocation);

		for (const float Angle : EscapeCandidateAngles)
		{
			const FVector Direction = AwayDirection.RotateAngleAxis(Angle, FVector::UpVector);
			FNavLocation Result;
			if (!Nav.ProjectPointToNavigation(BoarLocation + Direction * EscapeDistance, Result))
				continue;
			if (FVector::DistSquared(BoarLocation, Result.Location) < MinimumEscapeDistanceSq)
				continue;
			if (FVector::DistSquared2D(Result.Location, ThreatLocation) <= CurrentThreatDistanceSq)
				continue;

			OutLocation = Result.Location;
			return true;
		}

		// 角に追い込まれた場合は周囲の到達可能地点から、プレイヤーから最も遠いものを選ぶ。
		// プレイヤーより遠くならない地点しかなくても、停止し続けるよりは移動を優先する。
		bool bFound = false;
		float BestThreatDistanceSq = -1.0f;
		for (int32 Attempt = 0; Attempt < RandomEscapeAttempts; ++Attempt)
		{
			FNavLocation Result;
			if (!Nav.GetRandomReachablePointInRadius(BoarLocation, EscapeDistance * 1.5f, Result))
				continue;
			if (FVector::DistSquared(BoarLocation, Result.Location) < MinimumEscapeDistanceSq)
				continue;

			const float ThreatDistanceSq = FVector::DistSquared2D(Result.Location, ThreatLocation);
			if (ThreatDistanceSq > BestThreatDistanceSq)
			{
				BestThreatDistanceSq = ThreatDistanceSq;
				OutLocation = Result.Location;
				bFound = true;
			}
		}
		return bFound;
	}

	/** スタミナ切れで停止すべき状態か返す。減速のみの設定では逃走を続ける。 */
	bool IsStoppedByStamina(const ABoarBase& Boar)
	{
		return Boar.ShouldStopWhileRecoveringStamina() && Boar.IsStaminaRecovering();
	}
}

EStateTreeRunStatus FStateTreeBoarEscapeTask::EnterState(FStateTreeExecutionContext& Context,
                                                         const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());

	if (Boar == nullptr)
		return EStateTreeRunStatus::Failed;
	if (InstanceData.TargetPlayer == nullptr)
		return EStateTreeRunStatus::Failed;
	// 回復が終わるまでは逃走を再開せず、回復Stateへ譲る。
	if (IsStoppedByStamina(*Boar))
		return EStateTreeRunStatus::Failed;
	// このワールドで経路探索を担当するNavigationSystemを取得する。
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Boar->GetWorld());

	if (Nav == nullptr)
		return EStateTreeRunStatus::Failed;

	if (!FindEscapeLocation(*Boar, InstanceData.TargetPlayer->GetActorLocation(), *Nav, MinEscapeDistance, MinEscapeDistanceRatio, InstanceData.EscapeLocation))
		return EStateTreeRunStatus::Failed;

	InstanceData.ReplanTimeRemaining = ReplanInterval;
	Boar->SetEscaping(true);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeBoarEscapeTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());
	if (Boar == nullptr)
		return EStateTreeRunStatus::Failed;

	// スタミナ切れはState失敗として返し、StateTreeを回復Stateへ再選択させる。
	if (IsStoppedByStamina(*Boar))
		return EStateTreeRunStatus::Failed;

	if (InstanceData.TargetPlayer == nullptr || ReplanInterval <= 0.0f)
		return EStateTreeRunStatus::Running;

	InstanceData.ReplanTimeRemaining -= DeltaTime;
	if (InstanceData.ReplanTimeRemaining > 0.0f)
		return EStateTreeRunStatus::Running;
	InstanceData.ReplanTimeRemaining = ReplanInterval;

	// 逃走先がプレイヤー側へ向いた場合だけ再計算し、通常時は経路を毎回引き直さない。
	const FVector BoarLocation = Boar->GetActorLocation();
	const FVector ToDestination = (InstanceData.EscapeLocation - BoarLocation).GetSafeNormal2D();
	const FVector ToPlayer = (InstanceData.TargetPlayer->GetActorLocation() - BoarLocation).GetSafeNormal2D();
	if (FVector::DotProduct(ToDestination, ToPlayer) <= 0.0f)
		return EStateTreeRunStatus::Running;

	// 更新した逃走先はMove ToのTrack Moving Goalが有効な場合に経路へ反映される。
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Boar->GetWorld()))
	{
		FindEscapeLocation(*Boar, InstanceData.TargetPlayer->GetActorLocation(), *Nav, MinEscapeDistance, MinEscapeDistanceRatio, InstanceData.EscapeLocation);
	}
	return EStateTreeRunStatus::Running;
}

void FStateTreeBoarEscapeTask::ExitState(FStateTreeExecutionContext& Context,
                                         const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get()))
	{
		Boar->SetEscaping(false);
	}
}
