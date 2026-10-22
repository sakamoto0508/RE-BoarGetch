#pragma once

#include "CoreMinimal.h"

class UBoarGadgetSubsystem;
class AGadgetBase;

/** GameMode resolves startup dependencies and passes them to the player. */
struct FBoarGadgetLoadoutContext
{
	UBoarGadgetSubsystem* Progress = nullptr;
	TSubclassOf<AGadgetBase> TestGadgetClass;
	bool bTemporary = false;
};
