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

/**
 * 逃走開始の旋回と、逃走中の大きな切り返し（減速→踏ん張りながら旋回→停止→加速。その場旋回の段階は無い）の調整値です（AKI-152）。
 * 逃走方向はN秒間（DirectionCommitDuration）維持し、N秒ごとに認識中のプレイヤーから方向を再評価します（AKI-172）。
 * 逃走中に向きを変えるのは、逃走開始の旋回・切り返し・障害物復帰だけです。再評価で方向差がPivotAngleThreshold以上なら切り返し、未満なら向きを変えずに走り続けます。
 * 逃走は最短EscapeMinDuration秒続け、プレイヤーをEscapeLoseDuration秒認識できなくなったら（逃げ切ったら）終えます。
 * bEnabledがfalseの種別は従来どおりCharacterMovementの向き追従だけで逃走します。
 */
USTRUCT(BlueprintType)
struct REBOARGETCH_API FBoarEscapeTurnSettings
{
	GENERATED_BODY()

	/** 逃走開始の旋回と大きな切り返しを使うかです。初期値ではNormalだけ有効です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn")
	bool bEnabled = false;

	/** 逃走開始時、逃走方向との差がこの角度以上ならその場で素早く向きを変えてから走り出します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Start", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "Degrees"))
	float StartTurnMinAngle = 45.0f;

	/** 逃走開始の旋回速度です。切り返しの旋回より速くします。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Start", meta = (ClampMin = "1.0", Units = "DegreesPerSecond"))
	float StartTurnRateDegreesPerSecond = 540.0f;

	/** 逃走開始の加速時間です。この時間で初速割合から逃走速度へ戻します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Start", meta = (ClampMin = "0.0", Units = "Seconds"))
	float StartAccelerationDuration = 0.35f;

	/** 逃走開始の加速を始める速度割合です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Start", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StartInitialSpeedRatio = 0.3f;

	/**
	 * 大きな方向転換と判定する角度です。方向維持時間（DirectionCommitDuration）の終わりの再評価で、
	 * プレイヤーから離れる新しい方向と維持方向の差がこの角度以上なら切り返します。未満なら向きを変えず、今の方向へ走り続けます（走りながら曲がらない）。
	 * 障害物ではObstaclePivotAngleThresholdを使います。比較候補：70〜90°（AKI-172）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Pivot", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "Degrees"))
	float PivotAngleThreshold = 100.0f;

	/** 切り返しの減速時間です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Pivot", meta = (ClampMin = "0.0", Units = "Seconds"))
	float DecelerateDuration = 0.25f;

	/**
	 * 切り返しで止まっている時間（捕獲の隙）です（AKI-172）。減速して止まってから走り出すまでの秒数で、
	 * この間に踏ん張りながら切り返し先へ向きを変えます（180°なら 180÷この秒数 °/秒で回ります）。
	 * 長くすると捕まえやすく、向き変えもゆっくりになります。踏ん張りのMontageが終わった後は最後のポーズで止まったままです。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Pivot", meta = (ClampMin = "0.05", Units = "Seconds"))
	float PivotStopDuration = 1.0f;

	/** 止まっている時間のうち、最初の踏ん張りの部分の秒数です（ログ・段階表示用。PivotStopDurationより短くします）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Pivot", meta = (ClampMin = "0.0", Units = "Seconds"))
	float BraceDuration = 0.2f;

	/** 切り返し後の加速時間です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Pivot", meta = (ClampMin = "0.0", Units = "Seconds"))
	float PivotAccelerationDuration = 0.5f;

	/** 切り返し後の加速を始める速度割合です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Pivot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PivotInitialSpeedRatio = 0.15f;

	/**
	 * 切り返し（または逃走開始の旋回）を終えて再加速を始めてから、障害物・再入による切り返しを許可するまでの秒数です。
	 * 壁際で停止旋回を繰り返さないための抑制で、抑制中の障害物回避は止まらず走りながら曲がります。
	 * 方向維持時間の終わりの再評価（Reevaluate）は、切り返し後に必ずDirectionCommitDuration走ってから行うため対象外です。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Pivot", meta = (ClampMin = "0.0", Units = "Seconds"))
	float MinPivotInterval = 1.2f;

	/**
	 * 障害物・NavMesh端で直進できないときの方向転換（NaturalPivot/Obstacle）を、停止旋回にする最小角度です。
	 * これ未満の曲がりは止まらず走りながら曲がります（小さな障害物回避）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Pivot", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "Degrees"))
	float ObstaclePivotAngleThreshold = 70.0f;

	/**
	 * 逃走方向の維持時間（N秒、AKI-172）です。逃走開始・切り返し・障害物復帰・再評価で方向を確定してからこの秒数は、
	 * プレイヤーが横や背後へ動いても逃走方向を引き直しません（NavMeshに沿う±20°の小さな修正と障害物復帰だけ行います）。
	 * 経過したら認識中のプレイヤーの位置から方向を再評価します。経過しても必ず止まるわけではありません。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Commit", meta = (ClampMin = "0.1", Units = "Seconds"))
	float DirectionCommitDuration = 1.2f;

	/**
	 * 維持方向へ逃走先を延長する1区間の長さです（NavMeshの経路を引く単位）。
	 * 逃走先の手前（SegmentEndCheckDistance）で同じ方向へ次の区間を延長し、直進できなければ障害物復帰へ移ります。方向の判断には使いません。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Commit", meta = (ClampMin = "100.0", Units = "Centimeters"))
	float RunSegmentDistance = 500.0f;

	/**
	 * 逃走先までの残り距離がこれ以下になったら、次の区間への延長（直進できなければ障害物復帰）を行います。
	 * 減速（DecelerateDuration）で止まり切れる距離より長くします。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Commit", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float SegmentEndCheckDistance = 300.0f;

	/**
	 * 逃走Stateを別Stateへの遷移で抜けてから、同じ逃走として再開できる最大秒数です（AKI-172）。
	 * 再開には「停止・捕獲で終えていない」「同じプレイヤーから逃げている」も必要です。再開時は逃走方向と方向維持の残り時間を引き継ぎ、
	 * 方向を計算し直しません。超えた場合・条件を満たさない場合は新しい逃走として開始します。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Commit", meta = (ClampMin = "0.0", Units = "Seconds"))
	float ContinueEscapeGraceDuration = 0.5f;

	/**
	 * 通常の逃走走行中（切り返し・逃走開始の旋回以外）の向き追従の最大旋回速度です。小さいほど直進的に見えます。
	 * 0以下ならMovementのTurnRateDegreesPerSecond（通常歩行と同じ）を使います。逃走を終えると通常値へ戻します。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Run", meta = (ClampMin = "0.0", Units = "DegreesPerSecond"))
	float RunTurnRateDegreesPerSecond = 180.0f;

	/**
	 * 逃走を続ける最短の時間です（AKI-172）。逃げ始めてからこの秒数は、プレイヤーを見失っても逃走を終えません。
	 * 逃走と徘徊を短い間隔で繰り返さないためのものです。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Session", meta = (ClampMin = "0.0", Units = "Seconds"))
	float EscapeMinDuration = 3.0f;

	/**
	 * 逃げ切ったと判定するまでの時間です（AKI-172）。逃走中、プレイヤーを認識できない状態（視界外かつThreatMemoryRange外）が
	 * この秒数続いたら逃げ切りとして逃走を終え、徘徊へ戻ります。それまでは見失っても同じ相手から逃げ続けます。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Session", meta = (ClampMin = "0.0", Units = "Seconds"))
	float EscapeLoseDuration = 2.0f;

	/**
	 * 進めなくなったとき（Stuck）、プレイヤーがこの距離以内にいれば障害物ではなくプレイヤーに阻まれたとみなします（AKI-172）。
	 * その場合は障害物復帰の走行旋回をせず、離れる方向との差がPivotAngleThreshold以上なら切り返します。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EscapeTurn|Session", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float PlayerBlockDistance = 250.0f;
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

	/** 追跡中のプレイヤーに対して攻撃予兆を開始する距離です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|PlayerAttack", meta = (ClampMin = "0.0"))
	float PlayerAttackRange = 400.0f;

	/** プレイヤーへの突進終了後、次の判断へ移るまで停止する秒数です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|PlayerAttack", meta = (ClampMin = "0.0"))
	float AttackRecoveryDuration = 0.8f;

	/** プレイヤーへの攻撃終了から、次の攻撃予兆を開始できるまでの秒数です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|PlayerAttack", meta = (ClampMin = "0.0"))
	float PlayerAttackCooldown = 1.5f;

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

	/**
	 * 一度見つけたプレイヤーを、視界から外れてもこの距離内なら脅威として認識し続けます。
	 * 逃げて背を向けると視野角から外れるため、追われている間の逃走継続に使います。0で無効です。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float ThreatMemoryRange = 0.0f;

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

	/** 有効時は逃走Stateでの移動中だけスタミナを消費し、徘徊中の歩行では消費しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina")
	bool bDrainStaminaOnlyWhileEscaping = true;

	/** 有効時はスタミナ0で移動要求を止め、復帰割合まで回復するまでその場で停止します。無効時は減速のみです。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina")
	bool bStopWhileRecoveringStamina = true;

	/** 通常時に使用する歩行速度です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float BaseWalkSpeed = 450.0f;

	/**
	 * アニメーションの速度正規化に使う基準速度です（ABPのBlendSpace X=1.0＝小走りが足滑りなく見える速度）。
	 * Boarの歩き・小走り・走りアニメは約450cm/sを基準に作られているため、移動速度（BaseWalkSpeed）とは分けて持ちます。
	 * 0以下ならBaseWalkSpeedを使います（従来の挙動）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", Units = "CentimetersPerSecond"))
	float AnimationReferenceSpeed = 0.0f;

	/** 逃走State中に通常速度へ掛ける倍率です。1.0なら徘徊と同じ速度で逃走します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.1"))
	float EscapeSpeedMultiplier = 1.0f;

	/** 1秒あたりに回転できる最大Yaw角度です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float TurnRateDegreesPerSecond = 360.0f;

	/** スタミナ回復待機中に通常速度へ掛ける倍率です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StaminaRecoverySpeedMultiplier = 0.35f;

	/** スタミナ消費対象の移動中と判定する速度のしきい値です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float MovingSpeedThreshold = 10.0f;

	/** 逃走開始の旋回と逃走中の大きな切り返しの設定です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	FBoarEscapeTurnSettings EscapeTurn;
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
