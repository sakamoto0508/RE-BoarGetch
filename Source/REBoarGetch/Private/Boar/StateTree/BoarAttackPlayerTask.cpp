#include "Boar/StateTree/BoarAttackPlayerTask.h"

#include "Boar/BoarBase.h"

#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FStateTreeBoarAttackPlayerTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());
	if (Boar == nullptr || Boar->IsCaptured())
		return EStateTreeRunStatus::Failed;

	// Global Senseが同フレームで更新した認識結果から攻撃対象を確定する。
	InstanceData.TargetPlayer = Boar->GetPerceivedPlayer();
	if (InstanceData.TargetPlayer == nullptr)
		return EStateTreeRunStatus::Failed;

	InstanceData.ChargeDirection = Boar->GetActorForwardVector().GetSafeNormal2D();
	InstanceData.bCleanupCompleted = false;

	// 攻撃終了時に必ず戻せるよう、変更前の速度と回転設定を保存してから追跡移動を停止する。
	if (UCharacterMovementComponent* Movement = Boar->GetCharacterMovement())
	{
		InstanceData.OriginalWalkSpeed = Movement->MaxWalkSpeed;
		InstanceData.bOriginalOrientRotationToMovement = Movement->bOrientRotationToMovement;
		InstanceData.bOriginalUseControllerDesiredRotation = Movement->bUseControllerDesiredRotation;
		Movement->StopMovementImmediately();
	}

	if (AAIController* Controller = Cast<AAIController>(Boar->GetController()))
	{
		Controller->StopMovement();
	}

	BeginTelegraph(InstanceData, Boar);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeBoarAttackPlayerTask::Tick(
	FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());
	if (Boar == nullptr || Boar->IsCaptured())
	{
		FinishAttack(InstanceData, Boar);
		return EStateTreeRunStatus::Failed;
	}

	const float SafeDeltaTime = FMath::Max(DeltaTime, 0.0f);
	InstanceData.PhaseElapsedTime += SafeDeltaTime;
	UCharacterMovementComponent* Movement = Boar->GetCharacterMovement();

	switch (InstanceData.Phase)
	{
	case EBoarChargeAttackPhase::Telegraph:
	{
		// 予兆中はその場に留まり、旋回速度の範囲でプレイヤーへ向き直る。
		const APawn* Player = InstanceData.TargetPlayer.Get();
		if (Player == nullptr)
		{
			FinishAttack(InstanceData, Boar);
			return EStateTreeRunStatus::Failed;
		}

		const FVector ToPlayer = (Player->GetActorLocation() - Boar->GetActorLocation()).GetSafeNormal2D();
		if (!ToPlayer.IsNearlyZero())
		{
			const float TurnRate = Movement ? Movement->RotationRate.Yaw : 360.0f;
			const FRotator NewRotation = FMath::RInterpConstantTo(
				Boar->GetActorRotation(), ToPlayer.Rotation(), SafeDeltaTime, TurnRate);
			Boar->SetActorRotation(FRotator(0.0f, NewRotation.Yaw, 0.0f));
		}

		if (InstanceData.PhaseElapsedTime < Boar->GetAttackTelegraphDuration())
		{
			return EStateTreeRunStatus::Running;
		}

		// 予兆終了時点のプレイヤー位置から突進方向を決め、以後は追尾しない。
		if (!ToPlayer.IsNearlyZero())
		{
			InstanceData.ChargeDirection = ToPlayer;
		}

		InstanceData.Phase = EBoarChargeAttackPhase::Charge;
		InstanceData.PhaseElapsedTime = 0.0f;
		Boar->SetActorRotation(InstanceData.ChargeDirection.Rotation());
		if (Movement)
		{
			Movement->MaxWalkSpeed = Boar->GetChargeSpeed();
		}
		Boar->BeginChargeHitTracking();
		Boar->OnChargeStarted();
		return EStateTreeRunStatus::Running;
	}

	case EBoarChargeAttackPhase::Charge:
		// プレイヤー・壁などへの接触、または最大継続時間で突進を終了する。
		if (!Boar->HasChargeBlockingHit() && InstanceData.PhaseElapsedTime < Boar->GetChargeMaxDuration())
		{
			Boar->AddMovementInput(InstanceData.ChargeDirection, 1.0f);
			return EStateTreeRunStatus::Running;
		}

		BeginRetreat(InstanceData, Boar);
		return EStateTreeRunStatus::Running;

	case EBoarChargeAttackPhase::Retreat:
	{
		// プレイヤーの方を向いたまま、プレイヤーから離れる方向へ所定距離だけ後退する。
		// 見失っている場合は突進方向の逆へ下がる。
		APawn* Player = Boar->GetPerceivedPlayer();
		const FVector ToPlayer = Player
			? (Player->GetActorLocation() - Boar->GetActorLocation()).GetSafeNormal2D()
			: FVector::ZeroVector;
		const FVector RetreatDirection = ToPlayer.IsNearlyZero() ? -InstanceData.ChargeDirection : -ToPlayer;
		const FVector FaceDirection = ToPlayer.IsNearlyZero() ? InstanceData.ChargeDirection : ToPlayer;
		const float TurnRate = Movement ? Movement->RotationRate.Yaw : 360.0f;
		const FRotator NewRotation = FMath::RInterpConstantTo(
			Boar->GetActorRotation(), FaceDirection.Rotation(), SafeDeltaTime, TurnRate);
		Boar->SetActorRotation(FRotator(0.0f, NewRotation.Yaw, 0.0f));

		// 壁などで後退できない場合に止まり続けないよう、想定時間を超えたら打ち切る。
		const float RetreatSpeed = FMath::Max(Boar->GetAttackRetreatSpeed(), 1.0f);
		const float RetreatTimeLimit = Boar->GetAttackRetreatDistance() / RetreatSpeed * 1.5f + 0.2f;
		const bool bReachedDistance = FVector::DistSquared2D(Boar->GetActorLocation(), InstanceData.RetreatStartLocation)
			>= FMath::Square(Boar->GetAttackRetreatDistance());
		if (!bReachedDistance && InstanceData.PhaseElapsedTime < RetreatTimeLimit)
		{
			Boar->AddMovementInput(RetreatDirection, 1.0f);
			return EStateTreeRunStatus::Running;
		}

		RestoreRotationSettings(InstanceData, Boar);
		if (Movement)
		{
			Movement->StopMovementImmediately();
			Movement->MaxWalkSpeed = InstanceData.OriginalWalkSpeed;
		}

		// 檻攻撃と同じく、プレイヤーを認識し続けていれば再び予兆から突進する。
		if (Player != nullptr)
		{
			InstanceData.TargetPlayer = Player;
			BeginTelegraph(InstanceData, Boar);
			return EStateTreeRunStatus::Running;
		}

		// 見失った場合は短く硬直してから、StateTreeで檻・巡回を再判断させる。
		BeginRecovery(InstanceData, Boar);
		return EStateTreeRunStatus::Running;
	}

	case EBoarChargeAttackPhase::Recovery:
		// 硬直終了後はStateTreeへ制御を返し、Senseの最新結果で追跡・檻・巡回を再判断させる。
		if (InstanceData.PhaseElapsedTime < Boar->GetAttackRecoveryDuration())
		{
			return EStateTreeRunStatus::Running;
		}

		FinishAttack(InstanceData, Boar);
		return EStateTreeRunStatus::Succeeded;

	default:
		break;
	}

	FinishAttack(InstanceData, Boar);
	return EStateTreeRunStatus::Failed;
}

