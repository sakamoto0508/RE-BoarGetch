#pragma once

#include "CoreMinimal.h"

class UBoarGadgetSubsystem;
class AGadgetBase;

/** GameModeが起動時の依存関係を解決し、Playerへ渡す。 */
struct FBoarGadgetLoadoutContext
{
	UBoarGadgetSubsystem* Progress = nullptr;
	TSubclassOf<AGadgetBase> TestGadgetClass;
	bool bTemporary = false;
};
