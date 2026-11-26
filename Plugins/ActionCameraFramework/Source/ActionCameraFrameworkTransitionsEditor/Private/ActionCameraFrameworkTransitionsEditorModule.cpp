#include "ACFTransitionCatalog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ICookInfo.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "SACFTransitionPreviewPanel.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "ActionCameraFrameworkTransitionsEditor"

/**
 * Phase 7D editor module (README 33 章「Editor Preview」): the "ACF Transition Preview" tab, next to the other ACF tools
 * (Window > Developer Tools > Debug, like "ACF Camera Tuning"), and console command ACF.Transition.OpenPreview.
 *
 * Cooking: the transition material and the standard presets are only referenced by soft paths, so the cooker would not
 * find them. This module adds every package under /ActionCameraFramework/Transitions (except Samples) to every
 * cook-by-the-book (UE::Cook::FDelegates::ModifyCook), so a project only has to enable the plugin. The sample maps
 * are added too when the cook command line has -ACFCookTransitionSamples (BuildCookRun:
 * -additionalcookeroptions=-ACFCookTransitionSamples).
 */
class FActionCameraFrameworkTransitionsEditorModule : public IModuleInterface
{
public:
	static inline const FName TabName = TEXT("ACFTransitionPreview");

	virtual void StartupModule() override
	{
		ModifyCookHandle = UE::Cook::FDelegates::ModifyCook.AddStatic(&FActionCameraFrameworkTransitionsEditorModule::AddTransitionContentToCook);
		if (!FSlateApplication::IsInitialized())
		{
			return; // commandlets
		}
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
			{
				return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SACFTransitionPreviewPanel)];
			}))
			.SetDisplayName(LOCTEXT("Tab", "ACF Transition Preview"))
			.SetTooltipText(LOCTEXT("TabTip", "Preview the Action Camera Framework screen transitions and tune their settings."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory());
		bRegistered = true;
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FActionCameraFrameworkTransitionsEditorModule::RegisterBlueprintToolbar));
	}

	virtual void ShutdownModule() override
	{
		UE::Cook::FDelegates::ModifyCook.Remove(ModifyCookHandle);
		if (bRegistered && FSlateApplication::IsInitialized())
		{
			UToolMenus::UnRegisterStartupCallback(this);
			UToolMenus::UnregisterOwner(this);
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
		}
	}

	static void OpenPreview()
	{
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->TryInvokeTab(TabName);
		}
	}

private:
	void RegisterBlueprintToolbar()
	{
		FToolMenuOwnerScoped Owner(this);
		UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("AssetEditor.BlueprintEditor.ToolBar");
		FToolMenuEntry Button = FToolMenuEntry::InitToolBarButton("ACFTransitionPreview",
			FUIAction(FExecuteAction::CreateStatic(&FActionCameraFrameworkTransitionsEditorModule::OpenPreview)),
			LOCTEXT("BlueprintPreview", "Transition Preview"),
			LOCTEXT("BlueprintPreviewTip", "Preview an ACF screen transition or preset while editing this Blueprint."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Play"));
		Toolbar->FindOrAddSection("ACF").AddEntry(Button);
	}

	static void AddTransitionContentToCook(UE::Cook::ICookInfo&, TArray<UE::Cook::FPackageCookRule>& Rules)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		Registry.ScanPathsSynchronous({ TEXT("/ActionCameraFramework/Transitions") }, false);
		TArray<FAssetData> Assets;
		Registry.GetAssetsByPath(TEXT("/ActionCameraFramework/Transitions"), Assets, true);
		const bool bSamples = FParse::Param(FCommandLine::Get(), TEXT("ACFCookTransitionSamples"));
		TSet<FName> Added;
		for (const FAssetData& Asset : Assets)
		{
			const bool bSample = Asset.PackagePath.ToString().StartsWith(TEXT("/ActionCameraFramework/Transitions/Samples"));
			if ((bSample && !bSamples) || Added.Contains(Asset.PackageName))
			{
				continue;
			}
			Added.Add(Asset.PackageName);
			UE::Cook::FPackageCookRule& Rule = Rules.AddDefaulted_GetRef();
			Rule.PackageName = Asset.PackageName;
			Rule.InstigatorName = TEXT("ActionCameraFrameworkTransitions");
			Rule.CookRule = UE::Cook::EPackageCookRule::AddToCook;
		}
		UE_LOG(LogACFTransition, Display, TEXT("ACF Transitions: %d packages added to the cook (sample maps %s)."), Added.Num(), bSamples ? TEXT("included") : TEXT("not included; -ACFCookTransitionSamples adds them"));
	}

	FDelegateHandle ModifyCookHandle;
	bool bRegistered = false;
};

static FAutoConsoleCommand GACFOpenTransitionPreview(TEXT("ACF.Transition.OpenPreview"), TEXT("Opens the ACF Transition Preview tab (editor)."), FConsoleCommandDelegate::CreateLambda([]()
{
	FActionCameraFrameworkTransitionsEditorModule::OpenPreview();
}));

IMPLEMENT_MODULE(FActionCameraFrameworkTransitionsEditorModule, ActionCameraFrameworkTransitionsEditor)

#undef LOCTEXT_NAMESPACE
