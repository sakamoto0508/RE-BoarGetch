// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "Boar/BoarDataAsset.h"
#include "GameFramework/Character.h"
#include "Navigation/PathFollowingComponent.h"
#include "BoarBase.generated.h"

//class UBoarStatusComponent;
class UCaptureComponent;
class ACage;
class APawn;
class APatrolPath;
class UAnimSequenceBase;

UCLASS()
class REBOARGETCH_API ABoarBase : public ACharacter
{
	GENERATED_BODY()

public:
	ABoarBase();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * イノシシを捕まえます。
	 */
	UFUNCTION(BlueprintCallable, Category = "Boar")
	void Capture();
	
	/**
	 * イノシシが解放されたときに呼ばれる関数。
	 */
	UFUNCTION(BlueprintCallable, Category = "Boar")
	void ReleaseBoar();

	/**
	 * 捕まっているかどうかを返します。
	 */
	UFUNCTION(BlueprintPure, Category = "Boar")
	bool IsCaptured() const;

	/**
	 * 周囲のプレイヤー・檻を探索し、認識データを更新します。
	 */
	UFUNCTION(BlueprintCallable, Category = "Boar|AI")
	bool RefreshPerceptionTargets();

	/** 現在認識中のプレイヤーです。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	APawn* GetPerceivedPlayer() const { return PerceivedPlayer; }

	/** 現在認識中の檻です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	ACage* GetPerceivedCage() const { return PerceivedCage; }

	/** 現在認識中プレイヤーまでの距離です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetPerceivedPlayerDistance() const { return PerceivedPlayerDistance; }

	/** 現在認識中の檻までの距離です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetPerceivedCageDistance() const { return PerceivedCageDistance; }

	/** 視認距離です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetSightRange() const { return SightRange; }

	/** 追跡目安距離です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetChaseRange() const { return ChaseRange; }

	/** 逃走目安距離です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetEscapeRange() const { return EscapeRange; }

	/** 檻優先度係数です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetCagePriorityWeight() const { return CagePriorityWeight; }

	/** プレイヤー優先度係数です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetPlayerPriorityWeight() const { return PlayerPriorityWeight; }

	/** 逃走優先度係数です。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetEscapePriorityWeight() const { return EscapePriorityWeight; }

	/** 檻攻撃可能か返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	bool CanAttackCage() const { return bCanAttackCage; }

	/** 檻攻撃への遷移判定に使用する値をデバッグ表示します。 */
	/** 現在の種類が檻へ与えるダメージです。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetCageAttackDamage() const { return CageAttackDamage; }

	/** 現在の予兆アニメーションまたは仮設定から攻撃予兆時間を返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI|Attack")
	float GetAttackTelegraphDuration() const;

	/** 攻撃予兆アニメーションの再生速度を返します。 */
	float GetAttackTelegraphPlayRate() const { return AttackTelegraphPlayRate; }

	/** 突進速度を返します。 */
	float GetChargeSpeed() const { return ChargeSpeed; }

	/** 突進の着弾距離を返します。 */
	float GetChargeImpactDistance() const { return ChargeImpactDistance; }

	/** 突進の最大継続時間を返します。 */
	float GetChargeMaxDuration() const { return ChargeMaxDuration; }

	/** 着弾後の後退距離を返します。 */
	float GetAttackRetreatDistance() const { return AttackRetreatDistance; }

	/** 着弾後の後退速度を返します。 */
	float GetAttackRetreatSpeed() const { return AttackRetreatSpeed; }

	/** 攻撃予兆開始をアニメーションBlueprintへ通知します。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boar|Animation")
	void OnAttackTelegraphStarted(float Duration, float PlayRate);

	/** 突進開始をアニメーションBlueprintへ通知します。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boar|Animation")
	void OnChargeStarted();

	/** 突進攻撃終了をアニメーションBlueprintへ通知します。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boar|Animation")
	void OnChargeAttackFinished();

	/** 現在の種別設定を返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	EBoarArchetype GetBoarArchetype() const { return BoarArchetype; }

	/** 種別設定を切り替え、共通パラメータを再適用します。 */
	UFUNCTION(BlueprintCallable, Category = "Boar|AI")
	void SetBoarArchetype(EBoarArchetype NewArchetype);

	/** 種別ロジックとして逃走を優先するか返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	bool ShouldPreferEscape() const;

	/** 青イノシシのスタミナ比率(0-1)を返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetStaminaNormalized() const;

	/** 青イノシシがスタミナ回復待機中か返します。 */
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	bool IsStaminaRecovering() const { return bIsRecoveringStamina; }

	/** パトロールパスを返します。 */
	UFUNCTION(BlueprintPure)
	APatrolPath* GetPatrolPath() const { return PatrolPath; }

	/** 現在のAIステートを画面とログへデバッグ表示します。 */
	/** 現在のAIステートと移動先を画面とログへデバッグ表示します。 */
private:
	/** 距離・視野角・遮蔽・絶対発見距離から対象を認識できるか判定します。 */
	bool CanDetectTarget(const AActor* TargetActor, float Distance) const;

