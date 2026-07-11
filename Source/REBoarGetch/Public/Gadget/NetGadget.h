// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GadgetBase.h"
#include "NetGadget.generated.h"

class ABoarBase;

UCLASS()
class REBOARGETCH_API ANetGadget : public AGadgetBase
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ANetGadget();
	
	/// <summary>
	/// ネットを使用します。
	/// </summary>
	virtual void Use_Implementation(AActor* TargetActor) override;

private:
	/// <summary>
	/// ネットの捕獲範囲です。
	/// </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true", ClampMin = "100.0"))
	float CaptureRadius = 300.0f;
	
	/// <summary>
	/// 範囲内のイノシシを探して捕まえます。
	/// </summary>
	void CaptureBoarInRange(const FVector& CenterLocation);
};
