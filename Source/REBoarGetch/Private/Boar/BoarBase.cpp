// Fill out your copyright notice in the Description page of Project Settings.


#include "Boar/BoarBase.h"
#include "Boar/BoarMovementComponent.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "BrainComponent.h"
#include "Cage/Cage.h"
#include "Component/CaptureComponent.h"
#include "Component/CapturePresentationComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Core/BoarGameMode.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "REBoarGetch.h"

namespace
{
	/** 逃走・切り返しのデバッグログ（AKI-152）。0=無効、1=Output Log、2=Output Log＋画面表示。 */
	int32 GBoarEscapeTurnDebug = 0;
	FAutoConsoleVariableRef CVarBoarEscapeTurnDebug(
		TEXT("Boar.AI.EscapeTurnDebug"),
		GBoarEscapeTurnDebug,
		TEXT("AKI-152: Normal Boar escape/pivot debug log. 0 = off, 1 = Output Log (LogREBoarGetch), 2 = Output Log + on-screen."),
		ECVF_Cheat);

	int32 GBoarAnimDebug = 0;
	FAutoConsoleVariableRef CVarBoarAnimDebug(
		TEXT("Boar.AI.AnimDebug"),
		GBoarAnimDebug,
		TEXT("AKI-172: Boar locomotion animation debug log (every 0.2s). 0 = off, 1 = Output Log (LogREBoarGetch)."),
		ECVF_Cheat);

	/** AnimBP（ABP_NormalBoar）の変数を名前で読む（デバッグ表示用。無ければNaN）。 */
	double ReadAnimNumber(const UObject* Object, const TCHAR* Name)
	{
		const FProperty* Property = Object ? Object->GetClass()->FindPropertyByName(Name) : nullptr;
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			const void* Value = Numeric->ContainerPtrToValuePtr<void>(Object);
			return Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(Value)
				: static_cast<double>(Numeric->GetSignedIntPropertyValue(Value));
		}
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
			return Bool->GetPropertyValue_InContainer(Object) ? 1.0 : 0.0;
		return TNumericLimits<double>::Max();
	}

	const TCHAR* PivotReasonName(EBoarEscapePivotReason Reason)
	{
		switch (Reason)
		{
		case EBoarEscapePivotReason::Reevaluate: return TEXT("ReevaluatePivot");
		case EBoarEscapePivotReason::NaturalObstacle: return TEXT("ObstaclePivot");
		default: return TEXT("None");
		}
	}

	FString EscapePhaseName(EBoarEscapeTurnPhase Phase)
	{
		return StaticEnum<EBoarEscapeTurnPhase>()->GetNameStringByValue(static_cast<int64>(Phase));
	}
}

// UObject生成中はDataAsset由来の種別設定がまだ確定していないため、Tick可否はBeginPlayで決める。
// CharacterMovementは演出用の移動時間倍率を持つUBoarMovementComponentにする（Component名・既存設定はそのまま）。
ABoarBase::ABoarBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UBoarMovementComponent>(CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	CaptureComponent = CreateDefaultSubobject<UCaptureComponent>(TEXT("CaptureComponent"));
}

// Blueprintの既定値を含めてActorが完成した後にDataAsset設定を適用する。
// スタミナ非対応個体は毎フレーム処理が不要なのでActor Tick自体を止める。
void ABoarBase::BeginPlay()
{
	Super::BeginPlay();

	// プレイヤーのSpringArmはCamera ChannelでTraceするため、Boarと重なるとカメラが急に寄ってしまう。
	// Pawn・CharacterMeshの既定ではCameraをBlockするので、Blueprint側の設定に関わらずIgnoreにする。
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		SkeletalMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	InitializeForStage();

	// アニメ診断ログ（Boar.AI.AnimDebug）。無効時は判定だけで何もしない。
	GetWorldTimerManager().SetTimer(AnimDebugTimerHandle, this, &ABoarBase::LogAnimDebug, 0.2f, true);
}

void ABoarBase::LogAnimDebug() const
{
	if (GBoarAnimDebug == 0)
		return;
	const USkeletalMeshComponent* SkeletalMesh = GetMesh();
	const UAnimInstance* AnimInstance = SkeletalMesh ? SkeletalMesh->GetAnimInstance() : nullptr;
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	const UWorld* World = GetWorld();
	FString MontageText = TEXT("none");
	if (AnimInstance != nullptr)
	{
		if (const UAnimMontage* Montage = AnimInstance->GetCurrentActiveMontage())
		{
			MontageText = FString::Printf(TEXT("%s pos=%.2f/%.2f rate=%.2f weight=%.2f"), *Montage->GetName(),
				AnimInstance->Montage_GetPosition(Montage), Montage->GetPlayLength(), AnimInstance->Montage_GetPlayRate(Montage),
				AnimInstance->GetSlotMontageGlobalWeight(FName(TEXT("DefaultSlot"))));
		}
	}
	UE_LOG(LogREBoarGetch, Log, TEXT("[AnimDebug] %s t=%.2f speed=%.0f maxWalk=%.0f escaping=%d phase=%s blendX=%.2f playRate=%.2f mode=%.0f charging=%.0f tired=%.0f | montage=%s"),
		*GetName(), World ? World->GetTimeSeconds() : 0.0, GetVelocity().Size2D(), Move ? Move->MaxWalkSpeed : 0.0f,
		bIsEscaping ? 1 : 0, *EscapePhaseName(EscapeTurnPhase),
		ReadAnimNumber(AnimInstance, TEXT("LocomotionBlendX")), ReadAnimNumber(AnimInstance, TEXT("LocomotionPlayRate")),
		ReadAnimNumber(AnimInstance, TEXT("LocomotionMode")), ReadAnimNumber(AnimInstance, TEXT("bIsCharging")),
		ReadAnimNumber(AnimInstance, TEXT("bIsTired")), *MontageText);
}

void ABoarBase::InitializeForStage()
{
	if (bSettingsInitialized)
		return;
	ApplyArchetypeDefaults();
	CurrentStamina = ActiveSettings.MaxStamina;
	SetActorTickEnabled(ActiveSettings.bUseStamina);
	bSettingsInitialized = true;
}

// Tickが有効なのは原則としてスタミナ対応かつ未捕獲の個体だけ。
void ABoarBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
		if (!Mode->CanAdvanceStage())
			return;
	UpdateStamina(DeltaSeconds);
	TickEscapeTurn(DeltaSeconds);
}

void ABoarBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Destroy・Level遷移・PIE終了で向き追従や速度を変えたまま残さない。
	ResetEscapeMotion();
	Super::EndPlay(EndPlayReason);
}

// Componentで二重捕獲を拒否してから、GameModeへ檻収容・カウント・ドロップ処理を委譲する。
void ABoarBase::Capture()
{
	CaptureWithFeedback(true);
}

