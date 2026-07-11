// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BoarBase.generated.h"

class UBoarStatusComponent;
class UCaptureComponent;

UCLASS()
class ABoarBase : public ACharacter
{
	GENERATED_BODY()

public:	
	ABoarBase();
	virtual void BeginPlay() override;
	
	/// <summary>
	/// イノシシを捕まえます。
	/// </summary>
	UFUNCTION(BlueprintCallable, Category = "Boar")
	void Capture();

	/// <summary>
	/// 捕まっているかどうかを返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category = "Boar")
	bool IsCaptured() const { return bIsCaptured; }

private:
	/// <summary>
	/// 捕獲状態です。
	/// </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar", meta = (AllowPrivateAccess = "true"))
	bool bIsCaptured = false;

	/// <summary>
	/// イノシシのステータスを管理するコンポーネントです。
	/// </summary>
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoarStatusComponent> StatusComponent;
};
