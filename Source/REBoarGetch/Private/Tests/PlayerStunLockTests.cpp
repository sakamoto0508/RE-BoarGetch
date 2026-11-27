#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Animation/AnimInstance.h"
#include "Boar/BoarBase.h"
#include "Component/GadgetComponent.h"
#include "Component/HealthComponent.h"
#include "Component/PlayerHitReactionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Gadget/NetGadget.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/BoarPlayerCharacter.h"
#include "Core/BoarInitializationContext.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"

// 赤ボア（突進攻撃）被弾時のStun操作ロック・復帰・演出通知の回帰テスト。
// 演出の見た目はPIEで確認する。ここではGameplayの状態とEvent（演出が受け取る結果）だけを検証する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarPlayerStunLockTest, "REBoarGetch.Spec.PlayerStunLock",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoarPlayerStunLockTest::RunTest(const FString& Parameters)
{
	const auto Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).
		CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("検証World"), World))
		return false;

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

	// Boarはプレイヤーの+X側（前方）から突進してくる。
	auto* Boar = World->SpawnActor<ABoarBase>(FVector(200, 0, 100), FRotator(0, 180, 0), Spawn);
	if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("Boar"), Boar))
		return false;

	Boar->InitializeForStage();

	FBoarGadgetLoadoutContext Context;
	Context.bTemporary = true;
	Context.TestGadgetClass = NetClass;

	if (!TestTrue(TEXT("Netを保存なしで装備"), Player->InitializeForPlay(Context)))
		return false;
	// GameModeのないWorldなので、準備済みのPlayerだけBeginPlayを通す（演出ComponentのEvent購読・HP初期化）。
	Player->DispatchBeginPlay();

	UCharacterMovementComponent* Move = Player->GetCharacterMovement();
	UHealthComponent* Health = Player->GetHealthComponent();
	UPlayerHitReactionComponent* Reaction = Player->GetHitReactionComponent();
	if (!TestNotNull(TEXT("被弾演出Component"), Reaction))
		return false;

	FTimerManager& Timers = World->GetTimerManager();
	auto Advance = [&Timers](float Seconds)
	{
		// TimerManagerだけを進める（CharacterMovementは動かさない）。
		// FTimerManager::Tickは同じフレーム番号では1回しか進まないため、1ステップごとにフレームを進める。
		const int32 Steps = FMath::CeilToInt(Seconds / 0.05f);
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			++GFrameCounter;
			Timers.Tick(0.05f);
		}
	};
	auto ResetGround = [&]()
	{
		Move->SetMovementMode(MOVE_Walking);
		Move->Velocity = FVector::ZeroVector;
		Player->SetActorRotation(FRotator::ZeroRotator);
		Player->bIsInvincible = false;
		Timers.ClearTimer(Player->InvincibleTimerHandle);

		// 各ケースを独立させる（累積ダメージでダウンしない）。
		Health->ResetHealth();
		Boar->BeginChargeHitTracking();
	};
	const float StunDuration = Player->StunDuration;
	TestTrue(TEXT("既定のStunはしりもち・起き上がりを見せられる長さ"), StunDuration >= 1.0f);

	// 1) 攻撃中でないBoarとの接触：ダメージ・Stun・演出のいずれも発生しない。
	ResetGround();
	Boar->EndChargeHitTracking();
	const float HealthStart = Health->GetCurrentHealth();
	Player->HandleBoarContact(Boar);
	TestFalse(TEXT("無効な接触ではStunしない"), Player->IsStunned());
	TestEqual(TEXT("無効な接触では被弾演出を再生しない"), Reaction->GetImpactPresentationCount(), 0);

	// 2) 地上で突進が命中：ダメージ・Stun・演出通知・ノックバック・攻撃方向への向き。
	ResetGround();
	Player->HandleBoarContact(Boar);
	TestTrue(TEXT("有効な命中でStun"), Player->IsStunned());
	TestEqual(TEXT("HPが1減る"), Health->GetCurrentHealth(), HealthStart - Player->ContactDamage);
	TestTrue(TEXT("行動状態はStun"), Player->GetPlayerActionState() == EPlayerActionState::Stun);
	TestEqual(TEXT("命中インパクト演出は1回"), Reaction->GetImpactPresentationCount(), 1);
	TestTrue(TEXT("頭上のStun表示を開始"), Reaction->IsStunPresentationActive());
	TestFalse(TEXT("地上被弾は着地待ちにならない"), Player->bStunAwaitingLanding);
	TestTrue(TEXT("地上被弾はStun時間の計測を開始"), Timers.IsTimerActive(Player->StunTimerHandle));
	TestTrue(TEXT("攻撃と反対方向（-X）へ押し出す"), Move->Velocity.X < -1.0 && FMath::Abs(Move->Velocity.Y) < 1.0);
	TestTrue(TEXT("攻撃してきた方向（+X）を向く"), Player->GetActorForwardVector().X > 0.99);
	TestFalse(TEXT("Stun中は速度方向へ振り向かない"), Move->bOrientRotationToMovement);
	TestTrue(TEXT("Hit Flashを開始"), Reaction->IsHitFlashActive());

	if (UAnimInstance* Anim = Player->GetMesh() ? Player->GetMesh()->GetAnimInstance() : nullptr)
		TestTrue(TEXT("しりもちAnimationを再生"), Reaction->IsKnockdownAnimationPlaying());
	else
		AddInfo(TEXT("AnimInstanceなし: しりもちAnimationの再生確認を省略"));

	// 3) Stun中は移動・ダッシュ・ジャンプ・ガジェット・Slot切替を受け付けない。
	Player->ConsumeMovementInputVector();
	Player->Move(FVector2D(0.0, 1.0));
	TestTrue(TEXT("Stun中の移動入力は積まれない"), Player->GetPendingMovementInputVector().IsNearlyZero());
	Player->StartDash();
	TestFalse(TEXT("Stun中はダッシュできない"), Player->IsDashing());
	Player->StartJump();
	TestFalse(TEXT("Stun中はジャンプできない"), Player->bPressedJump);
	Player->StartGadgetUse();
	TestFalse(TEXT("Stun中はガジェットを使えない"), Player->IsGadgetInUse());
	TestFalse(TEXT("Stun中は移動系の行動が全てロック"), Player->CanUseMovementActions());
	TestFalse(TEXT("Stun中はSlot切替もロック"), Player->CanSwitchGadgetSlot());

	// 4) 無敵中・Stun中の再接触：ダメージも演出も重ねない。
	const float HealthAfterHit = Health->GetCurrentHealth();
	Player->HandleBoarContact(Boar);
	TestEqual(TEXT("Stun中の再接触でHPは減らない"), Health->GetCurrentHealth(), HealthAfterHit);
	TestEqual(TEXT("Stun中の再接触で被弾演出を再生しない"), Reaction->GetImpactPresentationCount(), 1);

	// 5) 時間が残っている間はStunを維持し、満了で通常操作へ戻る。
	Advance(StunDuration * 0.5f);
	TestTrue(TEXT("途中ではStunを維持"), Player->IsStunned());
	Advance(StunDuration * 0.5f + 0.1f);
	TestFalse(TEXT("Stun時間満了で解除"), Player->IsStunned());
	TestFalse(TEXT("解除で頭上のStun表示を終える"), Reaction->IsStunPresentationActive());
	TestTrue(TEXT("解除後は通常の行動状態"), Player->GetPlayerActionState() == EPlayerActionState::Idle);
	TestTrue(TEXT("向きの自動追従を元に戻す"), Move->bOrientRotationToMovement);
	TestTrue(TEXT("解除後は移動系の行動が可能"), Player->CanUseMovementActions());
	TestTrue(TEXT("起き上がり直後は短い無敵"), Player->bIsInvincible);
	Player->StartDash();
	TestTrue(TEXT("解除後はダッシュできる"), Player->IsDashing());
	Player->StopDash();
	Player->StartGadgetUse();
	TestTrue(TEXT("解除後はガジェットを使える"), Player->IsGadgetInUse());
	Player->InterruptGadgetUse();
	Advance(Player->PostStunInvincibleDuration + 0.1f);
	TestFalse(TEXT("無敵は時間で切れる"), Player->bIsInvincible);

	// 6) 連続被弾（再Stun）：前回の状態を引きずらず、同じ流れで復帰する。
	ResetGround();
	Player->HandleBoarContact(Boar);
	TestTrue(TEXT("2回目の命中でもStun"), Player->IsStunned());
	TestEqual(TEXT("2回目の命中演出"), Reaction->GetImpactPresentationCount(), 2);
	Player->BeginStun();
	TestTrue(TEXT("Stun中の再Stunでも状態は一つ"), Player->IsStunned() && Player->GetPlayerActionState() == EPlayerActionState::Stun);
	Advance(StunDuration + 0.1f);
	TestFalse(TEXT("再Stun後も時間で解除"), Player->IsStunned());
	TestTrue(TEXT("再Stunでも向きの自動追従を正しく戻す"), Move->bOrientRotationToMovement);

	// 7) 空中被弾：空中で静止させず、着地してからStun時間を数える。
	ResetGround();
	Move->SetMovementMode(MOVE_Falling);
	Move->Velocity = FVector(0, 0, -300);
	Player->HandleBoarContact(Boar);
	TestTrue(TEXT("空中被弾でStun"), Player->IsStunned());
	TestTrue(TEXT("空中では移動モードを止めない（落下を続ける）"), Move->MovementMode == MOVE_Falling);
	TestTrue(TEXT("空中でも攻撃方向へ流す"), Move->Velocity.X < -1.0);
	TestEqual(TEXT("落下速度は維持"), Move->Velocity.Z, -300.0);
	TestTrue(TEXT("着地待ち"), Player->bStunAwaitingLanding);
	TestFalse(TEXT("着地前はStun時間を数えない"), Timers.IsTimerActive(Player->StunTimerHandle));
	Advance(0.3f);
	TestTrue(TEXT("着地前はStunを維持"), Player->IsStunned());
	Move->SetMovementMode(MOVE_Walking);
	Player->Landed(FHitResult());
	TestFalse(TEXT("着地で着地待ちを終える"), Player->bStunAwaitingLanding);
	TestTrue(TEXT("着地でしりもちの時間を数え始める"), Timers.IsTimerActive(Player->StunTimerHandle));
	TestTrue(TEXT("着地で横滑りを止める"), Move->Velocity.IsNearlyZero());
	TestTrue(TEXT("着地後もStun状態"), Player->GetPlayerActionState() == EPlayerActionState::Stun);
	Advance(StunDuration + 0.1f);
	TestFalse(TEXT("空中被弾も着地後の時間で解除"), Player->IsStunned());

	// 8) 空中被弾のまま着地しない（高所・落下継続）：上限時間で計測を始め、操作不能が残り続けない。
	ResetGround();
	Move->SetMovementMode(MOVE_Falling);
	Player->HandleBoarContact(Boar);
	Advance(Player->StunAirborneMaxWait + StunDuration + 0.2f);
	TestFalse(TEXT("着地しなくても上限後に解除"), Player->IsStunned());
	Move->SetMovementMode(MOVE_Walking);

	// 9) 他の所有者（Stage停止）による移動停止は、Stun解除で解除しない。
	ResetGround();
	Player->HandleBoarContact(Boar);
	Move->DisableMovement();
	Advance(StunDuration + 0.1f);
	TestFalse(TEXT("Stun解除"), Player->IsStunned());
	TestTrue(TEXT("Stage停止の移動停止はStun解除で戻さない"), Move->MovementMode == MOVE_None);
	Move->SetMovementMode(MOVE_Walking);

	// 10) Stun中にダウン（HP0）：Stunを中断し、演出の終了通知も届く。時間満了で通常状態へ戻らない。
	ResetGround();
	Player->HandleBoarContact(Boar);
	TestTrue(TEXT("ダウン検証のStun"), Player->IsStunned() && Reaction->IsStunPresentationActive());
	Player->EnterDowned();
	TestFalse(TEXT("ダウンでStunを中断"), Player->IsStunned());
	TestFalse(TEXT("ダウンで頭上のStun表示を消す"), Reaction->IsStunPresentationActive());
	TestFalse(TEXT("ダウンでStunのTimerを残さない"), Timers.IsTimerActive(Player->StunTimerHandle));
	Advance(StunDuration + 0.1f);
	TestTrue(TEXT("ダウン後は通常状態へ戻らない"), Player->GetPlayerActionState() == EPlayerActionState::Down);
	const int32 ImpactsBeforeDowned = Reaction->GetImpactPresentationCount();
	Player->bIsInvincible = false;
	Player->HandleBoarContact(Boar);
	TestEqual(TEXT("ダウン後の接触で被弾演出を再生しない"), Reaction->GetImpactPresentationCount(), ImpactsBeforeDowned);

	// 11) Stun中のActor破棄（Retry・Level遷移）：Timerと状態を残さない。
	auto* Player2 = World->SpawnActor<ABoarPlayerCharacter>(PlayerClass, FVector(0, 300, 100), FRotator::ZeroRotator, Spawn);
	if (TestNotNull(TEXT("破棄検証Player"), Player2))
	{
		TestTrue(TEXT("破棄検証の装備"), Player2->InitializeForPlay(Context));
		Player2->DispatchBeginPlay();
		Player2->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Player2->bIsInvincible = false;
		Player2->HandleBoarContact(Boar);
		TestTrue(TEXT("破棄検証のStun"), Player2->IsStunned());
		const FTimerHandle StunHandle = Player2->StunTimerHandle;

		// Retry・Level遷移と同じEndPlay（このテスト用Worldはアクター初期化を経ないためRouteEndPlay／Destroyが素通りする。EndPlayを直接通す）。
		UPlayerHitReactionComponent* Reaction2 = Player2->GetHitReactionComponent();
		Player2->EndPlay(EEndPlayReason::LevelTransition);
		TestFalse(TEXT("EndPlayでStunを終える"), Player2->bIsStunned);
		TestFalse(TEXT("EndPlayで頭上のStun表示を残さない"), Reaction2 && Reaction2->IsStunPresentationActive());
		TestFalse(TEXT("EndPlayでHit Flash（Overlay）を残さない"), Reaction2 && Reaction2->IsHitFlashActive());
		Player2->Destroy();
		Advance(0.05f);
		TestFalse(TEXT("破棄でStunのTimerを残さない"), Timers.TimerExists(StunHandle));
	}

	Boar->EndChargeHitTracking();
	return true;
}
#endif