bool ABoarBase::CaptureWithFeedback(bool bSharedFeedback)
{
	if (const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
		if (!Mode->CanAdvanceStage()) 
			return false;
	if (!CaptureComponent || !CaptureComponent->Capture(nullptr)) 
		return false;
	// デバッグ：どの段階で捕まったか（切り返しの好機で捕まえられたか、通常走行中か）を残す。
	if (IsEscapeTurnDebugEnabled())
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		LogEscapeTurn(FString::Printf(TEXT("CapturedDuring phase=%s lastPivot=%s sinceLastPivotStart=%.2fs speed=%.0f threatDist=%.0f"),
			*EscapePhaseName(EscapeTurnPhase), PivotReasonName(LastEscapePivotReason), Now - LastEscapePivotTime,
			GetVelocity().Size2D(), GetEscapeThreatDistance()));
	}
	// 切り返し中に捕獲されても、向き追従・速度倍率を捕獲前の状態へ戻してから止める。
	ResetEscapeMotion();
	SetActorTickEnabled(false);
	// Keep the successful capture at its original position until presentation completes.
	// 演出がある場合はGameModeへ確定待ちとして登録し、演出完了時に確定する。無い場合は即時確定。
	if (auto* PC = GetWorld()->GetFirstPlayerController())
		if (auto* Presentation = PC->FindComponentByClass<UCapturePresentationComponent>())
			if (Presentation->PresentCapturedBoar(this, bSharedFeedback))
			{
				if (auto* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
					Mode->RegisterPendingCapture(this, bSharedFeedback);
				return true;
			}
	if (auto* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
		Mode->HandleBoarCaptured(this, bSharedFeedback);
	return true;
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
		RememberedThreatPlayer.Reset();
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
			continue;
		

		const float Distance = FVector::Dist(GetActorLocation(), PlayerPawn->GetActorLocation());
		if (Distance < PerceivedPlayerDistance && CanDetectTarget(PlayerPawn, Distance))
		{
			PerceivedPlayer = PlayerPawn;
			PerceivedPlayerDistance = Distance;
		}
	}

	// 逃走中は背を向けて視野角から外れるため、記憶範囲内のプレイヤーは見えていなくても認識を続ける。
	if (PerceivedPlayer != nullptr)
	{
		RememberedThreatPlayer = PerceivedPlayer;
	}
	else if (APawn* Remembered = RememberedThreatPlayer.Get())
	{
		const float Distance = FVector::Dist(GetActorLocation(), Remembered->GetActorLocation());
		if (ActiveSettings.ThreatMemoryRange > 0.0f && Distance <= ActiveSettings.ThreatMemoryRange)
		{
			PerceivedPlayer = Remembered;
			PerceivedPlayerDistance = Distance;
		}
		else
		{
			RememberedThreatPlayer.Reset();
		}
	}

	// 逃走中（EscapeTurn有効の種別）は、逃げ切るまで逃げている相手を認識し続ける（AKI-172）。
	// 見失ってすぐ徘徊へ戻り、また見つけて逃げ始める…の繰り返しを防ぐ。
	UpdateEscapeThreatHold();

	// 視界条件を満たす檻のうち、一番近い対象を取得する。
	for (TActorIterator<ACage> It(World); It; ++It)
	{
		ACage* Cage = *It;
		// 破壊済みの檻は認識対象から除外する
		if (Cage == nullptr || Cage->GetIsCageDestroyed())
			continue;
		

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

// 視線Traceの終点。Actor原点は檻のように床面(z=0)にあり床メッシュへ刺さるため、当たり判定の中心を狙う。
// 当たり判定を持つComponentが無い場合だけActor原点を使う。
static FVector GetSightTargetLocation(const AActor* TargetActor)
{
	FVector Origin, Extent;
	TargetActor->GetActorBounds(true, Origin, Extent);
	return Extent.IsNearlyZero() ? TargetActor->GetActorLocation() : Origin;
}

// ターゲットを認識できるか判定する。
bool ABoarBase::CanDetectTarget(const AActor* TargetActor, float Distance) const
{
	if (TargetActor == nullptr || Distance > ActiveSettings.SightRange)
		return false;

	// 水平方向のAIなので、高低差を除いた方向ベクトルで視野角を判定する。
	const FVector ToTarget = (TargetActor->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	// 設定値は視野全体の角度なので、正面ベクトルとの比較には半角を使う。
	const float HalfSightAngle = FMath::Clamp(ActiveSettings.SightAngleDegrees * 0.5f, 0.0f, 180.0f);
	const float SightDotThreshold = FMath::Cos(FMath::DegreesToRadians(HalfSightAngle));
	const bool bInsideSightAngle = FVector::DotProduct(Forward, ToTarget) >= SightDotThreshold;
	const bool bInsideAbsoluteRange = Distance <= ActiveSettings.AbsoluteDetectionRange;

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
		GetSightTargetLocation(TargetActor),
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
	CurrentStamina = ActiveSettings.MaxStamina;
	bIsRecoveringStamina = false;
	ResetEscapeMotion();
}

// RefreshPerceptionTargetsで保存した認識結果を使い、種類ごとの仕様に従って優先対象を決める。
void ABoarBase::GetTargetPreference(bool& bOutPreferPlayer, bool& bOutPreferCage, bool& bOutPreferEscape) const
{
	const bool bHasPlayer = PerceivedPlayer != nullptr;
	const bool bHasCage = PerceivedCage != nullptr && ActiveSettings.bCanAttackCage;
	bOutPreferPlayer = false;
	bOutPreferCage = false;
	bOutPreferEscape = false;

	switch (BoarArchetype)
	{
	case EBoarArchetype::Normal:
		bOutPreferEscape = bHasPlayer;
		bOutPreferCage = !bHasPlayer && bHasCage;
		break;
	case EBoarArchetype::PlayerAttacker:
		bOutPreferPlayer = bHasPlayer;
		bOutPreferCage = !bHasPlayer && bHasCage;
		break;
	case EBoarArchetype::EscapeSpecialist:
		bOutPreferEscape = bHasPlayer;
		break;
	case EBoarArchetype::CageBreaker:
		bOutPreferCage = bHasCage;
		bOutPreferPlayer = !bHasCage && bHasPlayer;
		break;
	default:
		break;
	}
}

// 逃走優先かどうかを返す。スタミナ回復中は強制的に逃走側へ寄せる。
// スタミナ回復中は優先度に関係なく逃走を選ぶ。
// それ以外は設定された優先度で判定する。
bool ABoarBase::ShouldPreferEscape() const
{
	if (ActiveSettings.bUseStamina && bIsRecoveringStamina)
		return true;

	return ActiveSettings.EscapePriorityWeight > FMath::Max(ActiveSettings.PlayerPriorityWeight,
	                                                        ActiveSettings.CagePriorityWeight);
}

float ABoarBase::GetAttackTelegraphDuration() const
{
	if (ActiveSettings.AttackTelegraphAnimation)
	{
		return ActiveSettings.AttackTelegraphAnimation->GetPlayLength() / FMath::Max(
			ActiveSettings.AttackTelegraphPlayRate, 0.01f);
	}

	return FMath::Max(ActiveSettings.AttackTelegraphDuration, 0.0f);
}

bool ABoarBase::IsPlayerAttackReady() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->GetTimeSeconds() >= NextPlayerAttackTime;
}

bool ABoarBase::CanStartPlayerAttack() const
{
	return !IsCaptured() && IsValid(PerceivedPlayer)
		&& PerceivedPlayerDistance <= ActiveSettings.PlayerAttackRange && IsPlayerAttackReady();
}

void ABoarBase::StartPlayerAttackCooldown()
{
	if (const UWorld* World = GetWorld())
	{
		NextPlayerAttackTime = World->GetTimeSeconds() + ActiveSettings.PlayerAttackCooldown;
	}
}

void ABoarBase::BeginChargeHitTracking()
{
	bTrackingChargeHit = true;
	bChargeBlockingHit = false;
}

void ABoarBase::EndChargeHitTracking()
{
	bTrackingChargeHit = false;
	bChargeBlockingHit = false;
}

// 突進中の接触だけを記録する。プレイヤーへのダメージ・スタンは
// BoarPlayerCharacterが同じBlocking Hitから既存処理で適用するため、ここでは与えない。
void ABoarBase::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
                          bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse,
                          const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);

	if (!bTrackingChargeHit)
		return;

	// 坂や段差など歩行可能な面との接触は、突進を止める障害物として扱わない。
	if (const UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		if (Move->IsWalkable(Hit))
			return;
	}

	bChargeBlockingHit = true;
}

/** 檻が破壊されたときに、捕獲中のイノシシを解放して周囲へ移動させる。 */
void ABoarBase::ReleaseBoar()
{
	if (const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
		if (!Mode->CanAdvanceStage()) 
			return;
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

	// 回復停止中に捕獲された個体が速度0のまま解放移動できなくならないよう、スタミナを満タンへ戻す。
	CurrentStamina = ActiveSettings.MaxStamina;
	bIsRecoveringStamina = false;
	bIsEscaping = false;
	ResetEscapeMotion();
	ApplyMovementSpeed();

	// 捕獲前と同じく、スタミナ対応種別だけ毎フレーム更新へ戻す。
	SetActorTickEnabled(ActiveSettings.bUseStamina);

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
	if (!ActiveSettings.bUseStamina || ActiveSettings.MaxStamina <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	return FMath::Clamp(CurrentStamina / ActiveSettings.MaxStamina, 0.0f, 1.0f);
}

// DataAssetから種別に応じた基本パラメータを一括設定する。
void ABoarBase::ApplyArchetypeDefaults()
{
	const FBoarArchetypeSettings& Settings = BoarDataAsset
		                                         ? BoarDataAsset->GetSettings(BoarArchetype)
		                                         : UBoarDataAsset::GetDefaultSettings(BoarArchetype);

	ActiveSettings = Settings;

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->RotationRate.Yaw = Settings.TurnRateDegreesPerSecond;
		Move->MaxWalkSpeed = ActiveSettings.BaseWalkSpeed;
	}

	if (!ActiveSettings.bUseStamina)
	{
		bIsRecoveringStamina = false;
	}

	// ランタイムで種別を切り替えた場合もTick状態を新しい設定へ同期する。
	UpdateTickEnabled();
}

// スタミナ対応種別の移動状態を更新する。消費中に0へ到達すると回復モードへ入り、
// 設定比率まで回復するまでは停止（または減速）する。これにより0付近で状態が毎Frame往復するのを防ぐ。
void ABoarBase::UpdateStamina(float DeltaSeconds)
{
	if (!ActiveSettings.bUseStamina || IsCaptured())
		return;

	if (ActiveSettings.MaxStamina <= KINDA_SMALL_NUMBER)
	{
		CurrentStamina = 0.0f;
		bIsRecoveringStamina = true;
		return;
	}

	const FVector Velocity = GetVelocity();
	const bool bIsMoving = Velocity.SizeSquared2D() > FMath::Square(ActiveSettings.MovingSpeedThreshold);
	// 逃走限定の種別では、徘徊中の歩行を消費対象から外して回復側に回す。
	const bool bIsDrainTarget = !ActiveSettings.bDrainStaminaOnlyWhileEscaping || bIsEscaping;
	const bool bWasRecoveringStamina = bIsRecoveringStamina;

	if (bIsMoving && bIsDrainTarget && !bIsRecoveringStamina)
	{
		CurrentStamina -= ActiveSettings.StaminaDrainPerSecond * DeltaSeconds;
	}
	else
	{
		CurrentStamina += ActiveSettings.StaminaRecoveryPerSecond * DeltaSeconds;
	}

	CurrentStamina = FMath::Clamp(CurrentStamina, 0.0f, ActiveSettings.MaxStamina);
	if (CurrentStamina <= KINDA_SMALL_NUMBER)
	{
		bIsRecoveringStamina = true;
	}
	else if (CurrentStamina >= ActiveSettings.MaxStamina * ActiveSettings.StaminaRecoverExitRatio)
	{
		bIsRecoveringStamina = false;
	}

	// 0到達の1回だけ移動要求を止める。以後はStateTreeの回復Stateが再移動を抑止する。
	if (!bWasRecoveringStamina && bIsRecoveringStamina && ActiveSettings.bStopWhileRecoveringStamina)
	{
		StopForStaminaRecovery();
	}

	ApplyMovementSpeed();
}

void ABoarBase::SetEscaping(bool bNewEscaping)
{
	if (bIsEscaping == bNewEscaping)
		return;

	bIsEscaping = bNewEscaping;
	if (bIsEscaping)
	{
		// 逃走中は向き追従の旋回速度を逃走用（直進的）にする。EscapeTurn無効の種別は変わらない。
		ApplyEscapeRotationRate();
	}
	else
	{
		// 逃走Stateを抜けた時刻を残し、到着直後の再入を「逃走開始」ではなく「継続」と判定できるようにする。
		if (const UWorld* World = GetWorld())
			LastEscapeExitTime = World->GetTimeSeconds();
		if (IsEscapeTurnDebugEnabled())
			LogEscapeTurn(FString::Printf(TEXT("EscapeEnded (phase=%s を中断して復元)"), *EscapePhaseName(EscapeTurnPhase)));
		ResetEscapeMotion();
	}
	// スタミナ非対応かつ逃走倍率1.0の種別は、従来どおりMaxWalkSpeedに触れない。
	if (ActiveSettings.bUseStamina || !FMath::IsNearlyEqual(ActiveSettings.EscapeSpeedMultiplier, 1.0f))
	{
		ApplyMovementSpeed();
	}
}

// 回復中の停止・減速、逃走中の加速、通常速度の優先順で決める。
// 毎Tick呼ばれても結果が状態だけで決まるため、回復終了後は自動で通常速度へ戻る。
void ABoarBase::ApplyMovementSpeed()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (Move == nullptr)
		return;

	if (ActiveSettings.bUseStamina && bIsRecoveringStamina)
	{
		Move->MaxWalkSpeed = ActiveSettings.bStopWhileRecoveringStamina
			                     ? 0.0f
			                     : ActiveSettings.BaseWalkSpeed * ActiveSettings.StaminaRecoverySpeedMultiplier;
		return;
	}

	// 逃走中は切り返しの減速・停止・加速の倍率を合成する（旋回していないときは1）。
	Move->MaxWalkSpeed = bIsEscaping
		                     ? ActiveSettings.BaseWalkSpeed * ActiveSettings.EscapeSpeedMultiplier * GetEscapeTurnSpeedScale()
		                     : ActiveSettings.BaseWalkSpeed;
}

void ABoarBase::StopForStaminaRecovery()
{
	if (AAIController* AIController = Cast<AAIController>(GetController()))
	{
		AIController->StopMovement();
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
	}
}

// ---- 逃走開始の旋回と逃走中の大きな切り返し（AKI-152） ----
// 判断（いつ切り返すか・旋回先）はEscape Taskが渡す方向で一度だけ決め、ここでは段階の進行と移動・向きの制御だけを持つ。
// 停止中はMaxWalkSpeedを0にしてMove Toの経路追従は続けたままにするため、障害物回避・到達不能時の再計画は既存の流れを使う。

namespace
{
	float GetYawDelta(const ABoarBase& Boar, const FVector& Direction)
	{
		return FRotator::NormalizeAxis(Direction.Rotation().Yaw - Boar.GetActorRotation().Yaw);
	}
}

void ABoarBase::BeginEscapeMotion(const FVector& EscapeDirection)
{
	const FBoarEscapeTurnSettings& Settings = ActiveSettings.EscapeTurn;
	if (!Settings.bEnabled || GetWorld() == nullptr || EscapeDirection.IsNearlyZero())
		return;

	// 新しい逃走：前回の逃走方向・方向維持・控えた逃走は引き継がない。
	// （同じ逃走への再入はEscape TaskがContinueEscapeMotion（Sustained）／TryResumeEscape（Changed）で扱う。）
	ResetEscapeMotion();
	SetEscapeHeading(EscapeDirection);
	// 逃走の継続時間（最短時間・逃げ切り判定）はここから数える。同じ逃走への再入では数え直さない。
	EscapeSessionStartTime = GetWorld()->GetTimeSeconds();
	EscapeThreatLostTime = -1.0;
	const float Delta = GetYawDelta(*this, EscapeDirection);
	if (FMath::Abs(Delta) >= Settings.StartTurnMinAngle)
	{
		EscapeTurnTargetYaw = EscapeDirection.Rotation().Yaw;
		EscapeTurnSign = Delta >= 0.0f ? 1.0f : -1.0f;
		++EscapeStartTurnCount;
		if (IsEscapeTurnDebugEnabled())
			LogEscapeTurn(FString::Printf(TEXT("EscapeStarted (新しい逃走) StartTurn #%d angle=%.1f rate=%.0f threatDist=%.0f"),
				EscapeStartTurnCount, Delta, Settings.StartTurnRateDegreesPerSecond, GetEscapeThreatDistance()));
		HoldOrientRotation();
		SetEscapeTurnPhase(EBoarEscapeTurnPhase::StartTurn);
	}
	else
	{
		if (IsEscapeTurnDebugEnabled())
			LogEscapeTurn(FString::Printf(TEXT("EscapeStarted (新しい逃走) 旋回なし angle=%.1f < StartTurnMinAngle threatDist=%.0f"),
				Delta, GetEscapeThreatDistance()));
		EscapeAccelerationStartRatio = Settings.StartInitialSpeedRatio;
		EscapeAccelerationDuration = Settings.StartAccelerationDuration;
		StartEscapeCommit(true);
		SetEscapeTurnPhase(EscapeAccelerationDuration > 0.0f ? EBoarEscapeTurnPhase::Accelerate : EBoarEscapeTurnPhase::None);
	}
}

void ABoarBase::UpdateEscapeThreatHold()
{
	const UWorld* World = GetWorld();
	APawn* Threat = Cast<APawn>(const_cast<AActor*>(EscapeThreat.Get()));
	if (!ActiveSettings.EscapeTurn.bEnabled || !bIsEscaping || World == nullptr || Threat == nullptr || EscapeSessionStartTime < 0.0)
	{
		EscapeThreatLostTime = -1.0;
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (PerceivedPlayer == Threat)
	{
		// 通常の認識（視界・記憶範囲）で捉えている：見失い時間を戻す。
		if (EscapeThreatLostTime >= 0.0 && IsEscapeTurnDebugEnabled())
			LogEscapeTurn(FString::Printf(TEXT("EscapeThreatReacquired 見失い%.2fs後に再認識 → 逃走継続"), Now - EscapeThreatLostTime));
		EscapeThreatLostTime = -1.0;
		return;
	}
	if (PerceivedPlayer != nullptr)
		return;

	if (EscapeThreatLostTime < 0.0)
	{
		EscapeThreatLostTime = Now;
		if (IsEscapeTurnDebugEnabled())
			LogEscapeTurn(FString::Printf(TEXT("EscapeThreatLost 逃走相手を見失った（%.0fcm）→ 逃げ切り判定開始 lose=%.1fs min=%.1fs elapsed=%.2fs"),
				FVector::Dist(GetActorLocation(), Threat->GetActorLocation()), ActiveSettings.EscapeTurn.EscapeLoseDuration,
				ActiveSettings.EscapeTurn.EscapeMinDuration, Now - EscapeSessionStartTime));
	}

	const bool bWithinMinDuration = Now - EscapeSessionStartTime < ActiveSettings.EscapeTurn.EscapeMinDuration;
	const bool bWithinLoseDuration = Now - EscapeThreatLostTime < ActiveSettings.EscapeTurn.EscapeLoseDuration;
	if (bWithinMinDuration || bWithinLoseDuration)
	{
		// まだ逃げ切っていない：見えていなくても同じ相手から逃げ続ける。
		PerceivedPlayer = Threat;
		PerceivedPlayerDistance = FVector::Dist(GetActorLocation(), Threat->GetActorLocation());
		return;
	}

	if (IsEscapeTurnDebugEnabled())
		LogEscapeTurn(FString::Printf(TEXT("EscapeSucceeded 逃げ切り（見失って%.2fs・逃走%.2fs）→ 徘徊へ"),
			Now - EscapeThreatLostTime, Now - EscapeSessionStartTime));
	EscapeSessionStartTime = -1.0;
	EscapeThreatLostTime = -1.0;
}

void ABoarBase::SuspendEscape()
{
	if (!bIsEscaping)
		return;
	// 速度・回転・Montageを戻す前に、再開に必要な分だけ控える。
	const bool bCanResume = ActiveSettings.EscapeTurn.bEnabled && bHasEscapeHeading;
	const FVector Heading = EscapeHeading;
	const float CommitElapsed = GetEscapeCommitElapsed();
	const EBoarEscapeTurnPhase Phase = EscapeTurnPhase;
	// 通常の逃走終了と同じ復元（ResetEscapeMotion）を行う。旋回の途中でもMontage・速度倍率・向き追従は残さない。
	SetEscaping(false);
	if (!bCanResume)
		return;
	bEscapeSuspended = true;
	SuspendedEscapeHeading = Heading;
	SuspendedEscapeCommitElapsed = CommitElapsed;
	SuspendedEscapeTurnPhase = Phase;
	if (IsEscapeTurnDebugEnabled())
		LogEscapeTurn(FString::Printf(TEXT("EscapeSuspended (別Stateへ遷移) heading=%.0f committed=%.2fs phase=%s → %.2fs以内に同じ相手で戻れば再開"),
			Heading.Rotation().Yaw, CommitElapsed, *EscapePhaseName(Phase), ActiveSettings.EscapeTurn.ContinueEscapeGraceDuration));
}

bool ABoarBase::TryResumeEscape(const AActor* Threat)
{
	const FBoarEscapeTurnSettings& Settings = ActiveSettings.EscapeTurn;
	const UWorld* World = GetWorld();
	const bool bWasSuspended = bEscapeSuspended;
	bEscapeSuspended = false;
	const AActor* PreviousThreat = EscapeThreat.Get();
	SetEscapeThreat(Threat);
	if (!bWasSuspended || World == nullptr)
		return false;

	// 「同じ逃走か」は時間だけでなく、抜けた理由（SuspendEscapeは別Stateへの遷移でだけ呼ぶ）と相手でも判断する。
	const double Now = World->GetTimeSeconds();
	const double Gap = Now - LastEscapeExitTime;
	const TCHAR* Reject = nullptr;
	if (!Settings.bEnabled)
		Reject = TEXT("Disabled");
	else if (IsCaptured())
		Reject = TEXT("Captured");
	else if (Threat == nullptr || PreviousThreat != Threat)
		Reject = TEXT("DifferentThreat");
	else if (Gap > Settings.ContinueEscapeGraceDuration)
		Reject = TEXT("GapTooLong");
	else if (EscapeSessionStartTime < 0.0)
		Reject = TEXT("AlreadyEscaped");	// 逃げ切って終えた逃走は再開しない（次は新しい逃走）
	if (Reject != nullptr)
	{
		if (IsEscapeTurnDebugEnabled())
			LogEscapeTurn(FString::Printf(TEXT("EscapeResumeRejected [%s] gap=%.2fs → 新しい逃走として開始"), Reject, Gap));
		return false;
	}

	SetEscapeHeading(SuspendedEscapeHeading);
	const float Delta = FRotator::NormalizeAxis(EscapeTurnTargetYaw - GetActorRotation().Yaw);
	const bool bTurnRemaining = FMath::Abs(Delta) > 0.5f;
	switch (SuspendedEscapeTurnPhase)
	{
	case EBoarEscapeTurnPhase::StartTurn:
	case EBoarEscapeTurnPhase::Decelerate:
	case EBoarEscapeTurnPhase::Brace:
	case EBoarEscapeTurnPhase::Hold:
		if (bTurnRemaining)
		{
			// 旋回の途中で抜けた：旋回先（EscapeTurnTargetYaw）は固定したまま、同じ種類の旋回を続ける。
			// 切り返しはMontage・停止を作り直すため踏ん張りからやり直す（減速は済んでいる）。逃走開始の旋回をやり直すことはない。
			EscapeTurnSign = Delta >= 0.0f ? 1.0f : -1.0f;
			HoldOrientRotation();
			const EBoarEscapeTurnPhase ResumePhase = SuspendedEscapeTurnPhase == EBoarEscapeTurnPhase::StartTurn
				? EBoarEscapeTurnPhase::StartTurn : EBoarEscapeTurnPhase::Brace;
			if (UCharacterMovementComponent* Move = GetCharacterMovement())
				Move->StopMovementImmediately();
			SetEscapeTurnPhase(ResumePhase);
			if (IsEscapeTurnDebugEnabled())
				LogEscapeTurn(FString::Printf(TEXT("EscapeResumed gap=%.2fs | EscapeDirectionPreserved heading=%.0f | 旋回の途中から再開 phase=%s remainingYaw=%.1f"),
					Gap, EscapeHeading.Rotation().Yaw, *EscapePhaseName(ResumePhase), Delta));
			return true;
		}
		// 旋回は終わっていた：走り出しと同じく方向維持をここから始める。
		StartEscapeCommit(true);
		break;
	default:
		// 走行中に抜けた：方向維持の経過秒数を引き継ぐ（別State中の時間は数えない）。
		StartEscapeCommit(false);
		EscapeCommitStartTime = Now - FMath::Max(0.0f, SuspendedEscapeCommitElapsed);
		break;
	}
	if (IsEscapeTurnDebugEnabled())
		LogEscapeTurn(FString::Printf(TEXT("EscapeResumed gap=%.2fs | EscapeDirectionPreserved heading=%.0f | DirectionCommitRemaining=%.2fs"),
			Gap, EscapeHeading.Rotation().Yaw, GetEscapeCommitRemaining()));
	return true;
}

void ABoarBase::SetEscapeHeading(const FVector& Direction)
{
	const FVector Flat = Direction.GetSafeNormal2D();
	if (Flat.IsNearlyZero())
		return;
	EscapeHeading = Flat;
	bHasEscapeHeading = true;
}

void ABoarBase::CommitEscapeHeading(const FVector& Direction, const TCHAR* DebugReason)
{
	SetEscapeHeading(Direction);
	StartEscapeCommit(false);
	if (IsEscapeTurnDebugEnabled())
		LogEscapeTurn(FString::Printf(TEXT("DirectionCommit start (%s) heading=%.0f duration=%.2fs threatDist=%.0f maxSpeed=%.0f"),
			DebugReason, EscapeHeading.Rotation().Yaw, ActiveSettings.EscapeTurn.DirectionCommitDuration,
			GetEscapeThreatDistance(), GetEscapeEffectiveMaxSpeed()));
}

float ABoarBase::GetEscapeCommitRemaining() const
{
	const float Elapsed = GetEscapeCommitElapsed();
	return Elapsed < 0.0f ? -1.0f : FMath::Max(0.0f, ActiveSettings.EscapeTurn.DirectionCommitDuration - Elapsed);
}

bool ABoarBase::IsEscapeCommitExpired() const
{
	const float Elapsed = GetEscapeCommitElapsed();
	return Elapsed >= 0.0f && Elapsed >= ActiveSettings.EscapeTurn.DirectionCommitDuration;
}

float ABoarBase::GetEscapeEffectiveMaxSpeed() const
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	return Move ? Move->MaxWalkSpeed : 0.0f;
}

void ABoarBase::ApplyEscapeRotationRate()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (Move == nullptr)
		return;
	const float RunRate = ActiveSettings.EscapeTurn.RunTurnRateDegreesPerSecond;
	Move->RotationRate.Yaw = (bIsEscaping && ActiveSettings.EscapeTurn.bEnabled && RunRate > 0.0f)
		? RunRate : ActiveSettings.TurnRateDegreesPerSecond;
}

