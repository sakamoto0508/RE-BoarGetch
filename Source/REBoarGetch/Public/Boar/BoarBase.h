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

	/// <summary>
	/// イノシシを捕まえます。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Boar")
	void Capture();
	
	/// <summary>
	/// イノシシが解放されたときに呼ばれる関数。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Boar")
	void ReleaseBoar();

	/// <summary>
	/// 捕まっているかどうかを返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category = "Boar")
	bool IsCaptured() const;

	/// <summary>
	/// 周囲のプレイヤー・檻を探索し、認識データを更新します。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Boar|AI")
	bool RefreshPerceptionTargets();

	/// <summary> 現在認識中のプレイヤーです。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	APawn* GetPerceivedPlayer() const { return PerceivedPlayer; }

	/// <summary> 現在認識中の檻です。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	ACage* GetPerceivedCage() const { return PerceivedCage; }

	/// <summary> 現在認識中プレイヤーまでの距離です。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetPerceivedPlayerDistance() const { return PerceivedPlayerDistance; }

	/// <summary> 現在認識中の檻までの距離です。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetPerceivedCageDistance() const { return PerceivedCageDistance; }

	/// <summary> 視認距離です。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetSightRange() const { return SightRange; }

	/// <summary> 追跡目安距離です。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetChaseRange() const { return ChaseRange; }

	/// <summary> 逃走目安距離です。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetEscapeRange() const { return EscapeRange; }

	/// <summary> 檻優先度係数です。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetCagePriorityWeight() const { return CagePriorityWeight; }

	/// <summary> プレイヤー優先度係数です。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetPlayerPriorityWeight() const { return PlayerPriorityWeight; }

	/// <summary> 逃走優先度係数です。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetEscapePriorityWeight() const { return EscapePriorityWeight; }

	/// <summary> 檻攻撃可能か返します。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	bool CanAttackCage() const { return bCanAttackCage; }

	/// <summary> 現在の種別設定を返します。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	EBoarArchetype GetBoarArchetype() const { return BoarArchetype; }

	/// <summary> 種別設定を切り替え、共通パラメータを再適用します。 </summary>
	UFUNCTION(BlueprintCallable, Category = "Boar|AI")
	void SetBoarArchetype(EBoarArchetype NewArchetype);

	/// <summary> 種別ロジックとして逃走を優先するか返します。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	bool ShouldPreferEscape() const;

	/// <summary> 青イノシシのスタミナ比率(0-1)を返します。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	float GetStaminaNormalized() const;

	/// <summary> 青イノシシがスタミナ回復待機中か返します。 </summary>
	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	bool IsStaminaRecovering() const { return bIsRecoveringStamina; }

	/// <summary> パトロールパスを返します。 </summary>
	UFUNCTION(BlueprintPure)
	APatrolPath* GetPatrolPath() const { return PatrolPath; }

private:
	/// <summary> 種別ごとの共通AIパラメータを適用します。 </summary>
	void ApplyArchetypeDefaults();

	/// <summary> 種別に応じたスタミナ更新を行います。 </summary>
	void UpdateStamina(float DeltaSeconds);

	/// <summary>
	/// 捕獲状態を管理するコンポーネントです。
	/// </summary>
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCaptureComponent> CaptureComponent;

	/// <summary> イノシシ種別です。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	EBoarArchetype BoarArchetype = EBoarArchetype::Normal;

	/// <summary> 視認距離です。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float SightRange = 1200.0f;

	/// <summary> 追跡に入る目安距離です。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float ChaseRange = 900.0f;

	/// <summary> 逃走を優先する目安距離です。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float EscapeRange = 500.0f;

	/// <summary> 檻を狙う優先度係数です。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float CagePriorityWeight = 1.0f;

	/// <summary> プレイヤーを狙う優先度係数です。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float PlayerPriorityWeight = 1.0f;

	/// <summary> 逃走を選ぶ優先度係数です。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float EscapePriorityWeight = 1.0f;

	/// <summary> 檻を攻撃対象に含めるかです。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	bool bCanAttackCage = true;

	/// <summary> スタミナ制御を使う種別かです。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina", meta = (AllowPrivateAccess = "true"))
	bool bUseStamina = false;

	/// <summary> 最大スタミナです。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float MaxStamina = 100.0f;

	/// <summary> 走行時のスタミナ消費速度です。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float StaminaDrainPerSecond = 20.0f;

	/// <summary> 停止時のスタミナ回復速度です。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float StaminaRecoveryPerSecond = 25.0f;

	/// <summary> スタミナ0から回復復帰する比率です。 </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float StaminaRecoverExitRatio = 0.4f;

	/// <summary> 認識中のプレイヤーです。 </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<APawn> PerceivedPlayer;

	/// <summary> 認識中の檻です。 </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ACage> PerceivedCage;

	/// <summary> プレイヤーとの距離です。 </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	float PerceivedPlayerDistance = BIG_NUMBER;

	/// <summary> 檻との距離です。 </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI", meta = (AllowPrivateAccess = "true"))
	float PerceivedCageDistance = BIG_NUMBER;

	/// <summary> 現在スタミナです。 </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",meta = (AllowPrivateAccess = "true"))
	float CurrentStamina = 100.0f;

	/// <summary> スタミナ回復待機中かです。 </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|AI|Stamina",meta = (AllowPrivateAccess = "true"))
	bool bIsRecoveringStamina = false;

	/// <summary> 巡回パスです。 </summary>
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Boar|AI", meta=(AllowPrivateAccess="true"))
	TObjectPtr<APatrolPath> PatrolPath;
	
	/// <summary>
	/// イノシシのステータスを管理するコンポーネントです。
	/// </summary>
	//UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components", meta = (AllowPrivateAccess = "true"))
	//TObjectPtr<UBoarStatusComponent> StatusComponent;
};
