#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BoarDataAsset.generated.h"

class UAnimSequenceBase;

/** イノシシごとの行動傾向と能力設定を選択する種別です。 */
UENUM(BlueprintType)
enum class EBoarArchetype : uint8
{
	/** 檻とプレイヤーへの反応を標準値で行う通常種です。 */
	Normal,
	/** プレイヤーへの追跡・攻撃を優先する種です。 */
	PlayerAttacker,
	/** プレイヤーを認識した際の逃走を優先する種です。 */
	EscapeSpecialist,
	/** 檻への接近・攻撃を優先する種です。 */
	CageBreaker,
};

/** イノシシ1種分のAI・スタミナ・移動設定です。 */
USTRUCT(BlueprintType)
struct REBOARGETCH_API FBoarArchetypeSettings
{
	GENERATED_BODY()

	/** プレイヤーを行動対象として選ぶ優先度です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Priority", meta = (ClampMin = "0.0"))
	float PlayerPriorityWeight = 1.0f;

	/** 檻を行動対象として選ぶ優先度です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Priority", meta = (ClampMin = "0.0"))
	float CagePriorityWeight = 1.0f;

	/** プレイヤーからの逃走を選ぶ優先度です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Priority", meta = (ClampMin = "0.0"))
	float EscapePriorityWeight = 1.0f;

	/** この種が檻への攻撃行動を実行できるかを表します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bCanAttackCage = true;

	/** 檻への攻撃が1回命中した際に与えるダメージ量です。 */
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

	/** プレイヤーや檻を視覚認識できる最大距離です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float SightRange = 1200.0f;

	/** 正面を中心とした視野角です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float SightAngleDegrees = 120.0f;

	/** 視野角の外側でも必ず発見できる至近距離です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float AbsoluteDetectionRange = 200.0f;

	/** プレイヤー追跡へ移行する距離条件です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float ChaseRange = 900.0f;

	/** プレイヤーからの逃走を開始する距離条件です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float EscapeRange = 500.0f;

	/** 移動時のスタミナ消費と回復制御を使用するかを表します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina")
	bool bUseStamina = false;

	/** スタミナの最大値です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float MaxStamina = 100.0f;

	/** 移動中に1秒あたり消費するスタミナ量です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float StaminaDrainPerSecond = 20.0f;

	/** 停止中または回復待機中に1秒あたり回復するスタミナ量です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float StaminaRecoveryPerSecond = 25.0f;

	/** スタミナ切れ後、通常移動へ復帰する最大値に対する回復割合です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StaminaRecoverExitRatio = 0.4f;

	/** 通常時に使用する歩行速度です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float BaseWalkSpeed = 450.0f;

	/** 1秒あたりに回転できる最大Yaw角度です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float TurnRateDegreesPerSecond = 360.0f;

	/** スタミナ回復待機中に通常速度へ掛ける倍率です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StaminaRecoverySpeedMultiplier = 0.35f;

	/** スタミナ消費対象の移動中と判定する速度のしきい値です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float MovingSpeedThreshold = 10.0f;
};

/** イノシシ種別ごとの調整値をまとめるDataAssetです。 */
UCLASS(BlueprintType)
class REBOARGETCH_API UBoarDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 全種別に調整可能な既定値を登録します。 */
	UBoarDataAsset();

	/** 指定種別の設定を返し、未登録の場合はコード上の既定値へフォールバックします。 */
	const FBoarArchetypeSettings& GetSettings(EBoarArchetype Archetype) const;
	/** DataAsset未設定時にも使用できる、指定種別のコード既定値を返します。 */
	static const FBoarArchetypeSettings& GetDefaultSettings(EBoarArchetype Archetype);

private:
	/** イノシシ種別をキーとしてAI・戦闘・移動設定を保持します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boar", meta = (AllowPrivateAccess = "true"))
	TMap<EBoarArchetype, FBoarArchetypeSettings> ArchetypeSettings;
};