float ABoarBase::GetEscapeCommitElapsed() const
{
	const UWorld* World = GetWorld();
	return (World && EscapeCommitStartTime >= 0.0) ? static_cast<float>(World->GetTimeSeconds() - EscapeCommitStartTime) : -1.0f;
}

float ABoarBase::GetEscapeCommitDistance() const
{
	return EscapeCommitStartTime >= 0.0 ? FVector::Dist2D(GetActorLocation(), EscapeCommitStartLocation) : -1.0f;
}

float ABoarBase::GetEscapeReferenceYawDelta(const FVector& Direction) const
{
	if (Direction.IsNearlyZero())
		return 0.0f;
	// 維持方向があればそれを基準にする。Actorの正面は経路の曲がりや向き追従で揺れるため、
	// 小さな方向変更を積み重ねて大きな切り返しを回避できてしまう。
	if (bHasEscapeHeading)
		return FRotator::NormalizeAxis(Direction.Rotation().Yaw - EscapeHeading.Rotation().Yaw);
	return GetYawDelta(*this, Direction);
}

namespace
{
	/** 切り返し理由ごとの角度しきい値です。障害物だけは「止まらず曲がれる小さな回避」と区別するため別の値を使います。 */
	float GetPivotAngleThreshold(const FBoarEscapeTurnSettings& Settings, EBoarEscapePivotReason Reason)
	{
		return Reason == EBoarEscapePivotReason::NaturalObstacle ? Settings.ObstaclePivotAngleThreshold : Settings.PivotAngleThreshold;
	}

}

