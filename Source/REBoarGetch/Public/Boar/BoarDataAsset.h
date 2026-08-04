#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BoarDataAsset.generated.h"

class UAnimSequenceBase;

UENUM(BlueprintType)
enum class EBoarArchetype : uint8
{
	Normal,
	PlayerAttacker,
	EscapeSpecialist,
	CageBreaker,
};

/** イノシシ1種分のAI・スタミナ・移動設定です。 */
USTRUCT(BlueprintType)
struct REBOARGETCH_API FBoarArchetypeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Priority", meta = (ClampMin = "0.0"))
	float PlayerPriorityWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Priority", meta = (ClampMin = "0.0"))
	float CagePriorityWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Priority", meta = (ClampMin = "0.0"))
	float EscapePriorityWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bCanAttackCage = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
	float CageAttackDamage = 10.0f;

	/** 攻撃予兆に使用するアニメーションです。指定時はクリップ長を予兆時間に使用します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Charge")
	TObjectPtr<UAnimSequenceBase> AttackTelegraphAnimation;

	/** 予兆アニメーション未指定時に使用する仮の待機秒数です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Charge", meta = (ClampMin = "0.0"))
	float AttackTelegraphDuration = 1.0f;

	/** 予兆アニメーションの再生速度です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Charge", meta = (ClampMin = "0.01"))
	float AttackTelegraphPlayRate = 1.0f;

	/** 突進中の移動速度です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Charge", meta = (ClampMin = "0.0"))
	float ChargeSpeed = 1000.0f;

	/** 檻への着弾と判定する表面距離です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Charge", meta = (ClampMin = "0.0"))
	float ChargeImpactDistance = 30.0f;

	/** 突進が終了しない場合に打ち切る秒数です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Charge", meta = (ClampMin = "0.1"))
	float ChargeMaxDuration = 2.0f;

	/** 着弾後に後退する距離です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Charge", meta = (ClampMin = "0.0"))
	float AttackRetreatDistance = 200.0f;

	/** 着弾後に後退する速度です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Charge", meta = (ClampMin = "0.0"))
	float AttackRetreatSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float SightRange = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float SightAngleDegrees = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float AbsoluteDetectionRange = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float ChaseRange = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float EscapeRange = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina")
	bool bUseStamina = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float MaxStamina = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float StaminaDrainPerSecond = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float StaminaRecoveryPerSecond = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StaminaRecoverExitRatio = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float BaseWalkSpeed = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float TurnRateDegreesPerSecond = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StaminaRecoverySpeedMultiplier = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float MovingSpeedThreshold = 10.0f;
};

/** イノシシ種別ごとの調整値をまとめるDataAssetです。 */
UCLASS(BlueprintType)
class REBOARGETCH_API UBoarDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UBoarDataAsset();

	const FBoarArchetypeSettings& GetSettings(EBoarArchetype Archetype) const;
	static const FBoarArchetypeSettings& GetDefaultSettings(EBoarArchetype Archetype);

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boar", meta = (AllowPrivateAccess = "true"))
	TMap<EBoarArchetype, FBoarArchetypeSettings> ArchetypeSettings;
};