void FStateTreeBoarAttackPlayerTask::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FinishAttack(InstanceData, Cast<ABoarBase>(InstanceData.NPC.Get()));
}

void FStateTreeBoarAttackPlayerTask::BeginRetreat(
	FInstanceDataType& InstanceData, ABoarBase* Boar) const
{
	Boar->EndChargeHitTracking();
	if (UCharacterMovementComponent* Movement = Boar->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->MaxWalkSpeed = Boar->GetAttackRetreatSpeed();
		// 後退中は移動方向ではなくプレイヤー方向を向かせるため、移動による自動回転を止める。
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = false;
	}

	InstanceData.Phase = EBoarChargeAttackPhase::Retreat;
	InstanceData.PhaseElapsedTime = 0.0f;
	InstanceData.RetreatStartLocation = Boar->GetActorLocation();

	// 突進アニメーションを突進終了に合わせて終了させる。
	// クールダウンも突進終了から数え、プレイヤーを見失って再判断した後も次の突進へ移りやすくする。
	InstanceData.bChargeFinishedNotified = true;
	Boar->OnChargeAttackFinished();
	Boar->StartPlayerAttackCooldown();
}

void FStateTreeBoarAttackPlayerTask::BeginTelegraph(
	FInstanceDataType& InstanceData, ABoarBase* Boar) const
{
	InstanceData.Phase = EBoarChargeAttackPhase::Telegraph;
	InstanceData.PhaseElapsedTime = 0.0f;
	InstanceData.bChargeFinishedNotified = false;
	Boar->OnAttackTelegraphStarted(
		Boar->GetAttackTelegraphDuration(), Boar->GetAttackTelegraphPlayRate());
}

void FStateTreeBoarAttackPlayerTask::RestoreRotationSettings(
	const FInstanceDataType& InstanceData, ABoarBase* Boar) const
{
	if (UCharacterMovementComponent* Movement = Boar->GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = InstanceData.bOriginalOrientRotationToMovement;
		Movement->bUseControllerDesiredRotation = InstanceData.bOriginalUseControllerDesiredRotation;
	}
}

void FStateTreeBoarAttackPlayerTask::BeginRecovery(
	FInstanceDataType& InstanceData, ABoarBase* Boar) const
{
	if (UCharacterMovementComponent* Movement = Boar->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->MaxWalkSpeed = InstanceData.OriginalWalkSpeed;
	}

	InstanceData.Phase = EBoarChargeAttackPhase::Recovery;
	InstanceData.PhaseElapsedTime = 0.0f;
}

void FStateTreeBoarAttackPlayerTask::FinishAttack(
	FInstanceDataType& InstanceData, ABoarBase* Boar) const
{
	// Tick完了とExitStateの両方から呼ばれるため、復元処理は一度だけ実行する。
	if (Boar == nullptr || InstanceData.bCleanupCompleted)
		return;

	InstanceData.bCleanupCompleted = true;
	Boar->EndChargeHitTracking();
	RestoreRotationSettings(InstanceData, Boar);

	if (UCharacterMovementComponent* Movement = Boar->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->MaxWalkSpeed = InstanceData.OriginalWalkSpeed;
	}

	// 予兆中・突進中に中断された場合も、アニメーションを通常状態へ戻し、
	// 予兆が連続で再開しないようクールダウンを開始する。
	if (!InstanceData.bChargeFinishedNotified)
	{
		InstanceData.bChargeFinishedNotified = true;
		Boar->OnChargeAttackFinished();
		Boar->StartPlayerAttackCooldown();
	}
}
