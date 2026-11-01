#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
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

	// 復帰先の判定はActorを作らずに検証する。DefaultMapは実在する既存Mapを入力として渡す（設定は書き換えない）。
	const FSoftObjectPath Explicit(TEXT("/Game/Level/L_GadgetLab.L_GadgetLab"));
	const FSoftObjectPath ExistingMap(TEXT("/Game/Level/L_Lobby.L_Lobby"));
	const FSoftObjectPath MissingMap(TEXT("/Game/Level/L_DoesNotExist.L_DoesNotExist"));
	TestEqual(TEXT("設定済みLobbyLevelを優先"),
	          ABoarPlayerController::ResolveLobbyLevel(TSoftObjectPtr<UWorld>(Explicit), ExistingMap).ToSoftObjectPath(),
	          Explicit);
	TestEqual(TEXT("未設定なら実在するDefaultMapを使う"),
	          ABoarPlayerController::ResolveLobbyLevel(TSoftObjectPtr<UWorld>(), ExistingMap).ToSoftObjectPath(), ExistingMap);
	TestTrue(TEXT("存在しないDefaultMapは使わない"),
	         ABoarPlayerController::ResolveLobbyLevel(TSoftObjectPtr<UWorld>(), MissingMap).IsNull());
	TestTrue(TEXT("DefaultMapも未設定なら空"),
	         ABoarPlayerController::ResolveLobbyLevel(TSoftObjectPtr<UWorld>(), FSoftObjectPath()).IsNull());

	// LobbyLevel未設定の報告はController単位で1回だけ。Actorはテスト用Worldへ正規にSpawnし、Level遷移は呼ばない。
	const auto Initialization =
		UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).
		                               CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num,
	                                    &Initialization);
	if (!TestNotNull(TEXT("Lobby test world"), World))
		return false;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (const auto* Controller = World->SpawnActor<ABoarPlayerController>(Params);
		TestNotNull(TEXT("LobbyLevel未設定のController"), Controller))
	{
		const FSoftObjectPath DefaultMap(UGameMapsSettings::GetGameDefaultMap());
		AddExpectedError(TEXT("LobbyLevel is unset"), EAutomationExpectedErrorFlags::Contains, 1);
		TestEqual(TEXT("Controllerは判定関数と同じ復帰先を返す"), Controller->GetLobbyLevel().ToSoftObjectPath(),
		          ABoarPlayerController::ResolveLobbyLevel(TSoftObjectPtr<UWorld>(), DefaultMap).ToSoftObjectPath());
		Controller->GetLobbyLevel();
	}
	World->DestroyWorld(false);
	return true;
}
#endif
