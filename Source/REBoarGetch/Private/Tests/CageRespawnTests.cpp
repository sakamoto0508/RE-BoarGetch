#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Boar/BoarBase.h"
#include "Cage/Cage.h"
#include "Component/CaptureComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

// AKI-158：檻の破壊→全解放→設定時間後の自動復旧（HP全快・表示／Collision復帰・自動再収容なし）。
// Stage停止中（Clear／GameOver／開始前）は復旧待ちが進まないことも確認する。Timerは手動で進める。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCageRespawnTest, "REBoarGetch.Spec.CageRespawn",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCageRespawnTest::RunTest(const FString& Parameters)
{
	const auto Initialization =
		UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).
			CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num,
		&Initialization);
	if (!TestNotNull(TEXT("Cage test world"), World))
		return false;

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACage* Cage = World->SpawnActor<ACage>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	ABoarBase* Boar = World->SpawnActor<ABoarBase>(FVector(300, 0, 0), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Cage"), Cage) || !TestNotNull(TEXT("Boar"), Boar))
	{
		World->DestroyWorld(false);
		return false;
	}

	Cage->InitializeForStage();

	// 復旧時間はEditor設定（RespawnDelay）。テストでは5秒にする。
	if (FFloatProperty* Delay = FindFProperty<FFloatProperty>(ACage::StaticClass(), TEXT("RespawnDelay")))
		Delay->SetPropertyValue_InContainer(Cage, 5.f);

	Boar->BoarUniqueId = TEXT("CageRespawn_Boar");
	UCaptureComponent* Capture = Boar->FindComponentByClass<UCaptureComponent>();
	if (TestNotNull(TEXT("CaptureComponent"), Capture) && TestTrue(TEXT("捕獲状態"), Capture->Capture(nullptr)))
	{
		Cage->CollectBoar(Boar);
		TestEqual(TEXT("収容される"), Cage->GetCapturedBoarCount(), 1);
	}

	FTimerManager& Timers = World->GetTimerManager();
	// FTimerManager::Tickは同じGFrameCounterで2回目以降を無視するため、手動Tickごとにフレームを進める。
	const auto TickTimers = [&Timers](float DeltaSeconds)
	{
		++GFrameCounter;
		Timers.Tick(DeltaSeconds);
	};
	Cage->ApplyDamage(Cage->GetMaxHP());
	TestTrue(TEXT("HP0で破壊"), Cage->GetIsCageDestroyed());
	TestEqual(TEXT("破壊で収容数0"), Cage->GetCapturedBoarCount(), 0);
	TestFalse(TEXT("収容個体は解放される"), Boar->IsCaptured());
	TArray<FName> Housed;
	Cage->AppendHousedBoarUniqueIds(Housed);
	TestTrue(TEXT("AKI-45のスナップショットに解放個体が残らない"), Housed.IsEmpty());
	TestTrue(TEXT("破壊中は非表示"), Cage->IsHidden());
	TestFalse(TEXT("破壊中はCollision無効"), Cage->GetActorEnableCollision());

	// Stage停止中（Clear／GameOver）は復旧待ちが進まない。
	Cage->SetStageStopped(true);
	TickTimers(10.f);
	TestTrue(TEXT("Stage停止中は復旧しない"), Cage->GetIsCageDestroyed());
	Cage->SetStageStopped(false);
	TickTimers(4.f);
	TestTrue(TEXT("設定時間前は復旧しない"), Cage->GetIsCageDestroyed());
	TickTimers(1.5f);
	TestFalse(TEXT("設定時間後に復旧"), Cage->GetIsCageDestroyed());
	TestEqual(TEXT("HP全快"), Cage->GetHP(), Cage->GetMaxHP());
	TestFalse(TEXT("表示が戻る"), Cage->IsHidden());
	TestTrue(TEXT("Collisionが戻る"), Cage->GetActorEnableCollision());
	TestEqual(TEXT("解放済み個体を自動再収容しない"), Cage->GetCapturedBoarCount(), 0);

	// 復旧後は通常収容へ戻る。
	if (Capture && Capture->Capture(nullptr))
	{
		Cage->CollectBoar(Boar);
		TestEqual(TEXT("復旧後は再び収容できる"), Cage->GetCapturedBoarCount(), 1);
	}

	World->DestroyWorld(false);
	return true;
}
#endif
