// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "BoarAIController.generated.h"

class UStateTreeAIComponent;

/** StateTreeを使ってイノシシPawnを制御するAIControllerです。 */
UCLASS()
class ABoarAIController : public AAIController
{
	GENERATED_BODY()

	// StateTreeによるAI制御コンポーネント
	/** 所持したイノシシのStateTreeを実行するAIコンポーネントです。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStateTreeAIComponent> StateTreeAI;

public:
	/** StateTree AIコンポーネントを生成します。 */
	ABoarAIController();

protected:
	// Pawnを所持したときに呼ばれる
	/** Pawn所持後にStateTreeの実行に必要な初期状態を整えます。 */
	virtual void OnPossess(APawn* InPawn) override;
};
