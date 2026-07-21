// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BoarGameMode.generated.h"

/**
 * ゲームのルール全体を管理します。
 * 今は空でOK。将来、ステージクリア判定などを追加します。
 */
UCLASS()
class REBOARGETCH_API ABoarGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABoarGameMode();

	/** 捕獲成功時の共通後処理（檻送致/カウント/ドロップ）を実行します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Game")
	void HandleBoarCaptured(class ABoarBase* Boar);

	/** プレイヤー死亡時の終了処理を実行します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Game")
	void HandlePlayerDeath(class ABoarPlayerCharacter* PlayerCharacter);

	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Game")
	int32 GetCapturedBoarCount() const { return CapturedBoarCount; }

protected:
	/** ゲームオーバー演出・遷移をBP側で実装するためのイベントです。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Game")
	void OnGameOver();

private:
	/** 捕獲時に回復ピックアップを出す確率です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Capture", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float HealItemDropChance = 0.3f;

	/** 捕獲時に生成する回復ピックアップのクラスです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Capture", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<class AHealPickup> HealPickupClass;

	/** 現在の累計捕獲数です。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "REBoarGetch|Capture", meta = (AllowPrivateAccess = "true"))
	int32 CapturedBoarCount = 0;
};
