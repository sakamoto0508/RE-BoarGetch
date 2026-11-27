#include "Boar/StateTree/BoarAttackTask.h"

#include "Boar/BoarBase.h"
#include "Cage/Cage.h"

#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "StateTreeExecutionContext.h"

namespace BoarAttackCage
{
	/** CharacterMovementが無いときの旋回速度（度/秒）。CharacterMovementのRotation Rateの既定値と同じ。 */
	constexpr float FallbackTurnRate = 360.0f;

	/** 旋回速度の範囲で指定方向へ向き直らせる。 */
	void TurnTowards(ABoarBase* Boar, const FVector& Direction, float DeltaTime)
	{
		if (Direction.IsNearlyZero())
			return;

		const UCharacterMovementComponent* Movement = Boar->GetCharacterMovement();
		const float TurnRate = Movement ? Movement->RotationRate.Yaw : FallbackTurnRate;
		const FRotator NewRotation = FMath::RInterpConstantTo(
			Boar->GetActorRotation(), Direction.Rotation(), DeltaTime, TurnRate);
		Boar->SetActorRotation(FRotator(0.0f, NewRotation.Yaw, 0.0f));
	}

	/** 檻への水平方向。檻が無効なら零ベクトルを返す。 */
	FVector GetDirectionToCage(const ABoarBase* Boar, const ACage* Cage)
	{
		if (Cage == nullptr || Cage->GetIsCageDestroyed())
			return FVector::ZeroVector;

		return (Cage->GetActorLocation() - Boar->GetActorLocation()).GetSafeNormal2D();
	}
}