bool ABoarBase::NeedsEscapePivot(const FVector& EscapeDirection, EBoarEscapePivotReason Reason) const
{
	const FBoarEscapeTurnSettings& Settings = ActiveSettings.EscapeTurn;
	return Settings.bEnabled && !EscapeDirection.IsNearlyZero()
		&& FMath::Abs(GetEscapeReferenceYawDelta(EscapeDirection)) >= GetPivotAngleThreshold(Settings, Reason);
}

float ABoarBase::GetEscapeThreatDistance() const
{
	const AActor* Threat = EscapeThreat.Get();
	return Threat ? FVector::Dist2D(GetActorLocation(), Threat->GetActorLocation()) : -1.0f;
}

bool ABoarBase::IsEscapePivotCoolingDown() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->GetTimeSeconds() - LastEscapePivotEndTime < ActiveSettings.EscapeTurn.MinPivotInterval;
}

const TCHAR* ABoarBase::GetEscapePivotRejectReason(const FVector& EscapeDirection, EBoarEscapePivotReason Reason) const
{
	const FBoarEscapeTurnSettings& Settings = ActiveSettings.EscapeTurn;
	const UWorld* World = GetWorld();
	if (!Settings.bEnabled)
		return TEXT("Disabled");
	if (World == nullptr || EscapeDirection.IsNearlyZero())
		return TEXT("NoDirection");
	if (FMath::Abs(GetEscapeReferenceYawDelta(EscapeDirection)) < GetPivotAngleThreshold(Settings, Reason))
		return TEXT("AngleTooSmall");
	if (EscapeTurnPhase != EBoarEscapeTurnPhase::None && EscapeTurnPhase != EBoarEscapeTurnPhase::Accelerate)
		return TEXT("TurnInProgress");
	// 抑制の基準は「前回の切り返しを終えて走り出した時刻」。開始時刻基準だと切り返し自体（約2秒）で抑制が切れてしまうため。
	// 再評価（Reevaluate）は切り返し後にDirectionCommitDuration走ってから行うので、抑制は障害物・再入だけに掛ける（AKI-172）。
	if (Reason != EBoarEscapePivotReason::Reevaluate && IsEscapePivotCoolingDown())
		return TEXT("CooldownActive");
	return nullptr;
}

