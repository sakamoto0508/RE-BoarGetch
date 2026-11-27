#include "Cage/Cage.h"
#include "REBoarGetch.h"
#include "Audio/BoarAudioManagerSubsystem.h"
#include "Engine/GameInstance.h"
#include "Cage/CageVisualComponent.h"
#include "Boar/BoarBase.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Component/CaptureComponent.h"
#include "Core/BoarGameMode.h"
#include "Engine/World.h"

ACage::ACage()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	Presentation = CreateDefaultSubobject<UCageVisualComponent>(TEXT("CagePresentation"));
	Presentation->SetupAttachment(SceneRoot);

	CapturedBoarArea = CreateDefaultSubobject<UBoxComponent>(TEXT("CapturedBoarArea"));
	CapturedBoarArea->SetupAttachment(SceneRoot);
	// 既定の収容範囲（cm）。中心を床から100cm上げ、幅400×奥行400×高さ200の箱にする。檻ごとの大きさはBP／配置側で調整する。
	CapturedBoarArea->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));
	CapturedBoarArea->SetBoxExtent(FVector(200.0f, 200.0f, 100.0f));
	CapturedBoarArea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CapturedBoarArea->SetGenerateOverlapEvents(false);
}

void ACage::BeginPlay()
{
	Super::BeginPlay();

	// ステージ配置の檻はBeginPlay前にGameModeが初期化済み。単体で置かれた檻も、
	// 起動段階を知らずに同じ初期化（何度呼んでも安全）を通す。
	InitializeForStage();
}

void ACage::InitializeForStage()
{
	if (bStageInitialized)
		return;

	bStageInitialized = true;
	MaxHp = FMath::Max(MaxHp, 1.0f);
	CurrentHp = MaxHp;
	bIsDestroyed = false;
	OnHealthChanged.Broadcast(CurrentHp, MaxHp);
	UE_LOG(LogREBoarGetch, Log, TEXT("[StageInit] 檻の準備完了: %s"), *GetName());
}

