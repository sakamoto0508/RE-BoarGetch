#pragma once

#include "CoreMinimal.h"

class UBoarGadgetSubsystem;
class AGadgetBase;

/** GameModeが起動時の依存関係を解決し、Playerへ渡す。 */
struct FBoarGadgetLoadoutContext
{
	/** 初期化時の装備構成と永続進行の取得元。 */
	UBoarGadgetSubsystem* Progress = nullptr;
	/** 施設初期化で優先して装備する試用ガジェットのクラス。 */
	TSubclassOf<AGadgetBase> TestGadgetClass;
	/** 初期装備を永続保存しない一時構成として扱うか。 */
	bool bTemporary = false;
};