float ABoarBase::GetEscapeYawDelta(const FVector& Direction) const
{
	return Direction.IsNearlyZero() ? 0.0f : GetYawDelta(*this, Direction);
}

bool ABoarBase::IsEscapeTurnDebugEnabled()
{
	return GBoarEscapeTurnDebug != 0;
}

void ABoarBase::LogEscapeTurn(const FString& Message) const
{
	const UWorld* World = GetWorld();
	const FString Line = FString::Printf(TEXT("[EscapeTurn] %s t=%.2f phase=%s escaping=%d starts=%d pivots=%d | %s"),
		*GetName(), World ? World->GetTimeSeconds() : 0.0, *EscapePhaseName(EscapeTurnPhase), bIsEscaping ? 1 : 0,
		EscapeStartTurnCount, EscapePivotCount, *Message);
	UE_LOG(LogREBoarGetch, Log, TEXT("%s"), *Line);
	if (GBoarEscapeTurnDebug >= 2 && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Cyan, Line);
	}
}

bool ABoarBase::ContinueEscapeMotion()
{
	if (!ActiveSettings.EscapeTurn.bEnabled)
		return false;

	if (EscapeTurnPhase == EBoarEscapeTurnPhase::StartTurn)
	{
		if (IsEscapeTurnDebugEnabled())
			LogEscapeTurn(TEXT("EscapeContinue (Sustained): 逃走開始の旋回を継続"));
		return true;
	}
	if (IsEscapePivoting())
	{
		// 切り返しの途中で逃走先に到着した。旋回先は固定したまま続け、減速中なら到着で止まったので踏ん張りへ進める。
		if (EscapeTurnPhase == EBoarEscapeTurnPhase::Decelerate)
		{
			SetEscapeTurnPhase(EBoarEscapeTurnPhase::Brace);
			if (UCharacterMovementComponent* Move = GetCharacterMovement())
				Move->StopMovementImmediately();
		}
		if (IsEscapeTurnDebugEnabled())
			LogEscapeTurn(TEXT("EscapeContinue (Sustained): 切り返しを継続（逃走開始旋回はやり直さない）"));
		return true;
	}
	return false;
}

