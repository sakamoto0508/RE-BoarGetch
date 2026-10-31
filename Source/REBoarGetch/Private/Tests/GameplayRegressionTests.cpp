#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Boar/BoarBase.h"
#include "Boar/StateTree/BoarAttackPlayerTask.h"
#include "Component/GadgetComponent.h"
#include "Component/HealthComponent.h"
#include "Component/CaptureComponent.h"
#include "Gadget/NetGadget.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/BoarPlayerCharacter.h"
#include "Core/BoarInitializationContext.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarGameplayRegressionTest, "REBoarGetch.Spec.GameplayRegression",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoarGameplayRegressionTest::RunTest(const FString& Parameters)
{
	// PIE・実セーブを使わず、状態遷移と移動値の副作用だけを独立Worldで検証する。
	const auto Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
	                                                          .CreatePhysicsScene(false).CreateNavigation(false).
	                                                          CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
	                                    ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("回帰検証World"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Boar = World->SpawnActor<ABoarBase>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	const auto PlayerClass = LoadClass<ABoarPlayerCharacter>(
		nullptr, TEXT("/Game/BP/Player/BP_PlayerCharacter.BP_PlayerCharacter_C"));
	if (!TestNotNull(TEXT("既存Player BP"), PlayerClass)) return false;
	auto* Player = World->SpawnActor<ABoarPlayerCharacter>(PlayerClass, FVector(500, 0, 100), FRotator::ZeroRotator,
	                                                       Spawn);
	if (!TestNotNull(TEXT("Boar"), Boar) || !TestNotNull(TEXT("Player"), Player)) return false;
	Boar->InitializeForStage();
	Boar->PerceivedPlayer = Player;
	Boar->PerceivedPlayerDistance = 100.f;
	Boar->ActiveSettings.PlayerAttackCooldown = 10.f;
	Boar->ActiveSettings.AttackRetreatDistance = 0.f;
	Boar->ActiveSettings.AttackRecoveryDuration = .5f;
	Boar->StartPlayerAttackCooldown();

	// 対象が見え続ける後退完了でも、予兆へ直帰せず硬直→判断終了になること。
	FStateTreeBoarAttackPlayerTask Task;
	FBoarAttackPlayerTaskInstanceData Attack;
	Attack.NPC = Boar;
	Attack.Phase = EBoarChargeAttackPhase::Retreat;
	Attack.bCleanupCompleted = false;
	Attack.bChargeFinishedNotified = true;
	Attack.OriginalWalkSpeed = Boar->GetCharacterMovement()->MaxWalkSpeed;
	Task.TickAttack(Attack, .01f);
	TestTrue(TEXT("認識中でも必ず硬直へ進む"), Attack.Phase == EBoarChargeAttackPhase::Recovery);
	TestTrue(TEXT("硬直時間中は終了しない"), Task.TickAttack(Attack, .1f) == EStateTreeRunStatus::Running);
	TestTrue(TEXT("硬直後はStateTreeへ判断を返す"), Task.TickAttack(Attack, .5f) == EStateTreeRunStatus::Succeeded);
	TestFalse(TEXT("長いCooldown中は再攻撃不可"), Boar->CanStartPlayerAttack());
	Boar->NextPlayerAttackTime = 0.;
	TestTrue(TEXT("射程内かつCooldown終了なら再攻撃可能"), Boar->CanStartPlayerAttack());
	Boar->PerceivedPlayerDistance = Boar->GetPlayerAttackRange() + 1.f;
	TestFalse(TEXT("射程外では再攻撃不可"), Boar->CanStartPlayerAttack());

	FBoarGadgetLoadoutContext Context;
	Context.bTemporary = true;
	Context.TestGadgetClass = LoadClass<AGadgetBase>(nullptr, TEXT("/Game/BP/Gadget/BP_NetGadget.BP_NetGadget_C"));
	if (!TestTrue(TEXT("既存装備を保存なしで準備"), Player->InitializeForPlay(Context))) return false;
	auto* Net = Cast<ANetGadget>(Player->GetGadgetComponent()->GetCurrentGadget());
	if (!TestNotNull(TEXT("既存Net BP"), Net)) return false;
	auto* Move = Player->GetCharacterMovement();
	Move->SetMovementMode(MOVE_Falling);
	Move->Velocity = FVector(123, 45, 678);
	const FVector BeforeVelocity = Move->Velocity;
	const float BeforeGravity = Move->GravityScale;
	const float BeforeControl = Move->AirControl;
	Player->AirNetDropSwingAnimation = nullptr;
	TestTrue(TEXT("演出欠落でも空中の使用方式を維持"), Player->ShouldStartAirNetDrop(Net));
	Player->StartGadgetUse();
	TestTrue(TEXT("演出欠落の拒否で速度を消さない"), Move->Velocity.Equals(BeforeVelocity));
	TestEqual(TEXT("拒否で重力を変えない"), Move->GravityScale, BeforeGravity);
	TestTrue(TEXT("拒否でCooldownを消費しない"), Net->CanUse());
	TestFalse(TEXT("拒否で操作ロックを残さない"), Player->bIsGadgetInUse);

	// Swingの長さにかかわらず、Swing/Dropのどちらでも上限が判定窓と移動設定を復元する。
	for (EAirNetDropPhase Phase : {EAirNetDropPhase::Swing, EAirNetDropPhase::Drop})
	{
		Player->AirNetDropPhase = Phase;
		Player->bIsGadgetInUse = true;
		Player->bAirNetDropMovementOverridden = true;
		Player->SavedAirNetDropGravityScale = BeforeGravity;
		Player->SavedAirNetDropAirControl = BeforeControl;
		Player->AirNetDropMaxDiveDuration = .05f;
		Player->AirNetDropDiveElapsed = 0.f;
		Move->GravityScale = 0.f;
		Move->AirControl = 0.f;
		Move->Velocity.Z = -900.f;
		Net->BeginCaptureWindow();
		Player->TickAirNetDrop(.1f);
		TestTrue(
			TEXT("上限で急降下段階を抜ける"),
			Player->AirNetDropPhase != EAirNetDropPhase::Swing && Player->AirNetDropPhase != EAirNetDropPhase::Drop);
		TestEqual(TEXT("重力を復元"), Move->GravityScale, BeforeGravity);
		TestEqual(TEXT("空中操作を復元"), Move->AirControl, BeforeControl);
		TestEqual(TEXT("上限を超えたフレームで追加加速しない"), Move->Velocity.Z, -900.0);
		TestFalse(TEXT("Notify未到達でも判定窓を閉じる"), Net->IsCaptureWindowActive());
		Player->InterruptGadgetUse();
		TestFalse(TEXT("中断後は操作ロックを残さない"), Player->bIsGadgetInUse);
	}

	// 地上でもCooldown拒否をPlayerへ返し、演出・移動停止を開始しない。
	Move->SetMovementMode(MOVE_Walking);
	Move->Velocity = FVector(123, 45, 0);
	Net->Use(Player);
	TestFalse(TEXT("地上拒否検証のCooldownを開始"), Net->CanUse());
	TestFalse(TEXT("Componentは使用拒否を返す"), Player->GetGadgetComponent()->BeginUseCurrentGadget());
	Player->StartGadgetUse();
	TestFalse(TEXT("地上Cooldown中は操作ロックを開始しない"), Player->bIsGadgetInUse);
	TestTrue(TEXT("地上Cooldown中は移動速度を消さない"), Move->Velocity.Equals(FVector(123, 45, 0)));

	// 同じBoarでも捕獲中は接触無害、解放後は通常のダメージ対象へ戻る。
	const float HealthBeforeContact = Player->GetHealthComponent()->GetCurrentHealth();
	TestTrue(TEXT("接触検証Boarを捕獲"), Boar->CaptureComponent->Capture(nullptr));
	Player->HandleBoarContact(Boar);
	TestEqual(TEXT("捕獲中の接触ではHPを減らさない"), Player->GetHealthComponent()->GetCurrentHealth(), HealthBeforeContact);
	TestFalse(TEXT("捕獲中の接触ではStunを開始しない"), Player->bIsStunned);
	TestTrue(TEXT("接触検証Boarを解放"), Boar->CaptureComponent->Release());
	Player->HandleBoarContact(Boar);
	TestTrue(TEXT("解放後の接触はダメージを与える"), Player->GetHealthComponent()->GetCurrentHealth() < HealthBeforeContact);
	return true;
}
#endif
