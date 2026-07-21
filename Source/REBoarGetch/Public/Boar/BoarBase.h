// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BoarBase.generated.h"

//class UBoarStatusComponent;
class UCaptureComponent;
class ACage;
class APawn;
class APatrolPath;

UENUM(BlueprintType)
enum class EBoarArchetype : uint8
{
	Normal,
	Red,
	Blue,
	CageBreaker,
	EscapeSpecialist
};

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

private:
	/** 種別ごとの共通AIパラメータを適用します。 */
	void ApplyArchetypeDefaults();

	/** 種別に応じたスタミナ更新を行います。 */
	void UpdateStamina(float DeltaSeconds);

	/**
	 * 捕獲状態を管理するコンポーネントです。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCaptureComponent> CaptureComponent;

	/** イノシシ種別です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	EBoarArchetype BoarArchetype = EBoarArchetype::Normal;

	/** 視認距離です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float SightRange = 1200.0f;

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