bool ABoarBase::TryStartEscapePivot(const FVector& EscapeDirection, bool bSkipDecelerate, EBoarEscapePivotReason PivotReason)
{
	const FBoarEscapeTurnSettings& Settings = ActiveSettings.EscapeTurn;
	const UWorld* World = GetWorld();
	if (const TCHAR* Reason = GetEscapePivotRejectReason(EscapeDirection, PivotReason))
	{
		if (IsEscapeTurnDebugEnabled())
			LogEscapeTurn(FString::Printf(TEXT("EscapeTurnRejected [%s]: %s angle(vs %s)=%.1f threshold=%.1f sinceLastPivotEnd=%.2fs"),
				PivotReasonName(PivotReason), Reason, bHasEscapeHeading ? TEXT("heading") : TEXT("forward"),
				GetEscapeReferenceYawDelta(EscapeDirection), GetPivotAngleThreshold(Settings, PivotReason),
				World ? World->GetTimeSeconds() - LastEscapePivotEndTime : 0.0));
		return false;
	}
	const double Now = World->GetTimeSeconds();

	// 旋回先と回転方向はここで固定し、旋回中にプレイヤーが動いても引き直さない。
	const float Delta = GetYawDelta(*this, EscapeDirection);
	EscapeTurnTargetYaw = EscapeDirection.Rotation().Yaw;
	EscapeTurnSign = Delta >= 0.0f ? 1.0f : -1.0f;
	LastEscapePivotTime = Now;
	LastEscapePivotReason = PivotReason;
	++EscapePivotCount;
	// 旋回速度は踏ん張り（Brace）の開始時に、その時点の残り角度から決める（SetEscapeTurnPhase）。
	EscapePivotTurnRate = 0.0f;
	// デバッグ：捕獲チャンスが届く距離で起きたかを後で比べられるよう、開始時の距離を残す。
	EscapePivotStartTime = Now;
	EscapePivotHeldStartTime = -1.0;
	EscapePivotStartThreatDistance = GetEscapeThreatDistance();
	EscapePivotMinThreatDistance = EscapePivotStartThreatDistance;
	if (IsEscapeTurnDebugEnabled())
	{
		const AActor* Threat = EscapeThreat.Get();
		LogEscapeTurn(FString::Printf(TEXT("PivotTiming start reason=%s threatDist=%.0f threatSpeed=%.0f boarSpeed=%.0f expectedHold=%.2fs（踏ん張り＋停止の間に旋回）"),
			PivotReasonName(PivotReason), EscapePivotStartThreatDistance, Threat ? Threat->GetVelocity().Size2D() : -1.0f,
			GetVelocity().Size2D(), Settings.PivotStopDuration));
		LogEscapeTurn(FString::Printf(TEXT("EscapeTurnStarted #%d reason=%s angle(vs heading)=%.1f turn(vs forward)=%.1f dir=%s from=%s"),
			EscapePivotCount, PivotReasonName(PivotReason), GetEscapeReferenceYawDelta(EscapeDirection), Delta, EscapeTurnSign > 0.0f ? TEXT("R") : TEXT("L"),
			bSkipDecelerate ? TEXT("Brace(到着後)") : TEXT("Decelerate(走行中)")));
	}
	// 切り返し後に維持する逃走方向は旋回先と同じにする。方向維持（N秒）は旋回を終えて走り出してから始め直す。
	SetEscapeHeading(EscapeDirection);
	EscapeCommitStartTime = -1.0;
	HoldOrientRotation();
	SetEscapeTurnPhase(bSkipDecelerate ? EBoarEscapeTurnPhase::Brace : EBoarEscapeTurnPhase::Decelerate);
	if (bSkipDecelerate)
	{
		if (UCharacterMovementComponent* Move = GetCharacterMovement())
			Move->StopMovementImmediately();
	}
	return true;
}

bool ABoarBase::IsEscapeMovementHeld() const
{
	return EscapeTurnPhase == EBoarEscapeTurnPhase::StartTurn || EscapeTurnPhase == EBoarEscapeTurnPhase::Brace
		|| EscapeTurnPhase == EBoarEscapeTurnPhase::Hold;
}

bool ABoarBase::IsEscapePivoting() const
{
	return EscapeTurnPhase == EBoarEscapeTurnPhase::Decelerate || EscapeTurnPhase == EBoarEscapeTurnPhase::Brace
		|| EscapeTurnPhase == EBoarEscapeTurnPhase::Hold;
}

float ABoarBase::GetEscapeTurnDirection() const
{
	return (IsEscapePivoting() || EscapeTurnPhase == EBoarEscapeTurnPhase::StartTurn) ? EscapeTurnSign : 0.0f;
}

float ABoarBase::GetEscapeTurnRateDegreesPerSecond() const
{
	switch (EscapeTurnPhase)
	{
	case EBoarEscapeTurnPhase::StartTurn: return ActiveSettings.EscapeTurn.StartTurnRateDegreesPerSecond;
	case EBoarEscapeTurnPhase::Brace:
	case EBoarEscapeTurnPhase::Hold: return EscapePivotTurnRate;
	default: return 0.0f;
	}
}

