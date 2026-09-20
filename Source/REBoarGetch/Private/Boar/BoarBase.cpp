// Fill out your copyright notice in the Description page of Project Settings.


#include "Boar/BoarBase.h"
#include "AIController.h"
#include "Animation/AnimSequenceBase.h"
#include "BrainComponent.h"
#include "Cage/Cage.h"
#include "Component/CaptureComponent.h"
#include "Core/BoarGameMode.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

// UObject生成中はDataAsset由来の種別設定がまだ確定していないため、Tick可否はBeginPlayで決める。
ABoarBase::ABoarBase()
{
	PrimaryActorTick.bCanEverTick = true;
	CaptureComponent = CreateDefaultSubobject<UCaptureComponent>(TEXT("CaptureComponent"));
}

// Blueprintの既定値を含めてActorが完成した後にDataAsset設定を適用する。
// スタミナ非対応個体は毎フレーム処理が不要なのでActor Tick自体を止める。
void ABoarBase::BeginPlay()
{
	Super::BeginPlay();
	ApplyArchetypeDefaults();
	CurrentStamina = MaxStamina;
	SetActorTickEnabled(bUseStamina);
}

// Tickが有効なのは原則としてスタミナ対応かつ未捕獲の個体だけ。
void ABoarBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
		if (!Mode->CanAdvanceStage()) return;
	UpdateStamina(DeltaSeconds);
}

// Componentで二重捕獲を拒否してから、GameModeへ檻収容・カウント・ドロップ処理を委譲する。
void ABoarBase::Capture()
{
	if (const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
		if (!Mode->CanAdvanceStage()) return;
	if (CaptureComponent == nullptr)
		return;
	if (!CaptureComponent->Capture(nullptr))
		return;

	// 捕獲中は移動もスタミナ変化もないため、解放されるまでTickを停止する。
	SetActorTickEnabled(false);
	
	// 捕獲成功後のゲーム進行（檻送致・カウント・ドロップ）はGameModeに集約する。
	if (ABoarGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr)
	{
		GameMode->HandleBoarCaptured(this);
	}
}

// 捕獲状態を返す。
bool ABoarBase::IsCaptured() const
{
	return CaptureComponent != nullptr && CaptureComponent->IsCaptured();
}

// 各候補を「安い距離判定 -> 視野角 -> Visibility Trace」の順で絞り込み、
// 条件を満たした最寄り対象だけをStateTree向けキャッシュへ保存する。
bool ABoarBase::RefreshPerceptionTargets()
{
	if (IsCaptured())
	{
		PerceivedPlayer = nullptr;
		PerceivedCage = nullptr;
		PerceivedPlayerDistance = BIG_NUMBER;
		PerceivedCageDistance = BIG_NUMBER;
		return false;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
		return false;
	
	PerceivedPlayer = nullptr;
	PerceivedCage = nullptr;
	PerceivedPlayerDistance = BIG_NUMBER;
	PerceivedCageDistance = BIG_NUMBER;

	// 視界条件を満たすプレイヤーのうち、一番近い対象を取得する。
	const int32 NumPlayers = UGameplayStatics::GetNumPlayerControllers(World);
	for (int32 i = 0; i < NumPlayers; ++i)
	{
		APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, i);
		if (PlayerPawn == nullptr)
		{
			continue;
		}

		const float Distance = FVector::Dist(GetActorLocation(), PlayerPawn->GetActorLocation());
		if (Distance < PerceivedPlayerDistance && CanDetectTarget(PlayerPawn, Distance))
		{
			PerceivedPlayer = PlayerPawn;
			PerceivedPlayerDistance = Distance;
		}
	}

	// 視界条件を満たす檻のうち、一番近い対象を取得する。
	for (TActorIterator<ACage> It(World); It; ++It)
	{
		ACage* Cage = *It;
		// 破壊済みの檻は認識対象から除外する
		if (Cage == nullptr || Cage->GetIsCageDestroyed())
		{
			continue;
		}

		FVector ClosestPointOnCage;
		const float CollisionDistance = Cage->ActorGetDistanceToCollision(
			GetActorLocation(), ECC_Pawn, ClosestPointOnCage);
		
		const float Distance = CollisionDistance >= 0.0f
			? CollisionDistance
			: FVector::Dist(GetActorLocation(), Cage->GetActorLocation());
		if (Distance < PerceivedCageDistance && CanDetectTarget(Cage, Distance))
		{
			PerceivedCage = Cage;
			PerceivedCageDistance = Distance;
		}
	}

	return PerceivedPlayer != nullptr || PerceivedCage != nullptr;
}

// ターゲットを認識できるか判定する。
bool ABoarBase::CanDetectTarget(const AActor* TargetActor, float Distance) const
{
	if (TargetActor == nullptr || Distance > SightRange)
		return false;
	
	// 水平方向のAIなので、高低差を除いた方向ベクトルで視野角を判定する。
	const FVector ToTarget = (TargetActor->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	// 設定値は視野全体の角度なので、正面ベクトルとの比較には半角を使う。
	const float HalfSightAngle = FMath::Clamp(SightAngleDegrees * 0.5f, 0.0f, 180.0f);
	const float SightDotThreshold = FMath::Cos(FMath::DegreesToRadians(HalfSightAngle));
	const bool bInsideSightAngle = FVector::DotProduct(Forward, ToTarget) >= SightDotThreshold;
	const bool bInsideAbsoluteRange = Distance <= AbsoluteDetectionRange;

	// 通常は正面の視野角内だけ認識し、至近距離では角度条件を免除する。
	if (!bInsideSightAngle && !bInsideAbsoluteRange)
		return false;

	UWorld* World = GetWorld();
	if (World == nullptr)
		return false;

	FHitResult HitResult;
	// SCENE_QUERY_STAT名を付け、Insights/Collision Analyzerで視界Traceだけ識別できるようにする。
	// 複雑なMeshポリゴン判定は不要なのでbTraceComplex=falseとし、自分自身は除外する。
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BoarSight), false, this);
	// 最初の遮蔽物だけ分かればよいためSingle Traceを使う。
	const bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		GetPawnViewLocation(),
		TargetActor->GetActorLocation(),
		ECC_Visibility,
		QueryParams);

	// Traceが何にも当たらない場合と、対象自身が最初に当たる場合だけ視線が通っている。
	return !bHit || HitResult.GetActor() == TargetActor;
}

