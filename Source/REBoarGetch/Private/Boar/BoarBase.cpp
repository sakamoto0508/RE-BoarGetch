// Fill out your copyright notice in the Description page of Project Settings.


#include "Boar/BoarBase.h"
#include "AIController.h"
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

// コンストラクタ。捕獲用コンポーネントを生成し、種別差分用にTickを有効化する。
ABoarBase::ABoarBase()
{
	PrimaryActorTick.bCanEverTick = true;
	CaptureComponent = CreateDefaultSubobject<UCaptureComponent>(TEXT("CaptureComponent"));
}

// ゲーム開始時に種別ごとの初期値を適用し、スタミナを初期化する。
void ABoarBase::BeginPlay()
{
	Super::BeginPlay();
	ApplyArchetypeDefaults();
	CurrentStamina = MaxStamina;
}

// 種別差分のうち、スタミナ制御（青イノシシ）を毎フレーム更新する。
void ABoarBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateStamina(DeltaSeconds);
}

// 捕獲処理。成功時はGameModeへ通知して共通後処理へ流す。
void ABoarBase::Capture()
{
	if (CaptureComponent == nullptr) 
		return;
	if (!CaptureComponent->Capture(nullptr))	
		return;
	
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

// 周囲のプレイヤー/檻を探索し、距離付きの認識データへ反映する。
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

	//playerを取得。
	const int32 NumPlayers = UGameplayStatics::GetNumPlayerControllers(World);
	for (int32 i = 0; i < NumPlayers; ++i)
	{
		APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, i);
		if (PlayerPawn == nullptr)
		{
			continue;
		}

		const float Distance = FVector::Dist(GetActorLocation(), PlayerPawn->GetActorLocation());
		if (Distance < PerceivedPlayerDistance)
		{
			PerceivedPlayer = PlayerPawn;
			PerceivedPlayerDistance = Distance;
		}
	}

	//一番近い檻を取得。
	for (TActorIterator<ACage> It(World); It; ++It)
	{
		ACage* Cage = *It;
		if (Cage == nullptr)
		{
			continue;
		}

		const float Distance = FVector::Dist(GetActorLocation(), Cage->GetActorLocation());
		if (Distance < PerceivedCageDistance)
		{
			PerceivedCage = Cage;
			PerceivedCageDistance = Distance;
		}
	}

	const bool bPlayerInSight = PerceivedPlayer != nullptr && PerceivedPlayerDistance <= SightRange;
	const bool bCageInSight = PerceivedCage != nullptr && PerceivedCageDistance <= SightRange;
	// 視認範囲外ならターゲットから外す
	if (!bPlayerInSight)
	{
		PerceivedPlayer = nullptr;
		PerceivedPlayerDistance = BIG_NUMBER;
	}

	if (!bCageInSight)
	{
		PerceivedCage = nullptr;
		PerceivedCageDistance = BIG_NUMBER;
	}
	return bPlayerInSight || bCageInSight;
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

/** 檻が破壊されたときに、捕獲中のイノシシを解放して周囲へ移動させる。 */
void ABoarBase::ReleaseBoar()
{
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
			BrainComponent->RestartLogic();
		}
	}
}

// スタミナの正規化値を返す。スタミナ非対応種別は常に1.0を返す。
// TODO:これは後で派生クラスでBP側から設定できるようにするか、DataAsset化するか検討する。
float ABoarBase::GetStaminaNormalized() const
{
	if (!bUseStamina || MaxStamina <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	return FMath::Clamp(CurrentStamina / MaxStamina, 0.0f, 1.0f);
}

// 種別に応じた基本パラメータを一括設定する。
// TODO:これは後で派生クラスでBP側から設定できるようにするか、DataAsset化するか検討する。
void ABoarBase::ApplyArchetypeDefaults()
{
	switch (BoarArchetype)
	{
	case EBoarArchetype::Normal:
		PlayerPriorityWeight = 0.8f;
		CagePriorityWeight = 0.9f;
		EscapePriorityWeight = 1.4f;
		bCanAttackCage = true;
		bUseStamina = false;
		break;
	case EBoarArchetype::Red:
		PlayerPriorityWeight = 1.8f;
		CagePriorityWeight = 0.8f;
		EscapePriorityWeight = 0.4f;
		bCanAttackCage = true;
		bUseStamina = false;
		break;
	case EBoarArchetype::Blue:
		PlayerPriorityWeight = 0.4f;
		CagePriorityWeight = 0.1f;
		EscapePriorityWeight = 2.2f;
		bCanAttackCage = false;
		bUseStamina = true;
		MaxStamina = 100.0f;
		StaminaDrainPerSecond = 30.0f;
		StaminaRecoveryPerSecond = 35.0f;
		StaminaRecoverExitRatio = 0.4f;
		break;
	case EBoarArchetype::CageBreaker:
		PlayerPriorityWeight = 0.7f;
		CagePriorityWeight = 2.2f;
		EscapePriorityWeight = 0.5f;
		bCanAttackCage = true;
		bUseStamina = false;
		break;
	case EBoarArchetype::EscapeSpecialist:
		PlayerPriorityWeight = 0.5f;
		CagePriorityWeight = 0.6f;
		EscapePriorityWeight = 2.5f;
		bCanAttackCage = true;
		bUseStamina = false;
		break;
	default:
		break;
	}

	if (!bUseStamina)
	{
		bIsRecoveringStamina = false;
	}
}

// 青イノシシ向けのスタミナ更新。移動中は消費、停止中は回復させる。
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
	const bool bIsMoving = Velocity.SizeSquared2D() > FMath::Square(10.0f);

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
		//TODO: ここは後で派生クラスでBP側から設定できるようにするか、DataAsset化するか検討する。
		const float BaseSpeed = 450.0f;
		Move->MaxWalkSpeed = bIsRecoveringStamina ? BaseSpeed * 0.35f : BaseSpeed;
	}
}
