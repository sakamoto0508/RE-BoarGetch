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
	float GetHP() const { return HP; }

	/// <summary>
	/// 檻にダメージを与えます。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Cage")
	void ApplyDamage(float Damage);

private:
	/// <summary>
	/// 収容中のイノシシです。
	/// </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Cage", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<ABoarBase>> CapturedBoars;

	///<summary>
	/// 檻の耐久値です。
	/// </summary>
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cage", meta=(AllowPrivateAccess="true"))
	float HP = 100.f;
};
