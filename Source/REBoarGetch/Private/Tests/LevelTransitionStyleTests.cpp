#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ACFTransitionTypes.h"
#include "Stage/BoarLevelTransitionLibrary.h"
#include "Stage/HubPortal.h"
#include "Blueprint/UserWidget.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelTransitionStyleTest, "REBoarGetch.Spec.LevelTransitionStyle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelTransitionStyleTest::RunTest(const FString& Parameters)
{
	// 種類ごとに、Project Settingsの対応する項目を読む（値はiniで変わるので、対応だけを確かめる）。
	UBoarLevelTransitionSettings* Settings = NewObject<UBoarLevelTransitionSettings>(GetTransientPackage());
	Settings->Portal.Effect = EACFTransitionEffect::BlackFade;
	Settings->Menu.Effect = EACFTransitionEffect::DigitalGlitch;
	Settings->StageDeparture.Effect = EACFTransitionEffect::WireframeConversion;
	Settings->ReturnToLobby.Effect = EACFTransitionEffect::HologramTransfer;
	TestEqual(TEXT("Portal entry"), static_cast<int32>(Settings->GetEntry(EBoarLevelTransitionStyle::Portal).Effect), static_cast<int32>(EACFTransitionEffect::BlackFade));
	TestEqual(TEXT("Menu entry"), static_cast<int32>(Settings->GetEntry(EBoarLevelTransitionStyle::Menu).Effect), static_cast<int32>(EACFTransitionEffect::DigitalGlitch));
	TestEqual(TEXT("Stage departure entry"), static_cast<int32>(Settings->GetEntry(EBoarLevelTransitionStyle::StageDeparture).Effect), static_cast<int32>(EACFTransitionEffect::WireframeConversion));
	TestEqual(TEXT("Return to lobby entry"), static_cast<int32>(Settings->GetEntry(EBoarLevelTransitionStyle::ReturnToLobby).Effect), static_cast<int32>(EACFTransitionEffect::HologramTransfer));
	TestEqual(TEXT("Runtime lookup reads the project settings"), static_cast<int32>(UBoarLevelTransitionLibrary::GetTransitionEffect(EBoarLevelTransitionStyle::ReturnToLobby)),
	          static_cast<int32>(GetDefault<UBoarLevelTransitionSettings>()->ReturnToLobby.Effect));

	// ポータルは既定でProject SettingsのPortalを使い、個別に上書きするときの初期値は12。
	const AHubPortal* Portal = GetDefault<AHubPortal>();
	TestFalse(TEXT("Portal uses the project setting by default"), Portal->bOverrideTransition);
	TestEqual(TEXT("Portal override starts at ACF 12"), static_cast<int32>(Portal->Transition.Effect), 12);

	// タイトルのStartは、演出つきの関数でL_Lobbyを開く（素のOpenLevelは残さない）。
	const UBlueprint* Title = LoadObject<UBlueprint>(nullptr, TEXT("/Game/BP/Widget/WBP_Title.WBP_Title"));
	if (!TestNotNull(TEXT("WBP_Title loads"), Title))
		return false;
	int32 TransitionCalls = 0;
	for (const UEdGraph* Graph : Title->UbergraphPages)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			if (!Call)
				continue;
			const FName Name = Call->FunctionReference.GetMemberName();
			TestFalse(TEXT("No plain OpenLevel in WBP_Title"), Name == TEXT("OpenLevel") || Name == TEXT("OpenLevelBySoftObjectPtr"));
			if (Name != GET_FUNCTION_NAME_CHECKED(UBoarLevelTransitionLibrary, OpenLevelWithTransition))
				continue;
			++TransitionCalls;
			const UEdGraphPin* Exec = Call->GetExecPin();
			TestTrue(TEXT("Transition call is wired to Start"), Exec && Exec->LinkedTo.Num() > 0);
			const UEdGraphPin* Level = Call->FindPin(TEXT("LevelName"));
			TestTrue(TEXT("Transition opens L_Lobby"), Level && Level->DefaultValue.Contains(TEXT("L_Lobby")));
			const UEdGraphPin* Style = Call->FindPin(TEXT("Style"));
			TestTrue(TEXT("Title uses the Menu style"), Style && (Style->DefaultValue.IsEmpty() || Style->DefaultValue == TEXT("Menu")));
		}
	}
	TestEqual(TEXT("WBP_Title has one transition call"), TransitionCalls, 1);
	return true;
}
#endif
