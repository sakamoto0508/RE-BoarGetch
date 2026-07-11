// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Cage.generated.h"

UCLASS()
class ACage : public AActor
{
	GENERATED_BODY()
	
public:	
	ACage();

	/// <summary>
	/// イノシシを檻に入れます。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Cage")
	void CollectBoar(ABoarBase* Boar);

	/// <summary>
	/// 収容イノシシ数を返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Cage")
	int32 GetCapturedBoarCount() const { return CapturedBoars.Num(); }

private:
	/// <summary>
	/// 収容中のイノシシです。
	/// </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "REBoarGetch|Cage", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<ABoarBase>> CapturedBoars;
};