	/** 檻から離れる移動が完了したとき、通常のStateTree AIを再開します。 */
	UFUNCTION()
	void HandleReleaseMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result);

	/** 解放移動の監視を終了し、通常のStateTree AIへ戻します。 */
	void FinishReleaseMovement();

	/** 種別ごとの共通AIパラメータを適用します。 */
	void ApplyArchetypeDefaults();

	/** 種別に応じたスタミナ更新を行います。 */
	void UpdateStamina(float DeltaSeconds);

	/**
	 * 捕獲状態を管理するコンポーネントです。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCaptureComponent> CaptureComponent;

	/** 檻から解放するときにNavMesh上の出現地点を探す半径です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|Capture",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float ReleaseRadius = 300.0f;

	/** 解放移動を完了と判定する目的地からの距離です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|Capture",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float ReleaseAcceptanceRadius = 50.0f;

	/** 現在、檻から歩いて離れている途中かを表します。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|Capture",
		meta = (AllowPrivateAccess = "true"))
	bool bIsLeavingCage = false;

	/** AIステートと目的地のPrint String表示を有効にするかです。 */
	/** イノシシ種別です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	EBoarArchetype BoarArchetype = EBoarArchetype::Normal;

	/** 種別ごとのAI・スタミナ・移動設定です。未設定時は従来の既定値を使用します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoarDataAsset> BoarDataAsset;

	/** 視認距離です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float SightRange = 1200.0f;

	/** 正面を中心とした視界全体の角度です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI|Perception",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "360.0"))
	float SightAngleDegrees = 120.0f;

	/** 視野角の外でも対象を必ず発見する距離です。遮蔽物は無視しません。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI|Perception",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AbsoluteDetectionRange = 200.0f;

	/** 追跡に入る目安距離です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float ChaseRange = 900.0f;

	/** 逃走を優先する目安距離です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float EscapeRange = 500.0f;

	/** 檻を狙う優先度係数です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float CagePriorityWeight = 1.0f;

	/** プレイヤーを狙う優先度係数です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float PlayerPriorityWeight = 1.0f;

	/** 逃走を選ぶ優先度係数です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float EscapePriorityWeight = 1.0f;

	/** 檻を攻撃対象に含めるかです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	bool bCanAttackCage = true;

	/** 檻への1回分の攻撃力です。DataAssetの設定から適用されます。 */
	float CageAttackDamage = 10.0f;

	/** 攻撃予兆に使用するアニメーションです。 */
	TObjectPtr<UAnimSequenceBase> AttackTelegraphAnimation;

	/** 予兆アニメーション未指定時に使用する仮の待機秒数です。 */
	float AttackTelegraphDuration = 1.0f;

	/** 予兆アニメーションの再生速度です。 */
	float AttackTelegraphPlayRate = 1.0f;

	/** 突進中の移動速度です。 */
	float ChargeSpeed = 1000.0f;

	/** 檻への着弾と判定する表面距離です。 */
	float ChargeImpactDistance = 30.0f;

	/** 突進の最大継続時間です。 */
	float ChargeMaxDuration = 2.0f;

	/** 着弾後に後退する距離です。 */
	float AttackRetreatDistance = 200.0f;

	/** 着弾後に後退する速度です。 */
	float AttackRetreatSpeed = 300.0f;

	/** スタミナ制御を使う種別かです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina", meta = (AllowPrivateAccess = "true"))
	bool bUseStamina = false;

	/** 最大スタミナです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float MaxStamina = 100.0f;

	/** 走行時のスタミナ消費速度です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float StaminaDrainPerSecond = 20.0f;

	/** 停止時のスタミナ回復速度です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float StaminaRecoveryPerSecond = 25.0f;

	/** スタミナ0から回復復帰する比率です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float StaminaRecoverExitRatio = 0.4f;

	/** 通常時の移動速度です。DataAssetの設定から適用されます。 */
	float BaseWalkSpeed = 450.0f;

	/** スタミナ回復待機中の移動速度倍率です。 */
	float StaminaRecoverySpeedMultiplier = 0.35f;

	/** 移動中と判定する速度のしきい値です。 */
	float MovingSpeedThreshold = 10.0f;

	/** 認識中のプレイヤーです。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<APawn> PerceivedPlayer;

	/** 認識中の檻です。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ACage> PerceivedCage;

	/** プレイヤーとの距離です。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	float PerceivedPlayerDistance = BIG_NUMBER;

	/** 檻との距離です。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	float PerceivedCageDistance = BIG_NUMBER;

	/** 現在スタミナです。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",meta = (AllowPrivateAccess = "true"))
	float CurrentStamina = 100.0f;

	/** スタミナ回復待機中かです。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",meta = (AllowPrivateAccess = "true"))
	bool bIsRecoveringStamina = false;

	/** 巡回パスです。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Boar|AI", meta=(AllowPrivateAccess="true"))
	TObjectPtr<APatrolPath> PatrolPath;
	
	/**
	 * イノシシのステータスを管理するコンポーネントです。
	 */
	//UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components", meta = (AllowPrivateAccess = "true"))
	//TObjectPtr<UBoarStatusComponent> StatusComponent;
};
