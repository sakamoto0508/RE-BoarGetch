#include "Boar/StateTree/BoarEscapeTask.h"

#include "Boar/BoarBase.h"

#include "AIController.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
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

	/** 維持方向の延長で試す角度です。NavMeshの曲がり・障害物の縁を避ける程度の小さな振れ幅（止まらずに曲がれる範囲）に留めます。 */
	constexpr float HeadingCandidateAngles[] = {0.0f, 20.0f, -20.0f};
	/** 直進できないときに探す到達可能な方向です（維持方向からの角度）。 */
	constexpr float OpenDirectionAngles[] = {30.0f, -30.0f, 60.0f, -60.0f, 90.0f, -90.0f, 120.0f, -120.0f, 150.0f, -150.0f, 180.0f};
	/** NavMesh Raycastが壁に当たったとき、壁際ぎりぎりを目的地にしないよう手前へ戻す距離(cm)です。 */
	constexpr float HeadingWallPullback = 50.0f;

	/**
	 * Startから方向DirectionへNavMesh上を直線で進める距離（最大Length）を返す。壁・NavMesh端に当たったら手前で止める。
	 * NavMesh上に投影できない場合は0を返す。
	 */
	float NavClearDistance(ABoarBase& Boar, const FVector& Start, const FVector& Direction, float Length)
	{
		FVector Hit;
		if (UNavigationSystemV1::NavigationRaycast(&Boar, Start, Start + Direction * Length, Hit, {}, Boar.GetController()))
		{
			return FMath::Max(0.0f, FVector::Dist2D(Start, Hit) - HeadingWallPullback);
		}
		return Length;
	}

	/**
	 * 維持している逃走方向へ、次の直進区間の逃走先を延長する（CommittedRun）。小さな方向修正（±20°）だけを許し、
	 * NavMesh上の直線でMinimumDistance以上進めなければ失敗（＝直進できない）とする。
	 */
	bool FindLocationAlongHeading(ABoarBase& Boar, const FVector& Heading, UNavigationSystemV1& Nav,
		float Length, float MinimumDistance, FVector& OutLocation)
	{
		FNavLocation Start;
		if (Length <= KINDA_SMALL_NUMBER || !Nav.ProjectPointToNavigation(Boar.GetActorLocation(), Start))
			return false;

		for (const float Angle : HeadingCandidateAngles)
		{
			const FVector Direction = Heading.RotateAngleAxis(Angle, FVector::UpVector);
			const float Clear = NavClearDistance(Boar, Start.Location, Direction, Length);
			if (Clear < MinimumDistance)
				continue;
			FNavLocation Result;
			if (!Nav.ProjectPointToNavigation(Start.Location + Direction * Clear, Result))
				continue;
			OutLocation = Result.Location;
			return true;
		}
		return false;
	}

	/**
	 * 維持方向へ直進できないとき、到達可能（NavMesh上を直線でMinimumDistance以上進める）な別方向を探す。
	 * プレイヤーから離れる方向を最優先し、開けている方向・今の向きに近い方向を次に評価する。
	 */
	bool FindOpenDirection(ABoarBase& Boar, const FVector& Heading, const FVector& ThreatLocation, UNavigationSystemV1& Nav,
		float Length, float MinimumDistance, FVector& OutDirection, FVector& OutLocation)
	{
		FNavLocation Start;
		if (Length <= KINDA_SMALL_NUMBER || !Nav.ProjectPointToNavigation(Boar.GetActorLocation(), Start))
			return false;
		const FVector AwayDirection = (Start.Location - ThreatLocation).GetSafeNormal2D();

		float BestScore = -BIG_NUMBER;
		bool bFound = false;
		for (const float Angle : OpenDirectionAngles)
		{
			const FVector Direction = Heading.RotateAngleAxis(Angle, FVector::UpVector);
			const float Clear = NavClearDistance(Boar, Start.Location, Direction, Length);
			if (Clear < MinimumDistance)
				continue;
			const float Score = 1.0f * FVector::DotProduct(Direction, AwayDirection)
				+ 0.8f * (Clear / Length)
				+ 0.3f * FVector::DotProduct(Direction, Heading);
			if (Score <= BestScore)
				continue;
			FNavLocation Result;
			if (!Nav.ProjectPointToNavigation(Start.Location + Direction * Clear, Result))
				continue;
			BestScore = Score;
			OutDirection = Direction;
			OutLocation = Result.Location;
			bFound = true;
		}
		return bFound;
	}

	/** 逃走先をどう決めたかです。デバッグログと呼び出し側の適用方法に使います。 */
	enum class EEscapePlan : uint8
	{
		/** 決められなかった（逃走先なし）。 */
		None,
		/** 維持方向へ次の区間を延長した（方向維持中の直進）。 */
		ExtendHeading,
		/** 方向維持時間の終わりの再評価で、方向差が小さいので止まらずに次のN秒の方向を確定した。 */
		Recommit,
		/** 切り返し（BrakeAndPivot）を始めた。理由はABoarBase::GetEscapePivotReason。 */
		Pivot,
		/** 直進できないので、到達可能な別方向へ走りながら曲がる（小さな回避、または切り返しの抑制中）。 */
		ObstacleRecovery,
		/** 維持方向が無い（別Stateを挟んだ直後など）ので、プレイヤーから離れる方向を新しい維持方向にした。 */
		NewHeading,
	};

	/** どの時点の判断かです（AKI-172）。 */
	enum class EPlanContext : uint8
	{
		/** 走行中、逃走先の手前（SegmentEndCheckDistance）に来た。維持方向へ延長するだけで方向は判断しない。 */
		SegmentEnd,
		/** 方向維持時間（DirectionCommitDuration）を過ぎた。認識中のプレイヤーから方向を再評価する。 */
		Reevaluate,
		/** 走るはずなのに止まっている（壁・Actorに詰まった）。N秒を待たず障害物復帰する。 */
		Stuck,
		/** 逃走先に到着した（Sustained再入）。止まっているので切り返しは踏ん張りから始める。 */
		Arrival,
	};

	const TCHAR* EscapePlanName(EEscapePlan Plan)
	{
		switch (Plan)
		{
		case EEscapePlan::ExtendHeading: return TEXT("ExtendHeading");
		case EEscapePlan::Recommit: return TEXT("Recommit");
		case EEscapePlan::Pivot: return TEXT("Pivot");
		case EEscapePlan::ObstacleRecovery: return TEXT("ObstacleRecovery");
		case EEscapePlan::NewHeading: return TEXT("NewHeading");
		default: return TEXT("None");
		}
	}

	const TCHAR* PlanContextName(EPlanContext Context)
	{
		switch (Context)
		{
		case EPlanContext::SegmentEnd: return TEXT("SegmentEnd");
		case EPlanContext::Reevaluate: return TEXT("Reevaluate");
		case EPlanContext::Stuck: return TEXT("Stuck");
		default: return TEXT("Arrival");
		}
	}

	/**
	 * EscapeTurn有効な種別（Normal）の逃走方向の判断（AKI-172：N秒間方向維持方式）。切り返しの処理は理由によらず共通（TryStartEscapePivot）。
	 * - 方向維持中（SegmentEnd）：維持方向（±20°の小さな修正まで）へ次の区間を延長するだけ。プレイヤーの位置では引き直さない。
	 * - 方向維持時間の終わり（Reevaluate、または到着時に経過済み）：認識中のプレイヤーから離れる方向と維持方向の角度差を見る。
	 *   PivotAngleThreshold以上なら切り返し（減速→踏ん張り→停止旋回→再加速）、未満なら止まらず新しい方向で次のN秒を確定する。
	 *   プレイヤーを認識していなければ方向を変えず、逃走の終了は既存の知覚・StateTreeの条件に任せる。
	 * - 直進できない（延長できない・Stuck）：N秒を待たず到達可能な別方向を確定する。大きな方向転換なら切り返し、
	 *   小さな曲がり、または連続切り返しの抑制中は走りながら曲がる（ObstacleRecovery）。どちらも新しい方向で方向維持を始め直す。
	 * RemainingDistance：今の逃走先までの残り距離。同じ方向の次の区間はその先へ延長するため、直進可能の判定に足す。
	 */
	EEscapePlan PlanEscapeDestination(ABoarBase& Boar, const APawn* Threat, UNavigationSystemV1& Nav,
		float MinEscapeDistance, float MinEscapeDistanceRatio, EPlanContext Context, float RemainingDistance, FVector& OutLocation)
	{
		const bool bDebug = ABoarBase::IsEscapeTurnDebugEnabled();
		const FBoarEscapeTurnSettings& Settings = Boar.GetEscapeTurnSettings();
		const bool bSkipDecelerate = Context == EPlanContext::Arrival || Context == EPlanContext::Stuck;
		const FVector BoarLocation = Boar.GetActorLocation();
		const float MinRun = Settings.RunSegmentDistance * 0.5f;

		// プレイヤーから離れる逃走先は、使うときだけNavMeshで探す（方向維持中はプレイヤーの位置を使わない）。
		bool bAwayComputed = false;
		bool bHasAway = false;
		FVector AwayLocation = FVector::ZeroVector;
		FVector AwayDirection = FVector::ZeroVector;
		auto ComputeAway = [&]()
		{
			if (bAwayComputed)
				return;
			bAwayComputed = true;
			if (Threat != nullptr
				&& FindEscapeLocation(Boar, Threat->GetActorLocation(), Nav, MinEscapeDistance, MinEscapeDistanceRatio, AwayLocation))
			{
				bHasAway = true;
				AwayDirection = (AwayLocation - BoarLocation).GetSafeNormal2D();
			}
		};

		if (!Boar.HasEscapeHeading())
		{
			ComputeAway();
			if (!bHasAway)
				return EEscapePlan::None;
			Boar.CommitEscapeHeading(AwayDirection, TEXT("NewHeading"));
			OutLocation = AwayLocation;
			return EEscapePlan::NewHeading;
		}

		FVector Heading = Boar.GetEscapeHeading();
		const bool bReevaluate = Context == EPlanContext::Reevaluate
			|| (Context == EPlanContext::Arrival && Boar.IsEscapeCommitExpired());

		if (bReevaluate)
		{
			if (Threat == nullptr)
			{
				if (bDebug)
					Boar.LogEscapeTurn(FString::Printf(TEXT("DirectionReevaluate (%s) NoPerceivedPlayer → 方向を維持 heading=%.0f（逃走終了は既存の知覚・StateTreeに従う）"),
						PlanContextName(Context), Heading.Rotation().Yaw));
				Boar.CommitEscapeHeading(Heading, TEXT("NoPerceivedPlayer"));
			}
			else
			{
				ComputeAway();
				if (bHasAway)
				{
					const float Delta = Boar.GetEscapeReferenceYawDelta(AwayDirection);
					const bool bLargeTurn = FMath::Abs(Delta) >= Settings.PivotAngleThreshold;
					if (bDebug)
					{
						Boar.LogEscapeTurn(FString::Printf(
							TEXT("DirectionReevaluate (%s) heading=%.0f away=%.0f delta=%.1f threshold=%.1f → %s | threatDist=%.0f committed=%.2fs/%.0fcm maxSpeed=%.0f"),
							PlanContextName(Context), Heading.Rotation().Yaw, AwayDirection.Rotation().Yaw, Delta, Settings.PivotAngleThreshold,
							bLargeTurn ? TEXT("大きな方向差：切り返し") : TEXT("小さな方向差：向きを変えず直進"),
							Boar.GetEscapeThreatDistance(), Boar.GetEscapeCommitElapsed(), Boar.GetEscapeCommitDistance(),
							Boar.GetEscapeEffectiveMaxSpeed()));
					}
					if (bLargeTurn)
					{
						if (Boar.TryStartEscapePivot(AwayDirection, bSkipDecelerate, EBoarEscapePivotReason::Reevaluate))
						{
							OutLocation = AwayLocation;
							return EEscapePlan::Pivot;
						}
						// 始められなかった（旋回中など。通常は起きない）：今の方向を維持して次のN秒へ。
						Boar.CommitEscapeHeading(Heading, TEXT("PivotRejected"));
					}
					else
					{
						// 逃走中に走りながら曲がることはしない。向きを変えるのは切り返しだけ（障害物復帰を除く）。
						Boar.CommitEscapeHeading(Heading, TEXT("ReevaluateHold"));
					}
				}
				else
				{
					if (bDebug)
						Boar.LogEscapeTurn(TEXT("DirectionReevaluate: プレイヤーから離れる逃走先が見つからない → 方向を維持"));
					Boar.CommitEscapeHeading(Heading, TEXT("NoAwayLocation"));
				}
			}

			// 確定した方向へ逃走先を引き直す（方向が変わった場合もあるので現在地から測る）。
			if (FindLocationAlongHeading(Boar, Heading, Nav, RemainingDistance + Settings.RunSegmentDistance, MinRun, OutLocation))
				return EEscapePlan::Recommit;
		}
		else if (Context != EPlanContext::Stuck)
		{
			// 方向維持中：同じ方向へ次の区間を延長する。
			if (FindLocationAlongHeading(Boar, Heading, Nav, RemainingDistance + Settings.RunSegmentDistance,
				RemainingDistance + MinRun, OutLocation))
			{
				return EEscapePlan::ExtendHeading;
			}
		}

		// 直進できない：到達可能な次の逃走方向を確定してから向きを変える（N秒の終了は待たない）。
		ComputeAway();
		// 進めない原因がすぐ近くのプレイヤー（体がぶつかって止まった）なら障害物として扱わない（AKI-172）。
		// 走行旋回で回り込まず、離れる方向との差が大きければ切り返す。差が小さければ通常の障害物復帰へ進む。
		if (Context == EPlanContext::Stuck && Threat != nullptr && bHasAway
			&& FVector::Dist2D(BoarLocation, Threat->GetActorLocation()) <= Settings.PlayerBlockDistance)
		{
			const float Delta = Boar.GetEscapeReferenceYawDelta(AwayDirection);
			if (bDebug)
				Boar.LogEscapeTurn(FString::Printf(TEXT("BlockedByPlayer heading=%.0f away=%.0f delta=%.1f threshold=%.1f threatDist=%.0f → %s"),
					Heading.Rotation().Yaw, AwayDirection.Rotation().Yaw, Delta, Settings.PivotAngleThreshold,
					FVector::Dist2D(BoarLocation, Threat->GetActorLocation()),
					FMath::Abs(Delta) >= Settings.PivotAngleThreshold ? TEXT("切り返し") : TEXT("障害物復帰へ")));
			if (FMath::Abs(Delta) >= Settings.PivotAngleThreshold
				&& Boar.TryStartEscapePivot(AwayDirection, true, EBoarEscapePivotReason::Reevaluate))
			{
				OutLocation = AwayLocation;
				return EEscapePlan::Pivot;
			}
		}
		// プレイヤーを認識していないときは、維持方向に近い方向を優先する（脅威を維持方向の真後ろに置いて評価する）。
		const FVector ScoreThreatLocation = Threat != nullptr ? Threat->GetActorLocation() : BoarLocation - Heading * 100.0f;
		FVector OpenDirection;
		FVector OpenLocation;
		if (FindOpenDirection(Boar, Heading, ScoreThreatLocation, Nav, Settings.RunSegmentDistance, MinRun, OpenDirection, OpenLocation))
		{
			OutLocation = OpenLocation;
			const bool bNeedsPivot = Boar.NeedsEscapePivot(OpenDirection, EBoarEscapePivotReason::NaturalObstacle);
			if (bDebug)
				Boar.LogEscapeTurn(FString::Printf(TEXT("ObstacleRecovery (%s) heading=%.0f open=%.0f delta=%.1f obstacleThreshold=%.1f"),
					PlanContextName(Context), Heading.Rotation().Yaw, OpenDirection.Rotation().Yaw,
					Boar.GetEscapeReferenceYawDelta(OpenDirection), Settings.ObstaclePivotAngleThreshold));
			if (bNeedsPivot && Boar.TryStartEscapePivot(OpenDirection, bSkipDecelerate, EBoarEscapePivotReason::NaturalObstacle))
				return EEscapePlan::Pivot;
			// 小さな曲がり、または連続切り返しの抑制中：壁際で停止旋回を繰り返さず、走りながら曲がる。
			Boar.CommitEscapeHeading(OpenDirection, bNeedsPivot ? TEXT("ObstacleRunTurn(Cooldown)") : TEXT("ObstacleSmallTurn"));
			return EEscapePlan::ObstacleRecovery;
		}

		// 開けた方向が無い（角・狭所）：プレイヤーから離れる到達可能地点へ走りながら復帰する。止まり続けない。
		if (!bHasAway)
			return EEscapePlan::None;
		Boar.CommitEscapeHeading(AwayDirection, TEXT("ObstacleFallback"));
		OutLocation = AwayLocation;
		return EEscapePlan::ObstacleRecovery;
	}

	void LogPlanResult(ABoarBase& Boar, EEscapePlan Plan, const FVector& Current, const FVector& Adopted)
	{
		const AAIController* AIController = Cast<AAIController>(Boar.GetController());
		const UPathFollowingComponent* PathFollowing = AIController ? AIController->GetPathFollowingComponent() : nullptr;
		Boar.LogEscapeTurn(FString::Printf(TEXT("PlanResult=%s current=%s adopted=%s heading=%.0f commitRemaining=%.2fs navPathValid=%d"),
			EscapePlanName(Plan), *Current.ToCompactString(), *Adopted.ToCompactString(),
			Boar.HasEscapeHeading() ? Boar.GetEscapeHeading().Rotation().Yaw : 0.0f, Boar.GetEscapeCommitRemaining(),
			PathFollowing && PathFollowing->HasValidPath() ? 1 : 0));
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

	const bool bDebug = ABoarBase::IsEscapeTurnDebugEnabled();
	const FVector ThreatLocation = InstanceData.TargetPlayer->GetActorLocation();
	const bool bSustained = Transition.ChangeType == EStateTreeStateChangeType::Sustained && Boar->IsEscaping();
	InstanceData.ReplanTimeRemaining = ReplanInterval;
	InstanceData.StuckTime = 0.0f;

	// 逃走先へ到着→Rootから同じEscape Stateが再選択された場合（Sustained）は、同じ逃走の継続として扱う（AKI-152）。
	// 逃走開始の高速旋回はやり直さず、方向維持（N秒）もそのまま続ける。
	if (bSustained && Boar->IsEscapeTurnEnabled())
	{
		Boar->SetEscapeThreat(InstanceData.TargetPlayer);
		if (bDebug)
			Boar->LogEscapeTurn(FString::Printf(TEXT("EscapeTaskEnter (Sustained): 逃走先に到着（再Enter。逃走開始旋回はしない）commitRemaining=%.2fs"),
				Boar->GetEscapeCommitRemaining()));
		// 旋回・切り返しの途中：旋回先を変えず、旋回先と同じ向きの逃走先を使い続ける。
		if (Boar->ContinueEscapeMotion())
		{
			if (InstanceData.bHasPendingEscapeLocation)
			{
				InstanceData.EscapeLocation = InstanceData.PendingEscapeLocation;
			}
			else if (FVector::DistSquared2D(InstanceData.EscapeLocation, Boar->GetActorLocation()) < FMath::Square(MinEscapeDistance * 0.5f))
			{
				// 旋回先の逃走先にも既に着いている（まれ）。維持方向（＝旋回先）へ延長し、無理ならプレイヤーから離れる方向を使う。
				FVector Extended;
				const FBoarEscapeTurnSettings& TurnSettings = Boar->GetEscapeTurnSettings();
				if (FindLocationAlongHeading(*Boar, Boar->GetEscapeHeading(), *Nav, TurnSettings.RunSegmentDistance,
						TurnSettings.RunSegmentDistance * 0.5f, Extended)
					|| FindEscapeLocation(*Boar, ThreatLocation, *Nav, MinEscapeDistance, MinEscapeDistanceRatio, Extended))
				{
					InstanceData.EscapeLocation = Extended;
				}
			}
			InstanceData.bHasPendingEscapeLocation = false;
			InstanceData.bSegmentEndChecked = false;
			if (bDebug)
				Boar->LogEscapeTurn(FString::Printf(TEXT("PlanResult=TurnInProgress adopted=%s"), *InstanceData.EscapeLocation.ToCompactString()));
			return EStateTreeRunStatus::Running;
		}

		const FVector Previous = InstanceData.EscapeLocation;
		FVector NextLocation;
		const EEscapePlan Plan = PlanEscapeDestination(*Boar, InstanceData.TargetPlayer, *Nav, MinEscapeDistance, MinEscapeDistanceRatio,
			EPlanContext::Arrival, 0.0f, NextLocation);
		if (Plan == EEscapePlan::None)
		{
			if (bDebug)
				Boar->LogEscapeTurn(TEXT("EscapeTaskEnter (Sustained): 逃走先が見つからない（Failed）"));
			return EStateTreeRunStatus::Failed;
		}
		InstanceData.EscapeLocation = NextLocation;
		InstanceData.bHasPendingEscapeLocation = false;
		InstanceData.bSegmentEndChecked = false;
		if (bDebug)
			LogPlanResult(*Boar, Plan, Previous, NextLocation);
		return EStateTreeRunStatus::Running;
	}

	// 別Stateを挟んで同じ逃走へ戻った場合（Changed）：控えた逃走方向と方向維持の残り時間を引き継ぐ（AKI-172）。
	// プレイヤーの位置で方向を計算し直さず、再入を理由に切り返し・走行旋回・逃走開始旋回をしない。
	if (!bSustained && Boar->IsEscapeTurnEnabled() && Boar->TryResumeEscape(InstanceData.TargetPlayer))
	{
		Boar->SetEscaping(true);
		InstanceData.bHasPendingEscapeLocation = false;
		InstanceData.bSegmentEndChecked = false;
		const FBoarEscapeTurnSettings& TurnSettings = Boar->GetEscapeTurnSettings();
		const bool bTurning = Boar->IsEscapePivoting() || Boar->GetEscapeTurnPhase() == EBoarEscapeTurnPhase::StartTurn;
		FVector Resumed;
		if (FindLocationAlongHeading(*Boar, Boar->GetEscapeHeading(), *Nav, TurnSettings.RunSegmentDistance,
			TurnSettings.RunSegmentDistance * 0.5f, Resumed))
		{
			InstanceData.EscapeLocation = Resumed;
			if (bDebug)
				Boar->LogEscapeTurn(FString::Printf(TEXT("PlanResult=Resume (方向を維持) adopted=%s"), *Resumed.ToCompactString()));
			return EStateTreeRunStatus::Running;
		}
		// 控えた方向へ直進できない：安全なNavMesh復帰を優先する。旋回中は旋回先を変えず、止まっている間の仮の逃走先だけ置く。
		FVector NextLocation = FVector::ZeroVector;
		const EEscapePlan Plan = bTurning ? EEscapePlan::None
			: PlanEscapeDestination(*Boar, InstanceData.TargetPlayer, *Nav, MinEscapeDistance, MinEscapeDistanceRatio,
				EPlanContext::Stuck, 0.0f, NextLocation);
		if (Plan == EEscapePlan::Pivot)
		{
			InstanceData.PendingEscapeLocation = NextLocation;
			InstanceData.bHasPendingEscapeLocation = true;
		}
		else if (Plan != EEscapePlan::None)
		{
			InstanceData.EscapeLocation = NextLocation;
		}
		else if (FindEscapeLocation(*Boar, ThreatLocation, *Nav, MinEscapeDistance, MinEscapeDistanceRatio, NextLocation))
		{
			InstanceData.EscapeLocation = NextLocation;
		}
		else
		{
			InstanceData.EscapeLocation = Boar->GetActorLocation();
		}
		if (bDebug)
			Boar->LogEscapeTurn(FString::Printf(TEXT("PlanResult=Resume (控えた方向へ直進できない → %s) adopted=%s"),
				bTurning ? TEXT("旋回中のため仮の逃走先") : EscapePlanName(Plan),
				*(Plan == EEscapePlan::Pivot ? InstanceData.PendingEscapeLocation : InstanceData.EscapeLocation).ToCompactString()));
		return EStateTreeRunStatus::Running;
	}

	FVector NewLocation;
	if (!FindEscapeLocation(*Boar, ThreatLocation, *Nav, MinEscapeDistance, MinEscapeDistanceRatio, NewLocation))
	{
		if (bDebug)
			Boar->LogEscapeTurn(TEXT("EscapeTaskEnter: 逃走先が見つからない（Failed）"));
		return EStateTreeRunStatus::Failed;
	}
	const FVector NewDirection = (NewLocation - Boar->GetActorLocation()).GetSafeNormal2D();

	// EscapeTurn無効の種別（Red／Blue等）の再選択は従来どおり、プレイヤーから離れる新しい逃走先を使う。
	if (bSustained)
	{
		InstanceData.EscapeLocation = NewLocation;
		InstanceData.bHasPendingEscapeLocation = false;
		return EStateTreeRunStatus::Running;
	}

	InstanceData.EscapeLocation = NewLocation;
	InstanceData.bHasPendingEscapeLocation = false;
	InstanceData.bSegmentEndChecked = false;
	if (bDebug)
		Boar->LogEscapeTurn(FString::Printf(TEXT("EscapeTaskEnter (Changed) dest=%s angle=%.1f threatDist=%.0f"),
			*NewLocation.ToCompactString(), Boar->GetEscapeYawDelta(NewDirection), Boar->GetEscapeThreatDistance()));
	Boar->SetEscapeThreat(InstanceData.TargetPlayer);
	Boar->SetEscaping(true);
	// 新しい逃走：逃走開始の旋回・加速を始める（EscapeTurn無効の種別では何もしない）。
	Boar->BeginEscapeMotion(NewDirection);
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

	// 切り返しの新しい逃走先は、止まって踏ん張ってから（または切り返しが中断されたら）経路へ反映する。
	// 減速中に新しい経路へ曲がり始めないようにするため。
	if (InstanceData.bHasPendingEscapeLocation && (Boar->IsEscapeMovementHeld() || !Boar->IsEscapePivoting()))
	{
		InstanceData.EscapeLocation = InstanceData.PendingEscapeLocation;
		InstanceData.bHasPendingEscapeLocation = false;
		InstanceData.bSegmentEndChecked = false;
		if (ABoarBase::IsEscapeTurnDebugEnabled())
			Boar->LogEscapeTurn(FString::Printf(TEXT("PendingDestinationApplied dest=%s"), *InstanceData.EscapeLocation.ToCompactString()));
	}

	const bool bDebug = ABoarBase::IsEscapeTurnDebugEnabled();

	// Normal（EscapeTurn有効）：決めた方向をN秒維持し、N秒ごとに再評価する（AKI-172）。
	// 毎フレームの処理は時刻・距離・速度の比較だけで、NavMesh検索は判断が必要なときだけ行う。
	if (Boar->IsEscapeTurnEnabled())
	{
		// 逃げ切った（認識が逃走相手を手放した）：逃走Stateを完了し、徘徊へ戻す（AKI-172）。
		// 逃げ切るまではUpdateEscapeThreatHoldが相手を認識し続けるため、見失った瞬間には終わらない。
		if (InstanceData.TargetPlayer == nullptr)
		{
			if (bDebug)
				Boar->LogEscapeTurn(TEXT("EscapeTaskComplete 逃げ切り → 逃走Stateを完了"));
			return EStateTreeRunStatus::Succeeded;
		}
		Boar->SetEscapeThreat(InstanceData.TargetPlayer);
		// 旋回中は目標方向を固定したまま判断しない。
		if (InstanceData.bHasPendingEscapeLocation || Boar->IsEscapePivoting()
			|| Boar->GetEscapeTurnPhase() == EBoarEscapeTurnPhase::StartTurn)
		{
			InstanceData.StuckTime = 0.0f;
			return EStateTreeRunStatus::Running;
		}

		const float Remaining = FVector::Dist2D(Boar->GetActorLocation(), InstanceData.EscapeLocation);
		InstanceData.StuckTime = Boar->GetVelocity().SizeSquared2D() <= FMath::Square(StuckSpeedThreshold)
			? InstanceData.StuckTime + DeltaTime : 0.0f;

		EPlanContext PlanContext;
		if (Boar->IsEscapeCommitExpired())
		{
			PlanContext = EPlanContext::Reevaluate;
		}
		else if (StuckRecoverDelay > 0.0f && InstanceData.StuckTime >= StuckRecoverDelay)
		{
			// 方向維持中でも、進めないなら障害物復帰を優先する（壁に向かって止まり続けない）。
			InstanceData.StuckTime = 0.0f;
			PlanContext = EPlanContext::Stuck;
		}
		else if (!InstanceData.bSegmentEndChecked && Remaining <= Boar->GetEscapeTurnSettings().SegmentEndCheckDistance)
		{
			// 区間の終わりの延長は逃走先ごとに1回だけ。
			InstanceData.bSegmentEndChecked = true;
			PlanContext = EPlanContext::SegmentEnd;
		}
		else
		{
			return EStateTreeRunStatus::Running;
		}

		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Boar->GetWorld());
		if (Nav == nullptr)
			return EStateTreeRunStatus::Running;
		const FVector Previous = InstanceData.EscapeLocation;
		FVector NextLocation;
		const EEscapePlan Plan = PlanEscapeDestination(*Boar, InstanceData.TargetPlayer, *Nav,
			MinEscapeDistance, MinEscapeDistanceRatio, PlanContext, Remaining, NextLocation);
		switch (Plan)
		{
		case EEscapePlan::Pivot:
			// 新しい逃走先は止まって踏ん張るまで保留し、減速中に新しい経路へ曲がり始めないようにする。
			InstanceData.PendingEscapeLocation = NextLocation;
			InstanceData.bHasPendingEscapeLocation = true;
			break;
		case EEscapePlan::ExtendHeading:
		case EEscapePlan::Recommit:
		case EEscapePlan::ObstacleRecovery:
		case EEscapePlan::NewHeading:
			// 到着前に次の区間へ延長する（Move ToのTrack Moving Goalで経路へ反映され、区間の境目で止まらない）。
			InstanceData.EscapeLocation = NextLocation;
			InstanceData.bSegmentEndChecked = false;
			break;
		default:
			break;
		}
		// 逃走先が見つからなくても、毎フレーム再評価し続けないよう方向維持を始め直す。
		if (PlanContext == EPlanContext::Reevaluate && !Boar->IsEscapePivoting() && Boar->IsEscapeCommitExpired())
			Boar->CommitEscapeHeading(Boar->GetEscapeHeading(), TEXT("NoDestination"));
		if (bDebug)
			LogPlanResult(*Boar, Plan, Previous, NextLocation);
		return EStateTreeRunStatus::Running;
	}

	if (ReplanInterval <= 0.0f)
		return EStateTreeRunStatus::Running;

	InstanceData.ReplanTimeRemaining -= DeltaTime;
	if (InstanceData.ReplanTimeRemaining > 0.0f)
		return EStateTreeRunStatus::Running;
	InstanceData.ReplanTimeRemaining = ReplanInterval;

	if (InstanceData.TargetPlayer == nullptr)
	{
		if (bDebug)
			Boar->LogEscapeTurn(TEXT("ReplanSkipped: NoTargetPlayer（プレイヤーを見失っている）"));
		return EStateTreeRunStatus::Running;
	}

	// 旋回中・切り返しの逃走先を保留中は判定しない。
	if (InstanceData.bHasPendingEscapeLocation || Boar->IsEscapePivoting()
		|| Boar->GetEscapeTurnPhase() == EBoarEscapeTurnPhase::StartTurn)
	{
		if (bDebug)
			Boar->LogEscapeTurn(TEXT("ReplanSkipped: TurnInProgress/PendingDestination"));
		return EStateTreeRunStatus::Running;
	}

	// 以下はEscapeTurn無効の種別（Blue等）の従来処理。

	// 逃走先がプレイヤー側へ向いた場合だけ再計算し、通常時は経路を毎回引き直さない。
	const FVector BoarLocation = Boar->GetActorLocation();
	const FVector ToDestination = (InstanceData.EscapeLocation - BoarLocation).GetSafeNormal2D();
	const FVector ToPlayer = (InstanceData.TargetPlayer->GetActorLocation() - BoarLocation).GetSafeNormal2D();
	if (FVector::DotProduct(ToDestination, ToPlayer) <= 0.0f)
	{
		if (bDebug)
			Boar->LogEscapeTurn(TEXT("ReplanSkipped: DestinationAwayFromPlayer（回り込まれていない）"));
		return EStateTreeRunStatus::Running;
	}

	// 更新した逃走先はMove ToのTrack Moving Goalが有効な場合に経路へ反映される。
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Boar->GetWorld());
	FVector NewLocation;
	if (Nav == nullptr || !FindEscapeLocation(*Boar, InstanceData.TargetPlayer->GetActorLocation(), *Nav, MinEscapeDistance, MinEscapeDistanceRatio, NewLocation))
	{
		if (bDebug)
			Boar->LogEscapeTurn(TEXT("ReplanFailed: 新しい逃走先が見つからない"));
		return EStateTreeRunStatus::Running;
	}
	if (bDebug)
	{
		const AAIController* AIController = Cast<AAIController>(Boar->GetController());
		const UPathFollowingComponent* PathFollowing = AIController ? AIController->GetPathFollowingComponent() : nullptr;
		Boar->LogEscapeTurn(FString::Printf(TEXT("Replan: 回り込みを検知 current=%s new=%s angle=%.1f navPathValid=%d"),
			*InstanceData.EscapeLocation.ToCompactString(), *NewLocation.ToCompactString(),
			Boar->GetEscapeYawDelta((NewLocation - BoarLocation).GetSafeNormal2D()),
			PathFollowing && PathFollowing->HasValidPath() ? 1 : 0));
	}

	// 大きな方向転換だけを切り返しにする。小さな方向修正は止まらず走りながら曲がる。
	const FVector NewDirection = (NewLocation - BoarLocation).GetSafeNormal2D();
	if (Boar->NeedsEscapePivot(NewDirection))
	{
		if (Boar->TryStartEscapePivot(NewDirection))
		{
			InstanceData.PendingEscapeLocation = NewLocation;
			InstanceData.bHasPendingEscapeLocation = true;
		}
		// 再発動間隔中は大きく向きを変えず、今の逃走先へ走り続ける。
		return EStateTreeRunStatus::Running;
	}

	InstanceData.EscapeLocation = NewLocation;
	return EStateTreeRunStatus::Running;
}

