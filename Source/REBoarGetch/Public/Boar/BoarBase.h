// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BoarBase.generated.h"

//class UBoarStatusComponent;
class UCaptureComponent;

UCLASS()
class REBOARGETCH_API ABoarBase : public ACharacter
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
	bool IsCaptured() const;

private:
	/// <summary>
	/// 捕獲状態を管理するコンポーネントです。
	/// </summary>
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCaptureComponent> CaptureComponent;

	/// <summary>
	/// イノシシのステータスを管理するコンポーネントです。
	/// </summary>
	//UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components", meta = (AllowPrivateAccess = "true"))
	//TObjectPtr<UBoarStatusComponent> StatusComponent;
};