// 種別変更API。BPやデバッグ用途でランタイム切替できるようにする。
void ABoarBase::SetBoarArchetype(EBoarArchetype NewArchetype)
{
	BoarArchetype = NewArchetype;
	ApplyArchetypeDefaults();
	CurrentStamina = MaxStamina;
	bIsRecoveringStamina = false;
}

// 逃走優先かどうかを返す。スタミナ回復中は強制的に逃走側へ寄せる。
// スタミナ回復中は優先度に関係なく逃走を選ぶ。
// それ以外は設定された優先度で判定する。
bool ABoarBase::ShouldPreferEscape() const
{
	if (bUseStamina && bIsRecoveringStamina)
		return true;
	
	return EscapePriorityWeight > FMath::Max(PlayerPriorityWeight, CagePriorityWeight);
}

float ABoarBase::GetAttackTelegraphDuration() const
{
	if (AttackTelegraphAnimation)
	{
		return AttackTelegraphAnimation->GetPlayLength() / FMath::Max(AttackTelegraphPlayRate, 0.01f);
	}

	return FMath::Max(AttackTelegraphDuration, 0.0f);
}

/** 檻が破壊されたときに、捕獲中のイノシシを解放して周囲へ移動させる。 */
void ABoarBase::ReleaseBoar()
{
	if (const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
		if (!Mode->CanAdvanceStage()) return;
	// 捕獲済みの個体だけを解放対象にし、二重解放を防止する。
	if (CaptureComponent == nullptr || !CaptureComponent->IsCaptured())
		return;

	// 捕獲時に停止したAIと、檻から歩いて出るための目的地を準備する。
	AAIController* AIController = Cast<AAIController>(GetController());
	FVector ReleaseLocation = GetActorLocation();
	bool bFoundReleaseLocation = false;

	// 瞬間移動は行わず、現在地から到達可能なNavMesh上の地点を移動先にする。
	if (UWorld* World = GetWorld())
	{
		if (UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(World))
		{
			FNavLocation NavLocation;
			// ランダムな到達可能ポイントを取得
			// 中心位置、探索半径、検索結果
			bFoundReleaseLocation = NavigationSystem->GetRandomReachablePointInRadius(
				GetActorLocation(), ReleaseRadius, NavLocation);
			if (bFoundReleaseLocation)
			{
				ReleaseLocation = NavLocation.Location;
			}
		}
	}

	if (AIController)
	{
		// 通常StateTreeのMove Toと解放移動が競合しないよう、一時的に停止する。
		AIController->StopMovement();
		if (UBrainComponent* BrainComponent = AIController->GetBrainComponent())
		{
			BrainComponent->StopLogic(TEXT("Leaving cage"));
		}
	}

	if (!CaptureComponent->Release())
	{
		FinishReleaseMovement();
		return;
	}
	for (TActorIterator<ACage> It(GetWorld()); It; ++It) It->ForgetBoar(this);

	// 捕獲前と同じく、スタミナ対応種別だけ毎フレーム更新へ戻す。
	SetActorTickEnabled(bUseStamina);

	PerceivedPlayer = nullptr;
	PerceivedCage = nullptr;
	PerceivedPlayerDistance = BIG_NUMBER;
	PerceivedCageDistance = BIG_NUMBER;

	if (AIController == nullptr || !bFoundReleaseLocation)
	{
		FinishReleaseMovement();
		UE_LOG(LogTemp, Warning, TEXT("[Boar] Released %s without release movement."),
			*GetNameSafe(this));
		return;
	}

	bIsLeavingCage = true;
	// 解放移動が終わったタイミングで通常StateTreeへ復帰するため、完了通知を監視する。
	AIController->ReceiveMoveCompleted.AddUniqueDynamic(
		this, &ABoarBase::HandleReleaseMoveCompleted);

	const EPathFollowingRequestResult::Type MoveResult = AIController->MoveToLocation(
		ReleaseLocation, ReleaseAcceptanceRadius);

	if (MoveResult != EPathFollowingRequestResult::RequestSuccessful)
	{
		FinishReleaseMovement();
	}

	UE_LOG(LogTemp, Log, TEXT("[Boar] Released %s. MoveTarget=%s"),
		*GetNameSafe(this), *ReleaseLocation.ToCompactString());
}

void ABoarBase::HandleReleaseMoveCompleted(
	FAIRequestID RequestID, EPathFollowingResult::Type Result)
{
	// 成否にかかわらず解放移動を終了し、通常AIが停止したままになることを防ぐ。
	(void)RequestID;
	(void)Result;
	FinishReleaseMovement();
}

void ABoarBase::FinishReleaseMovement()
{
	bIsLeavingCage = false;

	if (AAIController* AIController = Cast<AAIController>(GetController()))
	{
		// 次回の解放時に通知が重複しないよう、登録したDelegateを解除する。
		AIController->ReceiveMoveCompleted.RemoveDynamic(
			this, &ABoarBase::HandleReleaseMoveCompleted);

		if (UBrainComponent* BrainComponent = AIController->GetBrainComponent())
		{
			// 解放移動後は既存のStateTreeに制御を戻し、通常の徘徊を再開する。
			const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>();
			if (!Mode || Mode->CanAdvanceStage()) BrainComponent->RestartLogic();
		}
	}
}

// スタミナの正規化値を返す。スタミナ非対応種別は常に1.0を返す。
float ABoarBase::GetStaminaNormalized() const
{
	if (!bUseStamina || MaxStamina <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	return FMath::Clamp(CurrentStamina / MaxStamina, 0.0f, 1.0f);
}

// DataAssetから種別に応じた基本パラメータを一括設定する。
void ABoarBase::ApplyArchetypeDefaults()
{
	const FBoarArchetypeSettings& Settings = BoarDataAsset
		? BoarDataAsset->GetSettings(BoarArchetype)
		: UBoarDataAsset::GetDefaultSettings(BoarArchetype);

	PlayerPriorityWeight = Settings.PlayerPriorityWeight;
	CagePriorityWeight = Settings.CagePriorityWeight;
	EscapePriorityWeight = Settings.EscapePriorityWeight;
	bCanAttackCage = Settings.bCanAttackCage;
	CageAttackDamage = Settings.CageAttackDamage;
	AttackTelegraphAnimation = Settings.AttackTelegraphAnimation;
	AttackTelegraphDuration = Settings.AttackTelegraphDuration;
	AttackTelegraphPlayRate = Settings.AttackTelegraphPlayRate;
	ChargeSpeed = Settings.ChargeSpeed;
	ChargeImpactDistance = Settings.ChargeImpactDistance;
	ChargeMaxDuration = Settings.ChargeMaxDuration;
	AttackRetreatDistance = Settings.AttackRetreatDistance;
	AttackRetreatSpeed = Settings.AttackRetreatSpeed;
	SightRange = Settings.SightRange;
	SightAngleDegrees = Settings.SightAngleDegrees;
	AbsoluteDetectionRange = Settings.AbsoluteDetectionRange;
	ChaseRange = Settings.ChaseRange;
	EscapeRange = Settings.EscapeRange;
	bUseStamina = Settings.bUseStamina;
	MaxStamina = Settings.MaxStamina;
	StaminaDrainPerSecond = Settings.StaminaDrainPerSecond;
	StaminaRecoveryPerSecond = Settings.StaminaRecoveryPerSecond;
	StaminaRecoverExitRatio = Settings.StaminaRecoverExitRatio;
	BaseWalkSpeed = Settings.BaseWalkSpeed;
	StaminaRecoverySpeedMultiplier = Settings.StaminaRecoverySpeedMultiplier;
	MovingSpeedThreshold = Settings.MovingSpeedThreshold;

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->RotationRate.Yaw = Settings.TurnRateDegreesPerSecond;
		Move->MaxWalkSpeed = BaseWalkSpeed;
	}

	if (!bUseStamina)
	{
		bIsRecoveringStamina = false;
	}

	// ランタイムで種別を切り替えた場合もTick状態を新しい設定へ同期する。
	SetActorTickEnabled(bUseStamina && !IsCaptured());
}

// スタミナ対応種別の移動状態を更新する。消費中に0へ到達すると回復モードへ入り、
// 設定比率まで回復するまでは速度を落とす。これにより0付近で状態が毎Frame往復するのを防ぐ。
void ABoarBase::UpdateStamina(float DeltaSeconds)
{
	if (!bUseStamina || IsCaptured())
		return;
	
	if (MaxStamina <= KINDA_SMALL_NUMBER)
	{
		CurrentStamina = 0.0f;
		bIsRecoveringStamina = true;
		return;
	}

	const FVector Velocity = GetVelocity();
	const bool bIsMoving = Velocity.SizeSquared2D() > FMath::Square(MovingSpeedThreshold);

	if (bIsMoving && !bIsRecoveringStamina)
	{
		CurrentStamina -= StaminaDrainPerSecond * DeltaSeconds;
	}
	else
	{
		CurrentStamina += StaminaRecoveryPerSecond * DeltaSeconds;
	}

	CurrentStamina = FMath::Clamp(CurrentStamina, 0.0f, MaxStamina);
	if (CurrentStamina <= KINDA_SMALL_NUMBER)
	{
		bIsRecoveringStamina = true;
	}
	else if (CurrentStamina >= MaxStamina * StaminaRecoverExitRatio)
	{
		bIsRecoveringStamina = false;
	}

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = bIsRecoveringStamina
			? BaseWalkSpeed * StaminaRecoverySpeedMultiplier
			: BaseWalkSpeed;
	}
}
