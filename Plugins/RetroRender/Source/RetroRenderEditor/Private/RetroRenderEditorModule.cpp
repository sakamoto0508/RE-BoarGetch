#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"
#include "SRetroRenderPanel.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "RetroRenderEditor"

static const FName RetroTabName("RetroRender");

class FRetroRenderEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(RetroTabName, FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
		{
			return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SRetroRenderPanel)];
		}))
		.SetDisplayName(LOCTEXT("TabTitle", "Retro Render"))
		.SetTooltipText(LOCTEXT("TabTip", "PS1 / N64 / PS2 / CRT look, texture and mesh retro tools"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			FToolMenuOwnerScoped Owner(RetroTabName);
			UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
			FToolMenuSection& Section = Menu->FindOrAddSection("RetroRender", LOCTEXT("Section", "Retro Render"));
			Section.AddMenuEntry("OpenRetroRender", LOCTEXT("Open", "Retro Render"), LOCTEXT("OpenTip", "Open the Retro Render panel"),
				FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(RetroTabName); })));
		}));
	}

	virtual void ShutdownModule() override
	{
		if (UToolMenus::IsToolMenuUIEnabled()) UToolMenus::UnregisterOwner(RetroTabName);
		if (FSlateApplication::IsInitialized()) FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(RetroTabName);
	}
};

IMPLEMENT_MODULE(FRetroRenderEditorModule, RetroRenderEditor)

#undef LOCTEXT_NAMESPACE
