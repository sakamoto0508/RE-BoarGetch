#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "GameMapsSettings.h"
#include "Player/BoarPlayerController.h"
#include "UI/BoarGameOverWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarStageEndErrorHandlingTest, "REBoarGetch.Spec.StageEndErrorHandling",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoarStageEndErrorHandlingTest::RunTest(const FString& Parameters)
{
	// ボタン名が未設定のGameOver Widgetは、RetryとLobbyの両方を不足として報告する。
	const auto* Widget = NewObject<UBoarGameOverWidget>();
	const TArray<FString> Missing = Widget->GetMissingRequiredWidgets();
	TestEqual(TEXT("不足ボタンはRetry/Lobbyの2件"), Missing.Num(), 2);
	TestTrue(TEXT("Retryの不足を報告"),
	         Missing.ContainsByPredicate([](const FString& Item) { return Item.Contains(TEXT("RetryButtonWidgetName")); }));
	TestTrue(TEXT("Lobbyの不足を報告"),
	         Missing.ContainsByPredicate([](const FString& Item) { return Item.Contains(TEXT("LobbyButtonWidgetName")); }));

	// 設定済みのLobbyLevelは従来どおりそのまま使う。
	auto* Controller = NewObject<ABoarPlayerController>();
	const FSoftObjectPath Explicit(TEXT("/Game/Level/L_GadgetLab.L_GadgetLab"));
	Controller->LobbyLevel = TSoftObjectPtr<UWorld>(Explicit);
	TestEqual(TEXT("設定済みLobbyLevelを優先"), Controller->ResolveLobbyLevel().ToSoftObjectPath(), Explicit);

	// 未設定時はGameDefaultMapだけを代替に使い、Errorは1回だけ出す。
	Controller->LobbyLevel.Reset();
	const FSoftObjectPath DefaultMap(UGameMapsSettings::GetGameDefaultMap());
	const bool bDefaultMapExists = DefaultMap.IsValid() && FPackageName::DoesPackageExist(DefaultMap.GetLongPackageName());
	AddExpectedError(TEXT("LobbyLevel is unset"), EAutomationExpectedErrorFlags::Contains, 1);
	const TSoftObjectPtr<UWorld> Fallback = Controller->ResolveLobbyLevel();
	if (bDefaultMapExists)
		TestEqual(TEXT("GameDefaultMapへ復帰"), Fallback.ToSoftObjectPath(), DefaultMap);
	else
		TestTrue(TEXT("有効な代替が無ければ空"), Fallback.IsNull());
	Controller->ResolveLobbyLevel();
	return true;
}
#endif
