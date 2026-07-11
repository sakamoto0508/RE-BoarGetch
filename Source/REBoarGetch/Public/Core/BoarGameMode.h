// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BoarGameMode.generated.h"

/// <summary>
/// ゲームのルール全体を管理します。
/// 今は空でOK。将来、ステージクリア判定などを追加します。
/// </summary>
UCLASS()
class REBOARGETCH_API ABoarGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABoarGameMode();
};