EStateTreeRunStatus FStateTreeBoarAttackTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	(void)Transition;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());
	ACage* Cage = InstanceData.TargetCage.Get();

	if (Cage == nullptr || Cage->GetIsCageDestroyed())
		return EStateTreeRunStatus::Failed;

	if (Boar == nullptr || Boar->IsCaptured())
		return EStateTreeRunStatus::Failed;

	// 再利用されるインスタンスデータを攻撃開始時の予兆状態へ戻す。
	InstanceData.Phase = EBoarChargeAttackPhase::Telegraph;
	InstanceData.PhaseElapsedTime = 0.0f;
	InstanceData.ChargeDirection = Boar->GetActorForwardVector().GetSafeNormal2D();
	InstanceData.RetreatStartLocation = Boar->GetActorLocation();
	InstanceData.bChargeFinishedNotified = false;
	InstanceData.bCleanupCompleted = false;

	// 攻撃終了時に必ず戻せるよう、変更前の速度と回転設定を保存してから移動を停止する。
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

	Boar->OnAttackTelegraphStarted(
		Boar->GetAttackTelegraphDuration(), Boar->GetAttackTelegraphPlayRate());
	Boar->PlayAttackTelegraphSound();
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeBoarAttackTask::Tick(
	FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get());
	ACage* Cage = InstanceData.TargetCage.Get();
	if (Boar == nullptr || Boar->IsCaptured())
	{
		FinishAttack(InstanceData, Boar);
		return EStateTreeRunStatus::Failed;
	}

	const float SafeDeltaTime = FMath::Max(DeltaTime, 0.0f);
	InstanceData.PhaseElapsedTime += SafeDeltaTime;
	UCharacterMovementComponent* Movement = Boar->GetCharacterMovement();
	const FVector ToCage = BoarAttackCage::GetDirectionToCage(Boar, Cage);

	switch (InstanceData.Phase)
	{
	case EBoarChargeAttackPhase::Telegraph:
		// 突進前に檻が壊れた・消えた場合は攻撃を取りやめる。
		if (ToCage.IsNearlyZero())
		{
			FinishAttack(InstanceData, Boar);
			return EStateTreeRunStatus::Failed;
		}

		// 予兆中はその場に留まり、旋回速度の範囲で檻へ向き直る（瞬時に向きを変えない）。
		BoarAttackCage::TurnTowards(Boar, ToCage, SafeDeltaTime);

		if (InstanceData.PhaseElapsedTime < Boar->GetAttackTelegraphDuration())
			return EStateTreeRunStatus::Running;

		// 予兆終了時点の檻方向を突進方向として固定し、以後は追尾しない。
		InstanceData.ChargeDirection = ToCage;
		InstanceData.Phase = EBoarChargeAttackPhase::Charge;
		InstanceData.PhaseElapsedTime = 0.0f;
		Boar->SetActorRotation(InstanceData.ChargeDirection.Rotation());

		if (Movement)
		{
			Movement->MaxWalkSpeed = Boar->GetChargeSpeed();
		}

		Boar->BeginChargeHitTracking();
		Boar->OnChargeStarted();
		Boar->PlayBoarSound(Boar->GetAudioSettings().ChargeStart);
		return EStateTreeRunStatus::Running;

	case EBoarChargeAttackPhase::Charge:
	{
		// 檻のCollision表面までの距離で着弾を判定する。
		bool bReachedCage = false;
		FVector ClosestPointOnCage = Boar->GetActorLocation();
		float DistanceToCage = -1.0f;
		if (!ToCage.IsNearlyZero())
		{
			DistanceToCage = Cage->ActorGetDistanceToCollision(
				Boar->GetActorLocation(), ECC_Pawn, ClosestPointOnCage);
			bReachedCage = DistanceToCage >= 0.0f
				&& DistanceToCage <= Boar->GetChargeImpactDistance();
		}

		// 未着弾なら固定方向へ進み続ける。壁・他のイノシシ等への接触や最大時間でも打ち切り、
		// 何かに引っかかったまま突進し続けないようにする。
		if (!bReachedCage && !ToCage.IsNearlyZero() && !Boar->HasChargeBlockingHit()
			&& InstanceData.PhaseElapsedTime < Boar->GetChargeMaxDuration())
		{
			Boar->AddMovementInput(InstanceData.ChargeDirection, 1.0f);
			return EStateTreeRunStatus::Running;
		}

		if (bReachedCage)
		{
			// 命中位置（檻のCollision表面）と突進方向を渡す。ダメージ量・判定は従来どおりで、命中VFXの位置と向きにだけ使う。
			Cage->ApplyAttackDamage(Boar->GetCageAttackDamage(),
				DistanceToCage > 0.0f ? ClosestPointOnCage : Boar->GetActorLocation(), InstanceData.ChargeDirection);
		}

		BeginRetreat(InstanceData, Boar);
		return EStateTreeRunStatus::Running;
	}

	case EBoarChargeAttackPhase::Retreat:
	{
		// 檻の方を向いたまま、檻から離れる方向へ所定距離だけ後退する。
		// 檻が壊れた場合は突進方向の逆へ下がる。
		const FVector FaceDirection = ToCage.IsNearlyZero() ? InstanceData.ChargeDirection : ToCage;
		BoarAttackCage::TurnTowards(Boar, FaceDirection, SafeDeltaTime);

		// 壁などで後退できない場合に止まり続けないよう、想定時間の1.5倍＋0.2秒で打ち切る。
		const float RetreatSpeed = FMath::Max(Boar->GetAttackRetreatSpeed(), 1.0f);
		constexpr float RetreatTimeScale = 1.5f;
		constexpr float RetreatTimeMargin = 0.2f;
		const float RetreatTimeLimit = Boar->GetAttackRetreatDistance() / RetreatSpeed * RetreatTimeScale + RetreatTimeMargin;
		const bool bReachedDistance = FVector::DistSquared2D(Boar->GetActorLocation(), InstanceData.RetreatStartLocation)
			>= FMath::Square(Boar->GetAttackRetreatDistance());

		if (!bReachedDistance && InstanceData.PhaseElapsedTime < RetreatTimeLimit)
		{
			Boar->AddMovementInput(-FaceDirection, 1.0f);
			return EStateTreeRunStatus::Running;
		}

		RestoreRotationSettings(InstanceData, Boar);

		if (Movement)
		{
			Movement->StopMovementImmediately();
			Movement->MaxWalkSpeed = InstanceData.OriginalWalkSpeed;
		}

		// 連続突進にならないよう必ず硬直を挟み、完了後はStateTreeへ判断を返す。
		InstanceData.Phase = EBoarChargeAttackPhase::Recovery;
		InstanceData.PhaseElapsedTime = 0.0f;
		return EStateTreeRunStatus::Running;
	}

	case EBoarChargeAttackPhase::Recovery:
		// 硬直中も檻の方を向かせ、終了時に檻が視野角内に残るようにする。
		BoarAttackCage::TurnTowards(Boar, ToCage, SafeDeltaTime);

		if (InstanceData.PhaseElapsedTime < Boar->GetAttackRecoveryDuration())
			return EStateTreeRunStatus::Running;

		FinishAttack(InstanceData, Boar);
		return EStateTreeRunStatus::Succeeded;

	default:
		break;
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

void FStateTreeBoarAttackTask::BeginRetreat(
	FInstanceDataType& InstanceData, ABoarBase* Boar) const
{
	Boar->EndChargeHitTracking();

	if (UCharacterMovementComponent* Movement = Boar->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->MaxWalkSpeed = Boar->GetAttackRetreatSpeed();

		// 後退中は移動方向ではなく檻の方を向かせるため、移動による自動回転を止める。
		// これをしないと後ろへ下がる際に180°振り向き、檻が視野角外へ外れて認識を失う。
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = false;
	}

	InstanceData.Phase = EBoarChargeAttackPhase::Retreat;
	InstanceData.PhaseElapsedTime = 0.0f;
	InstanceData.RetreatStartLocation = Boar->GetActorLocation();

	// 突進アニメーションを突進終了に合わせて終了させる。
	InstanceData.bChargeFinishedNotified = true;
	Boar->OnChargeAttackFinished();
	Boar->PlayBoarSound(Boar->GetAudioSettings().ChargeEnd);
}

void FStateTreeBoarAttackTask::RestoreRotationSettings(
	const FInstanceDataType& InstanceData, ABoarBase* Boar) const
{
	if (UCharacterMovementComponent* Movement = Boar->GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = InstanceData.bOriginalOrientRotationToMovement;
		Movement->bUseControllerDesiredRotation = InstanceData.bOriginalUseControllerDesiredRotation;
	}
}

void FStateTreeBoarAttackTask::FinishAttack(
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

	// 予兆中・突進中に中断された場合も、アニメーションを通常状態へ戻す。
	if (!InstanceData.bChargeFinishedNotified)
	{
		InstanceData.bChargeFinishedNotified = true;
		Boar->OnChargeAttackFinished();
	}
}
