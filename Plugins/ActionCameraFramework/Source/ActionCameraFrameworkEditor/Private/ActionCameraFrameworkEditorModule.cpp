#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "SACFCameraTuningPanel.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "ActionCameraFrameworkEditor"

/**
 * Editor module for Action Camera Framework (README 13 章 / 23 章 / 25 章).
 *
 * - The "ACF Camera Tuning" tab (Phase 7A-2, Window > Developer Tools > Debug): edits the camera of a PIE player through
 *   the runtime tuning API (ACFCameraTuning.h). Editor only; the runtime modules do not depend on it.
 * - Presets and sequences are authored with the standard Data Asset editor and checked by UACFCameraPreset::IsDataValid /
 *   UACFCameraSequence::IsDataValid (shared implementation in ACFCameraPresetValidation, runtime module, WITH_EDITOR only).
 * - The tests that need editor-only modules (Data Validation, Asset Registry, PIE).
 */
class FActionCameraFrameworkEditorModule : public IModuleInterface
{
public:
	static inline const FName TuningTabName = TEXT("ACFCameraTuning");

	virtual void StartupModule() override
	{
		if (!FSlateApplication::IsInitialized())
		{
			return; // commandlets: no UI
		}
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TuningTabName, FOnSpawnTab::CreateStatic(&FActionCameraFrameworkEditorModule::SpawnTuningTab))
			.SetDisplayName(LOCTEXT("TuningTab", "ACF Camera Tuning"))
			.SetTooltipText(LOCTEXT("TuningTabTip", "Tune the Action Camera Framework camera of a PIE player (the preset assets are not changed)."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory());
		bRegistered = true;
	}

	virtual void ShutdownModule() override
	{
		if (bRegistered && FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TuningTabName);
		}
	}

private:
	static TSharedRef<SDockTab> SpawnTuningTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SACFCameraTuningPanel)];
	}

	bool bRegistered = false;
};

IMPLEMENT_MODULE(FActionCameraFrameworkEditorModule, ActionCameraFrameworkEditor)

#undef LOCTEXT_NAMESPACE