void ACage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Level遷移やActor破棄後に再出現処理が発火しないよう、予約を解除する。
	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void ACage::CollectBoar(ABoarBase* Boar)
{
	if (Boar == nullptr)
		return;
	if (!CanAdvance())
		return;
	if (bIsDestroyed)
	{
		Boar->SetActorLocation(GetCapturedBoarLocation(0, Boar), false, nullptr, ETeleportType::TeleportPhysics);
		Boar->ReleaseBoar();
		NotifyHousedCountChanged();
		return;
	}

	if (!Boar->IsCaptured() || CapturedBoars.Contains(Boar))
		return;

	// 破棄済みの参照を除外し、新しい個体が空いた収容枠を利用できるようにする。
	CapturedBoars.RemoveAll([](const TObjectPtr<ABoarBase>& CapturedBoar)
	{
		return !IsValid(CapturedBoar);
	});

	const int32 SlotIndex = CapturedBoars.AddUnique(Boar);
	Boar->OnDestroyed.AddUniqueDynamic(this, &ACage::HandleHousedBoarDestroyed);

	// 捕獲地点から檻までの座標差が物理速度に反映されないよう、TeleportPhysicsで配置する。
	Boar->SetActorLocation(
		GetCapturedBoarLocation(SlotIndex, Boar),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	UE_LOG(LogREBoarGetch, Log, TEXT("[Cage] Collected %s Total=%d"), *GetNameSafe(Boar), CapturedBoars.Num());
	NotifyHousedCountChanged();
}

FVector ACage::GetCapturedBoarLocation(int32 SlotIndex, const ABoarBase* Boar) const
{
	if (CapturedBoarArea == nullptr)
		return GetActorLocation();

	const int32 BoarsPerRow = FMath::Max(1, CapturedBoarsPerRow);
	FVector2D SlotOffset = CapturedBoarCenterOffset;

	// 1頭目は中央へ置き、2頭目以降は中央の前後へ交互に列を増やす。
	if (SlotIndex > 0)
	{
		const int32 GridIndex = SlotIndex - 1;
		const int32 Row = GridIndex / BoarsPerRow + 1;
		const int32 Column = GridIndex % BoarsPerRow;
		const float CenteredColumn = static_cast<float>(Column) - static_cast<float>(BoarsPerRow - 1) * 0.5f;
		const int32 SignedRow = (Row % 2 == 1) ? (Row + 1) / 2 : -(Row / 2);

		SlotOffset.X += CenteredColumn * CapturedBoarSpacing;
		SlotOffset.Y += static_cast<float>(SignedRow) * CapturedBoarSpacing;
	}

	const FVector AreaExtent = CapturedBoarArea->GetScaledBoxExtent();
	const float CapsuleHalfHeight = Boar && Boar->GetCapsuleComponent()
			? Boar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			: 0.0f;

	return CapturedBoarArea->GetComponentLocation()
		+ CapturedBoarArea->GetForwardVector() * SlotOffset.X
		+ CapturedBoarArea->GetRightVector() * SlotOffset.Y
		+ CapturedBoarArea->GetUpVector()
		* (-AreaExtent.Z + CapsuleHalfHeight + CapturedBoarFloorOffset);
}

void ACage::ApplyDamage(float Damage)
{
	ApplyDamageInternal(Damage, nullptr, FVector::ZeroVector);
}

void ACage::ApplyAttackDamage(float Damage, const FVector& HitLocation, const FVector& HitDirection)
{
	ApplyDamageInternal(Damage, &HitLocation, HitDirection);
}

bool ACage::ApplyDamageInternal(float Damage, const FVector* HitLocation, const FVector& HitDirection)
{
	if (!CanAdvance() || bIsDestroyed || Damage <= 0.0f)
		return false;

	SecondsSinceDamage = 0.0f;

	const float PreviousRatio = CurrentHp / FMath::Max(MaxHp, 1.0f);
	CurrentHp = FMath::Clamp(CurrentHp - Damage, 0.0f, MaxHp);
	OnHealthChanged.Broadcast(CurrentHp, MaxHp);
	UE_LOG(LogREBoarGetch, Log, TEXT("[Cage] %s Damage %.0f -> HP %.0f/%.0f"), *GetName(), Damage, CurrentHp, MaxHp);

	// 命中音は攻撃が通った時だけ。危険域の警告はしきい値をまたいだ時と、再警告間隔ごとに限る。
	PlayCageSound(HitSound, false);
	const float Ratio = CurrentHp / FMath::Max(MaxHp, 1.0f);
	if (CurrentHp > 0.0f && Ratio <= WarningHpRatio && GetWorld())
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (PreviousRatio > WarningHpRatio || (WarningRepeatInterval > 0.0f && Now - LastWarningSoundTime >= WarningRepeatInterval))
		{
			LastWarningSoundTime = Now;
			PlayCageSound(LowHpWarningSound, true);
		}
	}

	// 攻撃が通った瞬間の衝撃波（演出のみ）。破壊になる一撃は破壊VFXに任せ、重ねない。
	if (CurrentHp > 0.0f && Presentation)
		Presentation->PlayHitVFX(HitLocation ? *HitLocation : Presentation->GetCageCenter(), HitDirection);

	if (CurrentHp <= 0.0f)
	{
		DestroyCage();
	}

	return true;
}

void ACage::DestroyCage()
{
	if (!CanAdvance() || bIsDestroyed)
		return;

	bIsDestroyed = true;

	OnCageDestroyed.Broadcast();
	PlayCageSound(DestroyedSound, true);

	// 破壊演出（演出のみ）。Actorを隠す前に、見た目のMeshの大きさから位置を決めて出す。
	if (Presentation)
		Presentation->PlayBreakVFX();

	// 同じActorで再出現するため、破壊中は表示・衝突・Tickだけを停止する。
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);

	// 解放時の通知から再集計されても収容数が残らないよう、反復前に元の配列を空にする。
	const TArray<TObjectPtr<ABoarBase>> ReleasedBoars = CapturedBoars;
	CapturedBoars.Empty();

	if (ReleasedBoars.Num() > 0)
		PlayCageSound(BoarReleasedSound, false);

	for (ABoarBase* Boar : ReleasedBoars)
	{
		if (IsValid(Boar))
		{
			Boar->OnDestroyed.RemoveDynamic(this, &ACage::HandleHousedBoarDestroyed);
			Boar->ReleaseBoar();
		}
	}

	NotifyHousedCountChanged();

	// 待機を0秒にできるよう、正の待機時間だけをゲーム時間のタイマーに渡す。
	if (RespawnDelay <= 0.0f)
	{
		RespawnCage();
		return;
	}

	GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &ACage::RespawnCage, RespawnDelay, false);
}

