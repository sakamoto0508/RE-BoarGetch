// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Cage.generated.h"

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

	/// <summary>
	/// イノシシを檻に入れます。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void CollectBoar(ABoarBase* Boar);

	/// <summary>
	/// 収容イノシシ数を返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category = "Cage")
	int32 GetCapturedBoarCount() const { return CapturedBoars.Num(); }

	/// <summary>
	/// 檻の耐久値を返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category = "Cage")
	float GetHP() const { return CurrentHp; }

	/// <summary>
	/// 檻にダメージを与えます。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void ApplyDamage(float Damage);

	/// <summary>
	/// 檻が破壊されているかを返します。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Cage")
	bool GetIsCageDestroyed() const { return CurrentHp <= 0.f; }

private:
	/// <summary>
	/// 収容中のイノシシです。
	/// </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Cage", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<ABoarBase>> CapturedBoars;

	/// <summary>
	/// 檻を破壊します。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void DestroyCage();

	/// <summary>
	/// 檻を再出現させます。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void RespawnCage();

	/// <summary>
	/// 檻の現在の耐久値です。
	/// </summary>
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cage", meta=(AllowPrivateAccess="true"))
	float CurrentHp = 100.f;

	/// <summary>
	/// 檻の最大耐久値です。
	/// </summary>
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cage", meta=(AllowPrivateAccess="true"))
	float MaxHp = 100.f;

	/// <summary>
	/// 檻が破壊された後、再出現するまでの時間です。
	/// </summary>
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cage", meta=(AllowPrivateAccess="true"))
	float RespawnDelay = 10.f;
	
	/// <summary>
	/// 檻の復活処理に使用するタイマーハンドルです。
	/// </summary>
	FTimerHandle RespawnTimerHandle;

	/// <summary>
	/// 二重に破壊処理が実行されることを防ぎます。
	/// </summary>
	bool bIsDestroyed = false;
};
