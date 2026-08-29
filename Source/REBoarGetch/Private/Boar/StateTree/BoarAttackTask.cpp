#include "Boar/StateTree/BoarAttackTask.h"

#include "Boar/BoarBase.h"
#include "Cage/Cage.h"

#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FStateTreeBoarAttackTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());

	ACage* Cage = InstanceData.TargetCage.Get();

	if (Cage == nullptr)
		return EStateTreeRunStatus::Failed;

	if (Boar == nullptr)
		return EStateTreeRunStatus::Failed;
	

	// 再利用されるインスタンスデータを攻撃開始時の予兆状態へ戻す。
	InstanceData.Phase = EBoarChargeAttackPhase::Telegraph;
	InstanceData.PhaseElapsedTime = 0.0f;
	InstanceData.ChargeDirection = FVector::ForwardVector;
	InstanceData.RetreatStartLocation = Boar->GetActorLocation();
	InstanceData.bCleanupCompleted = false;

	// 攻撃終了時に必ず戻せるよう、変更前の速度を保存してから移動を停止する。
	if (UCharacterMovementComponent* Movement = Boar->GetCharacterMovement())
	{
		InstanceData.OriginalWalkSpeed = Movement->MaxWalkSpeed;
		Movement->StopMovementImmediately();
	}

	if (AAIController* Controller = Cast<AAIController>(Boar->GetController()))
	{
		Controller->StopMovement();
	}

	const FVector ToCage = (Cage->GetActorLocation() - Boar->GetActorLocation()).GetSafeNormal2D();
	if (!ToCage.IsNearlyZero())
	{
		Boar->SetActorRotation(ToCage.Rotation());
	}

	Boar->OnAttackTelegraphStarted(
		Boar->GetAttackTelegraphDuration(), Boar->GetAttackTelegraphPlayRate());
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeBoarAttackTask::Tick(
	FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());
	ACage* Cage = InstanceData.TargetCage.Get();
	if (Boar == nullptr || Cage == nullptr || Cage->GetIsCageDestroyed() || Boar->IsCaptured())
	{
		FinishAttack(InstanceData, Boar);
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.PhaseElapsedTime += FMath::Max(DeltaTime, 0.0f);
	UCharacterMovementComponent* Movement = Boar->GetCharacterMovement();

	switch (InstanceData.Phase)
	{
	case EBoarChargeAttackPhase::Telegraph:
		// 予兆時間中はその場に留まり、終了時点の檻方向を突進方向として固定する。
		if (InstanceData.PhaseElapsedTime < Boar->GetAttackTelegraphDuration())
		{
			return EStateTreeRunStatus::Running;
		}

		InstanceData.Phase = EBoarChargeAttackPhase::Charge;
		InstanceData.PhaseElapsedTime = 0.0f;
		InstanceData.ChargeDirection =
			(Cage->GetActorLocation() - Boar->GetActorLocation()).GetSafeNormal2D();
		if (InstanceData.ChargeDirection.IsNearlyZero())
		{
			FinishAttack(InstanceData, Boar);
			return EStateTreeRunStatus::Failed;
		}

		Boar->SetActorRotation(InstanceData.ChargeDirection.Rotation());
		if (Movement)
		{
			Movement->MaxWalkSpeed = Boar->GetChargeSpeed();
		}
		Boar->OnChargeStarted();
		return EStateTreeRunStatus::Running;

	case EBoarChargeAttackPhase::Charge:
	{
		// 檻のCollision表面までの距離で着弾を判定し、未着弾なら固定方向へ進み続ける。
		FVector ClosestPointOnCage;
		const float DistanceToCage = Cage->ActorGetDistanceToCollision(
			Boar->GetActorLocation(), ECC_Pawn, ClosestPointOnCage);
		const bool bReachedCage = DistanceToCage >= 0.0f
			&& DistanceToCage <= Boar->GetChargeImpactDistance();

		// 檻が移動・消失しても突進が永久継続しないよう最大時間でも打ち切る。
		if (!bReachedCage && InstanceData.PhaseElapsedTime < Boar->GetChargeMaxDuration())
		{
			Boar->AddMovementInput(InstanceData.ChargeDirection, 1.0f);
			return EStateTreeRunStatus::Running;
		}

		if (bReachedCage)
		{
			Cage->ApplyDamage(Boar->GetCageAttackDamage());
		}

		if (Movement)
		{
			Movement->StopMovementImmediately();
			Movement->MaxWalkSpeed = Boar->GetAttackRetreatSpeed();
		}
		InstanceData.Phase = EBoarChargeAttackPhase::Retreat;
		InstanceData.PhaseElapsedTime = 0.0f;
		InstanceData.RetreatStartLocation = Boar->GetActorLocation();
		return EStateTreeRunStatus::Running;
	}

	case EBoarChargeAttackPhase::Retreat:
		// 着弾地点から所定距離だけ突進方向の逆へ後退し、通常AIへ制御を返す。
		if (FVector::DistSquared2D(Boar->GetActorLocation(), InstanceData.RetreatStartLocation)
			>= FMath::Square(Boar->GetAttackRetreatDistance()))
		{
			FinishAttack(InstanceData, Boar);
			return EStateTreeRunStatus::Succeeded;
		}

		Boar->AddMovementInput(-InstanceData.ChargeDirection, 1.0f);
		return EStateTreeRunStatus::Running;
	}

	FinishAttack(InstanceData, Boar);
	return EStateTreeRunStatus::Failed;
}

void FStateTreeBoarAttackTask::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FinishAttack(InstanceData, Cast<ABoarBase>(InstanceData.NPC.Get()));
}

void FStateTreeBoarAttackTask::FinishAttack(
	FInstanceDataType& InstanceData, ABoarBase* Boar) const
{
	// Tick完了とExitStateの両方から呼ばれるため、復元処理は一度だけ実行する。
	if (Boar == nullptr || InstanceData.bCleanupCompleted)
		return;

	InstanceData.bCleanupCompleted = true;

	if (UCharacterMovementComponent* Movement = Boar->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->MaxWalkSpeed = InstanceData.OriginalWalkSpeed;
	}

	Boar->OnChargeAttackFinished();
}
