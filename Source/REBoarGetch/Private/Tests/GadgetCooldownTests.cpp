#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Component/GadgetComponent.h"
#include "Core/BoarInitializationContext.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Gadget/GadgetBase.h"
#include "Player/BoarPlayerCharacter.h"
#include "TimerManager.h"

// AKI-154：Gadget Actorはスロット切替・装備変更のたびに作り直される。CooldownがActorと一緒に消えず、
// GadgetId単位でUGadgetComponentに残ること、Stage停止中は進まないことを確認する。Timerは手動で進める。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarGadgetCooldownTest, "REBoarGetch.Spec.GadgetCooldown",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoarGadgetCooldownTest::RunTest(const FString& Parameters)
{
	const auto Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("検証World"), World))
		return false;
	// 装備切替でガジェットをDestroyするため、World Contextを登録しておく。
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const auto PlayerClass = LoadClass<ABoarPlayerCharacter>(
		nullptr, TEXT("/Game/BP/Player/BP_PlayerCharacter.BP_PlayerCharacter_C"));
	const auto NetClass = LoadClass<AGadgetBase>(nullptr, TEXT("/Game/BP/Gadget/BP_NetGadget.BP_NetGadget_C"));
	if (!TestNotNull(TEXT("既存Player BP"), PlayerClass) || !TestNotNull(TEXT("既存Net BP"), NetClass))
		return false;

	auto* Player = World->SpawnActor<ABoarPlayerCharacter>(PlayerClass, FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Player"), Player))
		return false;

	FBoarGadgetLoadoutContext Context;
	Context.bTemporary = true;
	Context.TestGadgetClass = NetClass;
	if (!TestTrue(TEXT("Netを保存なしで装備"), Player->InitializeForPlay(Context)))
		return false;

	UGadgetComponent* Gadgets = Player->GetGadgetComponent();
	FTimerManager& Timers = World->GetTimerManager();
	// FTimerManager::Tickは同じGFrameCounterで2回目以降を無視するため、手動Tickごとにフレームを進める。
	auto TickTimers = [&](float DeltaSeconds)
	{
		++GFrameCounter;
		Timers.Tick(DeltaSeconds);
	};

	const FFloatProperty* CooldownProp = FindFProperty<FFloatProperty>(AGadgetBase::StaticClass(), TEXT("CooldownSeconds"));
	if (!TestNotNull(TEXT("CooldownSeconds設定項目"), CooldownProp))
		return false;
	const float NetCooldown = CooldownProp->GetPropertyValue_InContainer(NetClass->GetDefaultObject());
	if (!TestTrue(TEXT("Net BPのCooldownは正の値"), NetCooldown > 0.f))
		return false;

	auto CurrentGadget = [&] { return Gadgets->GetCurrentGadget(); };

	// まだTickしていないフレームで設定したTimerは保留扱いになり、次のTickでは有効化されるだけで進まない。
	// 最初に一度Tickしておき、以降に設定するTimerがすぐ有効になるようにする（実際のゲームでは毎フレームTickされる）。
	TickTimers(0.f);

	// 1) 使用でCooldownが始まる。
	AGadgetBase* Net = CurrentGadget();
	if (!TestNotNull(TEXT("装備中のNet"), Net) || !TestTrue(TEXT("使用前は使える"), Net->CanUse()))
		return false;
	Net->Use(Player);
	TestFalse(TEXT("使用でCooldown開始"), Net->CanUse());

	// 2) 同じガジェットを装備し直しても（Actorは作り直される）Cooldownは残る。不具合の本体。
	Gadgets->SwitchGadget(NetClass);
	AGadgetBase* ReequippedNet = CurrentGadget();
	TestTrue(TEXT("装備し直すとActorは別物になる"), ReequippedNet && ReequippedNet != Net);
	TestFalse(TEXT("装備し直してもCooldownは残る"), ReequippedNet && ReequippedNet->CanUse());

	// 3) 装備を外して（Actorを破棄して）から付け直してもCooldownは残る。
	Gadgets->UnequipGadget();
	TestNull(TEXT("装備を外す"), CurrentGadget());
	Gadgets->SwitchGadget(NetClass);
	TestFalse(TEXT("外してから付け直してもCooldownは残る"), CurrentGadget() && CurrentGadget()->CanUse());

	// 4) 別のガジェット（GadgetIdが違う）のCooldownは独立している。同じComponentを持つPlayerを所有者にする。
	FActorSpawnParameters OwnedSpawn = Spawn;
	OwnedSpawn.Owner = Player;
	AGadgetBase* Other = World->SpawnActor<AGadgetBase>(AGadgetBase::StaticClass(), FTransform::Identity, OwnedSpawn);
	if (TestNotNull(TEXT("別ガジェット"), Other))
	{
		TestTrue(TEXT("別ガジェットはNetのCooldownの影響を受けない"), Other->CanUse());
		Other->Use(Player);
		TestFalse(TEXT("別ガジェットも自分のCooldownを持つ"), Other->CanUse());
		Other->Destroy();
	}

	// 5) Stage停止中（Clear・GameOver）はCooldownが進まない。再開すれば残りの時間で終わる。
	Gadgets->SetCooldownsStopped(true);
	TickTimers(NetCooldown + 10.f);
	TestFalse(TEXT("Stage停止中はCooldownが進まない"), CurrentGadget()->CanUse());
	Gadgets->SetCooldownsStopped(false);
	TickTimers(NetCooldown + .1f);
	TestTrue(TEXT("再開後、設定時間が過ぎれば使える"), CurrentGadget()->CanUse());

	// 6) Stage停止中に始まったCooldownも止まった状態で始まる。
	Gadgets->SetCooldownsStopped(true);
	CurrentGadget()->Use(Player);
	TickTimers(NetCooldown + 10.f);
	TestFalse(TEXT("停止中に始めたCooldownも進まない"), CurrentGadget()->CanUse());
	Gadgets->SetCooldownsStopped(false);
	TickTimers(NetCooldown + .1f);
	TestTrue(TEXT("停止解除後に終わる"), CurrentGadget()->CanUse());

	// 7) 0秒以下のCooldownは始まらない。
	Gadgets->StartGadgetCooldown(CurrentGadget(), 0.f);
	TestTrue(TEXT("0秒のCooldownは始まらない"), CurrentGadget()->CanUse());

	return true;
}
#endif
