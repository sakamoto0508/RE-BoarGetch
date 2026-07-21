// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Cage.generated.h"
#include "Delegates/Delegate.h"

class ABoarBase;

UCLASS()
class REBOARGETCH_API ACage : public AActor
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	ACage();

	/** 檻のHP変更を通知するデリゲートです。 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
		FOnCageHealthChanged, float, CurrentHp, float, MaxHp);

	/** 檻の破壊を通知するデリゲートです。 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCageDestroyed);

	/** 檻の再出現を通知するデリゲートです。 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCageRespawned);

	/** 檻のHPが変更されたときに呼ばれるイベントです。 */
	UPROPERTY(BlueprintAssignable, Category = "Cage|Events")
	FOnCageHealthChanged OnHealthChanged;

	/** 檻が破壊されたときに呼ばれるイベントです。 */
	UPROPERTY(BlueprintAssignable, Category = "Cage|Events")
	FOnCageDestroyed OnDestroyed;

	/** 檻が再出現したときに呼ばれるイベントです。 */
	UPROPERTY(BlueprintAssignable, Category = "Cage|Events")
	FOnCageRespawned OnRespawned;

	/** 捕獲したイノシシを檻へ収容します。 */
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void CollectBoar(ABoarBase* Boar);

	/** 収容中のイノシシ数を返します。 */
	UFUNCTION(BlueprintPure, Category = "Cage")
	int32 GetCapturedBoarCount() const { return CapturedBoars.Num(); }

	/** 檻の現在HPを返します。 */
	UFUNCTION(BlueprintPure, Category = "Cage")
	float GetHP() const { return CurrentHp; }

	/** 檻へダメージを与えます。 */
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void ApplyDamage(float Damage);

	/** 檻が破壊状態かどうかを返します。 */
	UFUNCTION(BlueprintCallable, Category = "Cage")
	bool GetIsCageDestroyed() const { return bIsDestroyed; }

private:
	/** 収容中のイノシシです。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Cage", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<ABoarBase>> CapturedBoars;

	/** 檻を破壊状態にします。 */
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void DestroyCage();

	/** 檻を再出現させます。 */
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void RespawnCage();

	/** 檻の現在HPです。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage", meta = (AllowPrivateAccess = "true"))
	float CurrentHp = 100.f;

	/** 檻の最大HPです。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage", meta = (AllowPrivateAccess = "true"))
	float MaxHp = 100.f;

	/** 檻が破壊されてから再出現するまでの時間です。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cage", meta = (AllowPrivateAccess = "true"))
	float RespawnDelay = 10.f;

	/** 檻の再出現処理に使用するタイマーハンドルです。 */
	FTimerHandle RespawnTimerHandle;

	/** 破壊処理の多重実行を防ぐためのフラグです。 */
	bool bIsDestroyed = false;
};
