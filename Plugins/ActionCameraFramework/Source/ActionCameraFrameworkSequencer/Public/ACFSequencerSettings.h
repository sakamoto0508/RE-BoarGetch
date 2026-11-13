#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ACFSequencerTypes.h"
#include "ACFSequencerSettings.generated.h"

/** Project Settings > Plugins > Action Camera Framework - Sequencer。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Action Camera Framework - Sequencer"))
class ACTIONCAMERAFRAMEWORKSEQUENCER_API UACFSequencerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * Level Sequence の Camera Cut を自動で検出し、ACF の出力制御と復帰処理を行う。
	 * OFF にすると、このモジュールは状態の検出も通知も行わない（Sequencer のカメラはゲーム側で
	 * Add ACF External View Target に登録する。登録しなければ ACF がそのカメラの上に出力される）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Sequencer")
	bool bAutomaticHandling = true;

	/** 既定の連携設定（プレイヤー単位 / Level Sequence Actor 単位で上書きできる）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Sequencer")
	FACFSequencerCameraOptions DefaultOptions;

	/** 状態の変化（開始・終了・抑制）を LogActionCamera に Log で出す（OFF なら Verbose）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bLogStateChanges = false;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
