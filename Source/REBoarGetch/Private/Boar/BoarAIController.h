// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "BoarAIController.generated.h"

class UStateTreeAIComponent;

UCLASS()
class ABoarAIController : public AAIController
{
	GENERATED_BODY()

	// StateTreeによるAI制御コンポーネント
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStateTreeAIComponent> StateTreeAI;

public:
	ABoarAIController();

protected:
	// Pawnを所持したときに呼ばれる
	virtual void OnPossess(APawn* InPawn) override;
};
