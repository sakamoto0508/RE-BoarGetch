#pragma once

#include "Core/BoarInitializationContext.h"

class UGadgetComponent;
class UWorld;

namespace BoarGadgetLoadoutInitializer
{
FBoarGadgetLoadoutContext ResolveContext(UWorld* World);
bool Initialize(UGadgetComponent* Component, const FBoarGadgetLoadoutContext& Context);
}
