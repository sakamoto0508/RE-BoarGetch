#include "ACFTransitionSampleDirector.h"
#include "ACFTransitionSettings.h"
#include "Modules/ModuleManager.h"

UACFTransitionProjectSettings::UACFTransitionProjectSettings()
{
	TransitionMaterial = FSoftObjectPath(TEXT("/ActionCameraFramework/Transitions/M_ACFTransition.M_ACFTransition"));
}

/**
 * Phase 7D: screen / level transitions (README 33 章). Runtime; depends on engine modules only (not on the camera Core).
 * The sample console commands (ACF.Transition.*) are in ACFTransitionCommands.cpp (not in Shipping).
 */
class FActionCameraFrameworkTransitionsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		AACFTransitionSampleDirector::RegisterCommandLineBootstrap();
	}
};

IMPLEMENT_MODULE(FActionCameraFrameworkTransitionsModule, ActionCameraFrameworkTransitions)