void ABoarBase::ResetEscapeMotion()
{
	const bool bWasActive = EscapeTurnPhase != EBoarEscapeTurnPhase::None;
	RestoreOrientRotation();
	EscapeTurnPhase = EBoarEscapeTurnPhase::None;
	EscapeTurnPhaseElapsed = 0.0f;
	EscapeTurnSign = 0.0f;
	bHasEscapeHeading = false;
	bEscapeSuspended = false;
	EscapeCommitStartTime = -1.0;
	EscapePivotStartTime = -1.0;
	EscapePivotTurnRate = 0.0f;
	// 逃走中の旋回速度を使っていた場合は通常歩行の値へ戻す（逃走中なら逃走用のまま）。
	ApplyEscapeRotationRate();
	// 速度倍率を掛けていた場合だけ戻す。突進など他の処理が設定した速度には触れない。
	if (bWasActive)
	{
		ApplyMovementSpeed();
		OnEscapeTurnPhaseChanged(EscapeTurnPhase, 0.0f);
	}
	UpdateTickEnabled();
}

void ABoarBase::TickEscapeTurn(float DeltaSeconds)
{
	if (EscapeTurnPhase == EBoarEscapeTurnPhase::None)
		return;

	const FBoarEscapeTurnSettings& Settings = ActiveSettings.EscapeTurn;
	EscapeTurnPhaseElapsed += DeltaSeconds;
	// デバッグ：切り返し〜再加速中のプレイヤー最接近距離（アミが届く距離まで来られたか）。
	if (EscapePivotStartTime >= 0.0)
	{
		const float ThreatDistance = GetEscapeThreatDistance();
		if (ThreatDistance >= 0.0f && (EscapePivotMinThreatDistance < 0.0f || ThreatDistance < EscapePivotMinThreatDistance))
			EscapePivotMinThreatDistance = ThreatDistance;
	}
	auto BeginAcceleration = [this](float StartRatio, float Duration)
	{
		RestoreOrientRotation();
		EscapeAccelerationStartRatio = StartRatio;
		EscapeAccelerationDuration = Duration;
		StartEscapeCommit(true);
		SetEscapeTurnPhase(Duration > 0.0f ? EBoarEscapeTurnPhase::Accelerate : EBoarEscapeTurnPhase::None);
	};

	switch (EscapeTurnPhase)
	{
	case EBoarEscapeTurnPhase::StartTurn:
		if (StepEscapeRotation(Settings.StartTurnRateDegreesPerSecond, DeltaSeconds))
		{
			if (IsEscapeTurnDebugEnabled())
				LogEscapeTurn(FString::Printf(TEXT("EscapeStartTurnCompleted %.2fs"), EscapeTurnPhaseElapsed));
			BeginAcceleration(Settings.StartInitialSpeedRatio, Settings.StartAccelerationDuration);
		}
		break;
	case EBoarEscapeTurnPhase::Decelerate:
		if (EscapeTurnPhaseElapsed >= Settings.DecelerateDuration)
		{
			SetEscapeTurnPhase(EBoarEscapeTurnPhase::Brace);
			// 減速の残りの慣性で滑らないよう、踏ん張りの開始で止め切る。
			if (UCharacterMovementComponent* Move = GetCharacterMovement())
				Move->StopMovementImmediately();
		}
		break;
	case EBoarEscapeTurnPhase::Brace:
		// 踏ん張りながら切り返し先へ向きを変える（その場旋回の段階は無い）。
		StepEscapeRotation(EscapePivotTurnRate, DeltaSeconds);
		if (EscapeTurnPhaseElapsed >= FMath::Min(Settings.BraceDuration, Settings.PivotStopDuration))
			SetEscapeTurnPhase(EBoarEscapeTurnPhase::Hold);
		break;
	case EBoarEscapeTurnPhase::Hold:
	{
		const bool bReached = StepEscapeRotation(EscapePivotTurnRate, DeltaSeconds);
		if (EscapeTurnPhaseElapsed >= FMath::Max(Settings.PivotStopDuration - Settings.BraceDuration, 0.0f))
		{
			// 時間内に回り切れなかった分（フレーム誤差）は、走り出す前に合わせ切る。
			const float RemainingYaw = FRotator::NormalizeAxis(EscapeTurnTargetYaw - GetActorRotation().Yaw);
			if (!bReached)
				SetActorRotation(FRotator(0.0f, EscapeTurnTargetYaw, 0.0f));
			if (IsEscapeTurnDebugEnabled())
				LogEscapeTurn(FString::Printf(TEXT("EscapeTurnCompleted (踏ん張り＋停止で旋回) rate=%.0f remainingYaw=%.1f"),
					EscapePivotTurnRate, bReached ? 0.0f : RemainingYaw));
			if (IsEscapeTurnDebugEnabled() && EscapePivotStartTime >= 0.0)
			{
				const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
				LogEscapeTurn(FString::Printf(TEXT("PivotTiming turnEnd reason=%s sinceStart=%.2fs held=%.2fs threatDist start=%.0f now=%.0f min=%.0f"),
					PivotReasonName(LastEscapePivotReason), Now - EscapePivotStartTime,
					EscapePivotHeldStartTime >= 0.0 ? Now - EscapePivotHeldStartTime : 0.0,
					EscapePivotStartThreatDistance, GetEscapeThreatDistance(), EscapePivotMinThreatDistance));
			}
			BeginAcceleration(Settings.PivotInitialSpeedRatio, Settings.PivotAccelerationDuration);
		}
		break;
	}
	case EBoarEscapeTurnPhase::Accelerate:
		if (EscapeTurnPhaseElapsed >= EscapeAccelerationDuration)
		{
			if (IsEscapeTurnDebugEnabled() && EscapePivotStartTime >= 0.0)
			{
				LogEscapeTurn(FString::Printf(TEXT("PivotTiming accelEnd reason=%s total=%.2fs threatDist now=%.0f min=%.0f"),
					PivotReasonName(LastEscapePivotReason), GetWorld() ? GetWorld()->GetTimeSeconds() - EscapePivotStartTime : 0.0,
					GetEscapeThreatDistance(), EscapePivotMinThreatDistance));
			}
			EscapePivotStartTime = -1.0;
			SetEscapeTurnPhase(EBoarEscapeTurnPhase::None);
		}
		break;
	default:
		break;
	}
	ApplyMovementSpeed();
}

void ABoarBase::SetEscapeTurnPhase(EBoarEscapeTurnPhase NewPhase)
{
	const bool bChanged = EscapeTurnPhase != NewPhase;
	if (bChanged && IsEscapeTurnDebugEnabled())
		LogEscapeTurn(FString::Printf(TEXT("Phase %s -> %s (%.2fs)"), *EscapePhaseName(EscapeTurnPhase),
			*EscapePhaseName(NewPhase), EscapeTurnPhaseElapsed));
	EscapeTurnPhase = NewPhase;
	EscapeTurnPhaseElapsed = 0.0f;
	if (NewPhase == EBoarEscapeTurnPhase::Brace && EscapePivotStartTime >= 0.0 && EscapePivotHeldStartTime < 0.0)
	{
		if (const UWorld* World = GetWorld())
			EscapePivotHeldStartTime = World->GetTimeSeconds();
	}
	if (NewPhase == EBoarEscapeTurnPhase::Brace)
	{
		// 踏ん張り＋停止の時間で、残りの角度を回り切る速度にする（再開時も残り角度から決め直す）。
		float RemainingYaw = FRotator::NormalizeAxis(EscapeTurnTargetYaw - GetActorRotation().Yaw) * EscapeTurnSign;
		if (RemainingYaw < 0.0f)
			RemainingYaw += 360.0f;
		const FBoarEscapeTurnSettings& Settings = ActiveSettings.EscapeTurn;
		EscapePivotTurnRate = RemainingYaw / FMath::Max(Settings.PivotStopDuration, 0.05f);
	}
	if (NewPhase == EBoarEscapeTurnPhase::None)
	{
		RestoreOrientRotation();
		EscapeTurnSign = 0.0f;
	}
	ApplyMovementSpeed();
	UpdateTickEnabled();
	if (bChanged)
	{
		OnEscapeTurnPhaseChanged(EscapeTurnPhase, GetEscapeTurnDirection());
	}
}