void ACage::RespawnCage()
{
	if (!CanAdvance() || !bIsDestroyed)
		return;

	CurrentHp = MaxHp;
	bIsDestroyed = false;
	LastWarningSoundTime = -1.0e9;

	OnHealthChanged.Broadcast(CurrentHp, MaxHp);
	OnRespawned.Broadcast();
	PlayCageSound(RestoredSound, true);

	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(true);

	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);
}

void ACage::PlayCageSound(USoundBase* Sound, bool bAs2D) const
{
	UGameInstance* Instance = GetGameInstance();
	auto* Audio = Sound && Instance ? Instance->GetSubsystem<UBoarAudioManagerSubsystem>() : nullptr;
	if (!Audio)
		return;

	if (bAs2D)
		Audio->PlaySoundEffect2D(const_cast<ACage*>(this), Sound);
	else
		Audio->PlayWorldSoundEffect(const_cast<ACage*>(this), Sound, GetActorLocation(), nullptr, SoundAttenuation);
}

bool ACage::CanAdvance() const
{
	return !bStageStopped && ABoarGameMode::IsStageProgressAllowed(this);
}

int32 ACage::GetCapturedBoarCount() const
{
	int32 Count = 0;

	for (const ABoarBase* Boar : CapturedBoars)
		if (IsValid(Boar) && Boar->IsCaptured())
			++Count;

	return Count;
}

void ACage::AppendHousedBoarUniqueIds(TArray<FName>& Out) const
{
	for (const ABoarBase* Boar : CapturedBoars)
		if (IsValid(Boar) && Boar->IsCaptured() && !Boar->BoarUniqueId.IsNone())
			Out.AddUnique(Boar->BoarUniqueId);
}

void ACage::NotifyHousedCountChanged()
{
	// 檻は自分の収容個体だけを保持し、全檻の合計とClear判定はGameModeへ委譲する。
	if (ABoarGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr)
		Mode->RefreshHousedBoarCount();
}

void ACage::ForgetBoar(ABoarBase* Boar)
{
	if (CapturedBoars.Remove(Boar) > 0)
	{
		if (Boar)
			Boar->OnDestroyed.RemoveDynamic(this, &ACage::HandleHousedBoarDestroyed);

		NotifyHousedCountChanged();
	}
}

void ACage::HandleHousedBoarDestroyed(AActor* Actor)
{
	ForgetBoar(Cast<ABoarBase>(Actor));
}

void ACage::SetStageStopped(bool bStopped)
{
	bStageStopped = bStopped;
	if
	(bStopped)
		GetWorldTimerManager().PauseTimer(RespawnTimerHandle);
	else
		GetWorldTimerManager().UnPauseTimer(RespawnTimerHandle);
}

void ACage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!CanAdvance() || bIsDestroyed)
		return;

	const float Previous = SecondsSinceDamage;
	SecondsSinceDamage += DeltaSeconds;
	const float RecoveryTime = FMath::Max(0.0f, SecondsSinceDamage - FMath::Max(Previous, RecoveryDelay));
	if (RecoveryPerSecond > 0.0f && RecoveryTime > 0.0f && CurrentHp < MaxHp)
	{
		CurrentHp = FMath::Min(MaxHp, CurrentHp + RecoveryPerSecond * RecoveryTime);
		OnHealthChanged.Broadcast(CurrentHp, MaxHp);
	}
}
