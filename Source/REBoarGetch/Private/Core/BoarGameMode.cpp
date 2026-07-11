// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/BoarGameMode.h"

#include "Player/BoarPlayerController.h"
#include "Player/BoarPlayerCharacter.h"

ABoarGameMode::ABoarGameMode()
{
	// プレイヤーコントローラーを指定
	PlayerControllerClass = ABoarPlayerController::StaticClass();

	// デフォルトポーンクラスを指定
	DefaultPawnClass = ABoarPlayerCharacter::StaticClass();
}