// 既定の見た目：既存ABPのDefaultSlotへ踏ん張り・旋回のMontageを流す。Montage未設定なら何もしない。
// Gameplay（停止・旋回・捕獲）はこの処理の成否に依存しない。
void ABoarBase::OnEscapeTurnPhaseChanged_Implementation(EBoarEscapeTurnPhase NewPhase, float TurnDirection)
{
	USkeletalMeshComponent* SkeletalMesh = GetMesh();
	UAnimInstance* AnimInstance = SkeletalMesh ? SkeletalMesh->GetAnimInstance() : nullptr;
	if (AnimInstance == nullptr)
		return;

	auto IsOwnMontage = [this](const UAnimMontage* Montage)
	{
		return Montage != nullptr && (Montage == EscapeBraceMontage || Montage == EscapeTurnLeftMontage || Montage == EscapeTurnRightMontage);
	};
	auto PlayTurnLoop = [&]()
	{
		UAnimMontage* TurnMontage = TurnDirection < 0.0f ? EscapeTurnLeftMontage : EscapeTurnRightMontage;
		if (TurnMontage == nullptr)
			return;
		const float PlayRate = FMath::Clamp(GetEscapeTurnRateDegreesPerSecond() / EscapeTurnAnimationRate, 0.1f, EscapeTurnMaxPlayRate);
		if (AnimInstance->Montage_Play(TurnMontage, PlayRate) > 0.0f)
		{
			// 旋回が終わるまで同じSectionを繰り返す。
			const FName Section = TurnMontage->GetSectionName(0);
			AnimInstance->Montage_SetNextSection(Section, Section, TurnMontage);
		}
	};

	switch (NewPhase)
	{
	case EBoarEscapeTurnPhase::Decelerate:
		// 減速〜踏ん張り〜停止を1本で再生し、最終フレームで保持する（Montage側で自動Blend Outを無効にしている）。
		if (EscapeBraceMontage)
			AnimInstance->Montage_Play(EscapeBraceMontage);
		break;
	case EBoarEscapeTurnPhase::Brace:
		// 減速を飛ばして踏ん張りから始めた場合は、踏ん張りの位置から再生する。
		if (EscapeBraceMontage && !AnimInstance->Montage_IsPlaying(EscapeBraceMontage))
			AnimInstance->Montage_Play(EscapeBraceMontage, 1.0f, EMontagePlayReturnType::MontageLength,
				FMath::Min(ActiveSettings.EscapeTurn.DecelerateDuration, EscapeBraceMontage->GetPlayLength()));
		break;
	case EBoarEscapeTurnPhase::Hold:
		break;
	case EBoarEscapeTurnPhase::StartTurn:
		// 発見リアクション等の別Montageが再生中なら上書きしない（旋回自体はGameplay側で進む）。
		if (UAnimMontage* Current = AnimInstance->GetCurrentActiveMontage(); Current != nullptr && !IsOwnMontage(Current))
			break;
		PlayTurnLoop();
		break;
	default:
		// 加速・通常走行へ戻ったら、自分が再生したMontageだけを止めて既存のLocomotionへ戻す。
		for (UAnimMontage* Montage : {EscapeBraceMontage.Get(), EscapeTurnLeftMontage.Get(), EscapeTurnRightMontage.Get()})
		{
			if (Montage != nullptr && AnimInstance->Montage_IsPlaying(Montage))
				AnimInstance->Montage_Stop(EscapeTurnBlendOutTime, Montage);
		}
		break;
	}
}

bool ABoarBase::StepEscapeRotation(float RateDegreesPerSecond, float DeltaSeconds)
{
	FRotator Rotation = GetActorRotation();
	// 固定した回転方向に沿った残り角度。逆回りで近道し直さないよう0〜360で測る。
	float Remaining = FRotator::NormalizeAxis(EscapeTurnTargetYaw - Rotation.Yaw) * EscapeTurnSign;
	if (Remaining < 0.0f)
	{
		Remaining += 360.0f;
	}
	if (Remaining <= 0.5f || Remaining >= 359.5f)
	{
		Rotation.Yaw = EscapeTurnTargetYaw;
		SetActorRotation(Rotation);
		return true;
	}

	const float Step = FMath::Min(RateDegreesPerSecond * DeltaSeconds, Remaining);
	Rotation.Yaw += Step * EscapeTurnSign;
	SetActorRotation(Rotation);
	return Remaining - Step <= 0.5f;
}

void ABoarBase::HoldOrientRotation()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bOrientRotationHeld || Move == nullptr)
		return;
	bSavedOrientRotationToMovement = Move->bOrientRotationToMovement;
	Move->bOrientRotationToMovement = false;
	bOrientRotationHeld = true;
}

void ABoarBase::RestoreOrientRotation()
{
	if (!bOrientRotationHeld)
		return;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = bSavedOrientRotationToMovement;
	}
	bOrientRotationHeld = false;
}

void ABoarBase::StartEscapeCommit(bool bFromTurnEnd)
{
	if (const UWorld* World = GetWorld())
	{
		EscapeCommitStartTime = World->GetTimeSeconds();
		EscapeCommitStartLocation = GetActorLocation();
		// 旋回を終えて走り出した時刻を連続切り返しの抑制の基準にする（逃走開始・切り返しの両方）。
		// 止まらずに方向だけ確定し直した場合（障害物復帰・再評価）は抑制の基準を動かさない。
		if (bFromTurnEnd)
		{
			LastEscapePivotEndTime = EscapeCommitStartTime;
			if (IsEscapeTurnDebugEnabled())
				LogEscapeTurn(FString::Printf(TEXT("DirectionCommit start (TurnEnd) heading=%.0f duration=%.2fs threatDist=%.0f"),
					bHasEscapeHeading ? EscapeHeading.Rotation().Yaw : GetActorRotation().Yaw,
					ActiveSettings.EscapeTurn.DirectionCommitDuration, GetEscapeThreatDistance()));
		}
	}
}

float ABoarBase::GetEscapeTurnSpeedScale() const
{
	const FBoarEscapeTurnSettings& Settings = ActiveSettings.EscapeTurn;
	switch (EscapeTurnPhase)
	{
	case EBoarEscapeTurnPhase::Decelerate:
		return Settings.DecelerateDuration > 0.0f
			? 1.0f - FMath::Clamp(EscapeTurnPhaseElapsed / Settings.DecelerateDuration, 0.0f, 1.0f)
			: 0.0f;
	case EBoarEscapeTurnPhase::StartTurn:
	case EBoarEscapeTurnPhase::Brace:
	case EBoarEscapeTurnPhase::Hold:
		return 0.0f;
	case EBoarEscapeTurnPhase::Accelerate:
		return EscapeAccelerationDuration > 0.0f
			? FMath::Lerp(EscapeAccelerationStartRatio, 1.0f, FMath::Clamp(EscapeTurnPhaseElapsed / EscapeAccelerationDuration, 0.0f, 1.0f))
			: 1.0f;
	default:
		return 1.0f;
	}
}

void ABoarBase::UpdateTickEnabled()
{
	SetActorTickEnabled(!IsCaptured() && (ActiveSettings.bUseStamina || EscapeTurnPhase != EBoarEscapeTurnPhase::None));
}
