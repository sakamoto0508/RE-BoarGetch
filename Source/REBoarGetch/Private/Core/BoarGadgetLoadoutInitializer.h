#pragma once

class UGadgetComponent;

/** World-level initialization policy shared by BeginPlay and explicit stage initialization. */
namespace BoarGadgetLoadoutInitializer
{
void Initialize(UGadgetComponent *Component, bool bAllowStageDeferral);
}
