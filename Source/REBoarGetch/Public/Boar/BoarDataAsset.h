#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BoarDataAsset.generated.h"

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