void FStateTreeBoarEscapeTask::ExitState(FStateTreeExecutionContext& Context,
                                         const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (ABoarBase* Boar = Cast<ABoarBase>(InstanceData.NPC.Get()))
	{
		// 同じEscape Stateがすぐ再選択される（Sustained）ときは逃走を終えない。
		// ここで終えると切り返しが中断され、次のEnterで逃走開始扱いの高速旋回や再発動間隔による素の向き追従になるため（AKI-152）。
		// 別Stateへ移る・StopLogic・捕獲などはChangedなので従来どおり速度・向き追従を復元する。
		if (Transition.ChangeType == EStateTreeStateChangeType::Sustained)
		{
			if (ABoarBase::IsEscapeTurnDebugEnabled())
				Boar->LogEscapeTurn(TEXT("EscapeTaskExit (Sustained): 逃走を継続"));
			return;
		}
		// StateTreeの停止（StopLogic：Stage Clear／GameOver／Retry等。遷移先が完了状態）は逃走を完全に終える。
		// 別Stateへの遷移（プレイヤーを見失ってPatrolへ等）は、速度・回転・Montageを戻したうえで逃走方向と方向維持を控え、
		// すぐ同じ相手の逃走へ戻ったときに再開できるようにする（AKI-172）。捕獲はCaptureWithFeedbackで控えごと破棄する。
		const bool bTreeStopping = Transition.TargetState.IsCompletionState();
		if (ABoarBase::IsEscapeTurnDebugEnabled())
			Boar->LogEscapeTurn(bTreeStopping ? TEXT("EscapeTaskExit (Changed: StateTree停止 → 逃走終了)")
				: TEXT("EscapeTaskExit (Changed: 別Stateへ遷移 → 逃走を控える)"));
		if (bTreeStopping)
			Boar->SetEscaping(false);
		else
			Boar->SuspendEscape();
	}
}
