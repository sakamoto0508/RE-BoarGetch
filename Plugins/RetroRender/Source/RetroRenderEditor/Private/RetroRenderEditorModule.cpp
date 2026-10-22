#include "Modules/ModuleManager.h"
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "RetroLevelMaterials.h"
#include "Rendering/RetroRenderSettings.h"
#include "Widgets/Notifications/SNotificationList.h"
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
	FDelegateHandle MapOpenedHandle;
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

		MapOpenedHandle = FEditorDelegates::OnMapOpened.AddRaw(this, &FRetroRenderEditorModule::OnMapOpened);

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			FToolMenuOwnerScoped Owner(RetroTabName);
			UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
			FToolMenuSection& Section = Menu->FindOrAddSection("RetroRender", LOCTEXT("Section", "Retro Render"));
			Section.AddMenuEntry("OpenRetroRender", LOCTEXT("Open", "Retro Render"), LOCTEXT("OpenTip", "Open the Retro Render panel"),
				FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(RetroTabName); })));
		}));
	}

	void OnMapOpened(const FString& Filename, bool bAsTemplate)
	{
		if (!GetDefault<URetroRenderSettings>()->bNotifyUnpatchedMaterials || !GEditor || IsRunningCommandlet() || FApp::IsUnattended()) return;
		const TArray<FString> Unpatched = RetroLevelMaterials::FindUnpatched(GEditor->GetEditorWorldContext().World());
		if (Unpatched.IsEmpty()) return;
		FString List;
		for (int32 i = 0; i < FMath::Min(Unpatched.Num(), 6); ++i) List += TEXT("\n  ") + FPaths::GetBaseFilename(Unpatched[i]);
		if (Unpatched.Num() > 6) List += FString::Printf(TEXT("\n  ... +%d"), Unpatched.Num() - 6);
		FNotificationInfo Info(FText::Format(LOCTEXT("Unpatched", "Retro Render: {0} material(s) in this level have no vertex-snap patch (post-process still applies).{1}"),
			Unpatched.Num(), FText::FromString(List)));
		Info.ExpireDuration = 10.f;
		Info.bFireAndForget = true;
		Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("OpenPanel", "Open Retro Render"), FText(),
			FSimpleDelegate::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(RetroTabName); }), SNotificationItem::CS_None));
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	virtual void ShutdownModule() override
	{
		FEditorDelegates::OnMapOpened.Remove(MapOpenedHandle);
		if (UToolMenus::IsToolMenuUIEnabled()) UToolMenus::UnregisterOwner(RetroTabName);
		if (FSlateApplication::IsInitialized()) FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(RetroTabName);
	}
};

IMPLEMENT_MODULE(FRetroRenderEditorModule, RetroRenderEditor)

#undef LOCTEXT_NAMESPACE
