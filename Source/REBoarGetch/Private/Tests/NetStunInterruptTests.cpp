#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotify_NetCaptureWindow.h"
#include "Animation/AnimNotifyQueue.h"
#include "Boar/BoarBase.h"
#include "Component/GadgetComponent.h"
#include "Component/HealthComponent.h"
#include "Gadget/NetGadget.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/BoarPlayerCharacter.h"
#include "Core/BoarInitializationContext.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

// AKI-146で見つかった「Stunで中断した後も地上アミのMontageが続き、Notify ONで捕獲窓が開き直る」不具合の回帰テスト。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarNetStunInterruptTest, "REBoarGetch.Spec.NetStunInterrupt",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoarNetStunInterruptTest::RunTest(const FString& Parameters)
{
	const auto Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
	                                                          .CreatePhysicsScene(false).CreateNavigation(false).
	                                                          CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
	                                    ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("検証World"), World)) return false;
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
	if (!TestNotNull(TEXT("既存Player BP"), PlayerClass) || !TestNotNull(TEXT("既存Net BP"), NetClass)) return false;
	auto* Player = World->SpawnActor<ABoarPlayerCharacter>(PlayerClass, FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
	auto* Boar = World->SpawnActor<ABoarBase>(FVector(1000, 0, 0), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("Boar"), Boar)) return false;
	Boar->InitializeForStage();

	FBoarGadgetLoadoutContext Context;
	Context.bTemporary = true;
	Context.TestGadgetClass = NetClass;
	if (!TestTrue(TEXT("Netを保存なしで装備"), Player->InitializeForPlay(Context))) return false;
	UGadgetComponent* Gadgets = Player->GetGadgetComponent();
	UCharacterMovementComponent* Move = Player->GetCharacterMovement();
	UAnimInstance* Anim = Player->GetMesh() ? Player->GetMesh()->GetAnimInstance() : nullptr;
	UAnimNotify_NetCaptureWindow* NotifyOn = NewObject<UAnimNotify_NetCaptureWindow>();
	UAnimNotify_NetCaptureWindow* NotifyOff = NewObject<UAnimNotify_NetCaptureWindow>();
	FBoolProperty* EnableProp = FindFProperty<FBoolProperty>(UAnimNotify_NetCaptureWindow::StaticClass(), TEXT("bEnableCaptureWindow"));
	if (!TestNotNull(TEXT("Notify設定項目"), EnableProp)) return false;
	EnableProp->SetPropertyValue_InContainer(NotifyOff, false);
	const FAnimNotifyEventReference NoEvent;
	auto FireNotify = [&](UAnimNotify_NetCaptureWindow* N) { N->Notify(Player->GetMesh(), nullptr, NoEvent); };
	// Cooldownはこの検証Worldでは進まないため、使用ごとに新しい網へ持ち替える（装備解除経路の検証も兼ねる）。
	auto FreshNet = [&]() -> ANetGadget*
	{
		Gadgets->SwitchGadget(NetClass);
		return Cast<ANetGadget>(Gadgets->GetCurrentGadget());
	};
	auto StartGroundUse = [&](ANetGadget* Net) -> bool
	{
		Move->SetMovementMode(MOVE_Walking);
		Player->StartGadgetUse();
		return Net && Player->bIsGadgetInUse && Player->CurrentActionState == EPlayerActionState::UseGadget;
	};
	auto GadgetMontageActive = [&](int32 InstanceID)
	{
		const FAnimMontageInstance* Instance = Anim && InstanceID != INDEX_NONE ? Anim->GetMontageInstanceForID(InstanceID) : nullptr;
		return Instance && Instance->IsActive();
	};

	// 1) 正常: 地上アミのNotify ON/OFFで判定窓が開閉し、Montage終了通知で使用を終える。
	ANetGadget* Net = FreshNet();
	if (!TestTrue(TEXT("地上アミの使用開始"), StartGroundUse(Net))) return false;
	const bool bHasMontage = Player->GadgetUseMontageInstanceID != INDEX_NONE;
	if (bHasMontage)
	{
		TestTrue(TEXT("BPが開始したアミMontageを記録"), GadgetMontageActive(Player->GadgetUseMontageInstanceID));
		TestEqual(TEXT("記録したのはAM_Player_NetSwing"), GetNameSafe(Player->GadgetUseMontage.Get()), FString(TEXT("AM_Player_NetSwing")));
	}
	else
	{
		AddWarning(TEXT("検証WorldではAnimInstanceがMontageを再生しなかったため、Montage停止の確認はPIEで行います。"));
	}
	TestFalse(TEXT("OneShotは開始直後にIsUsingがfalseでも"), Net->IsUsing());
	TestTrue(TEXT("使用中なら判定窓を開ける"), Player->CanOpenNetCaptureWindow(Net));
	FireNotify(NotifyOn);
	TestTrue(TEXT("通常使用ではNotify ONで判定窓が開く"), Net->IsCaptureWindowActive());
	FireNotify(NotifyOff);
	TestFalse(TEXT("Notify OFFで判定窓が閉じる"), Net->IsCaptureWindowActive());
	if (bHasMontage)
	{
		// 今回のMontageが再生中に届く終了通知は以前の使用のものとして無視する。
		Player->FinishGadgetUseAnimation(true);
		TestTrue(TEXT("再生中のMontageに対する古い終了通知では終了しない"), Player->bIsGadgetInUse);
		TestFalse(TEXT("別インスタンスのNotifyでは開かない"),
		          Player->CanOpenNetCaptureWindow(Net, Player->GadgetUseMontageInstanceID + 1000));
		TestTrue(TEXT("今回のインスタンスのNotifyでは開ける"),
		         Player->CanOpenNetCaptureWindow(Net, Player->GadgetUseMontageInstanceID));
		Anim->Montage_Stop(0.f, Player->GadgetUseMontage);
	}
	Player->FinishGadgetUseAnimation(false);
	TestFalse(TEXT("Montage終了で使用を終える"), Player->bIsGadgetInUse);
	TestTrue(TEXT("通常終了で通常状態へ戻る"), Player->CurrentActionState == EPlayerActionState::Idle);
	TestFalse(TEXT("通常終了で網のCooldownを消さない"), Net->CanUse());

	// 2) Notify ON前にStun: Montageを止め、後から届くNotify ONでも判定窓を開かない。
	Net = FreshNet();
	if (!TestTrue(TEXT("Stun検証の使用開始"), StartGroundUse(Net))) return false;
	const int32 StunnedInstance = Player->GadgetUseMontageInstanceID;
	Player->BeginStun();
	TestFalse(TEXT("Stunで使用を終える"), Player->bIsGadgetInUse);
	TestTrue(TEXT("Stun状態を維持"), Player->bIsStunned && Player->CurrentActionState == EPlayerActionState::Stun);
	if (bHasMontage)
	{
		TestFalse(TEXT("Stunで地上アミMontageを止める"), GadgetMontageActive(StunnedInstance));
	}
	TestEqual(TEXT("中断後はMontage記録を残さない"), Player->GadgetUseMontageInstanceID, (int32)INDEX_NONE);
	FireNotify(NotifyOn);
	TestFalse(TEXT("Stun後の古いNotify ONでは判定窓を開かない"), Net->IsCaptureWindowActive());
	Net->BeginCaptureWindow();
	TestFalse(TEXT("Notify以外の経路でもStun中は開かない"), Net->IsCaptureWindowActive());
	FireNotify(NotifyOff);
	TestFalse(TEXT("中断後のNotify OFFも安全に閉じる"), Net->IsCaptureWindowActive());
	// 停止で届くInterrupted通知が重なっても、Stunの行動制限とCooldownはそのまま。
	Player->FinishGadgetUseAnimation(true);
	Player->FinishGadgetUseAnimation(true);
	Player->FinishGadgetUseAnimation(false);
	TestTrue(TEXT("重複した終了通知でもStun状態を戻さない"), Player->CurrentActionState == EPlayerActionState::Stun);
	TestFalse(TEXT("重複した終了通知でCooldownを解除しない"), Net->CanUse());
	Player->StartGadgetUse();
	TestFalse(TEXT("Stun中はアミを使えない"), Player->bIsGadgetInUse);
	Player->EndStun();
	TestFalse(TEXT("Stun解除"), Player->bIsStunned);
	TestTrue(TEXT("Stun解除後は通常状態へ戻る"), Player->CurrentActionState == EPlayerActionState::Idle);

	// 3) Notify ON後にStun: 開いていた判定窓を閉じ、Stun解除後に再使用できる。
	Net = FreshNet();
	if (!TestTrue(TEXT("Stun解除後に再使用できる"), StartGroundUse(Net))) return false;
	FireNotify(NotifyOn);
	TestTrue(TEXT("Stun前は判定窓が開く"), Net->IsCaptureWindowActive());
	const float HealthBefore = Player->GetHealthComponent()->GetCurrentHealth();
	// AKI-163: Stun解除（起き上がり）直後は短い無敵が続く。ここでは解除直後の再被弾ではなく接触経路の中断を検証する。
	Player->bIsInvincible = false;
	Boar->BeginChargeHitTracking();	// 被弾は突進中の接触だけ
	Player->HandleBoarContact(Boar);
	Boar->EndChargeHitTracking();
	TestTrue(TEXT("実際の接触経路でダメージを受ける"), Player->GetHealthComponent()->GetCurrentHealth() < HealthBefore);
	TestTrue(TEXT("実際の接触経路でStun"), Player->bIsStunned);
	TestFalse(TEXT("Notify ON後のStunで判定窓を閉じる"), Net->IsCaptureWindowActive());
	FireNotify(NotifyOn);
	TestFalse(TEXT("2回目のNotify ONでも開かない"), Net->IsCaptureWindowActive());
	Player->EndStun();
	Player->bIsInvincible = false;

	// 4) 装備解除・切替（Stage終了も同じInterruptGadgetUse経路）で使用と判定窓が残らない。
	Net = FreshNet();
	if (!TestTrue(TEXT("装備切替検証の使用開始"), StartGroundUse(Net))) return false;
	FireNotify(NotifyOn);
	const int32 SwitchedInstance = Player->GadgetUseMontageInstanceID;
	ANetGadget* NextNet = FreshNet();
	TestFalse(TEXT("装備切替で使用を中断"), Player->bIsGadgetInUse);
	TestTrue(TEXT("装備切替後は通常状態"), Player->CurrentActionState == EPlayerActionState::Idle);
	if (bHasMontage)
	{
		TestFalse(TEXT("装備切替でアミMontageを止める"), GadgetMontageActive(SwitchedInstance));
	}
	FireNotify(NotifyOn);
	TestFalse(TEXT("切替後の新しい網は古いNotifyで開かない"), NextNet && NextNet->IsCaptureWindowActive());
	if (!TestTrue(TEXT("Stage終了検証の使用開始"), StartGroundUse(NextNet))) return false;
	FireNotify(NotifyOn);
	Player->InterruptGadgetUse();
	TestFalse(TEXT("Stage終了の中断で判定窓を閉じる"), NextNet->IsCaptureWindowActive());
	FireNotify(NotifyOn);
	TestFalse(TEXT("Stage終了後のNotifyで開かない"), NextNet->IsCaptureWindowActive());

	// 5) 空中アミ: 使用中は従来どおり開き、Stun後は開かない。
	Net = FreshNet();
	Move->SetMovementMode(MOVE_Falling);
	Player->bIsGadgetInUse = true;
	Player->AirNetDropPhase = EAirNetDropPhase::Swing;
	FireNotify(NotifyOn);
	TestTrue(TEXT("空中アミ使用中はNotify ONで開く"), Net->IsCaptureWindowActive());
	Player->BeginStun();
	TestFalse(TEXT("空中アミのStunで判定窓を閉じる"), Net->IsCaptureWindowActive());
	TestTrue(TEXT("空中アミの段階も終了"), Player->AirNetDropPhase == EAirNetDropPhase::None);
	FireNotify(NotifyOn);
	TestFalse(TEXT("空中アミのStun後も開かない"), Net->IsCaptureWindowActive());
	Player->EndStun();

	// 6) Downed: 使用中にHP0となっても判定窓は残らず、以後も開かない。
	Net = FreshNet();
	if (!TestTrue(TEXT("Downed検証の使用開始"), StartGroundUse(Net))) return false;
	FireNotify(NotifyOn);
	Player->EnterDowned();
	TestFalse(TEXT("Downedで使用を終える"), Player->bIsGadgetInUse);
	TestFalse(TEXT("Downedで判定窓を閉じる"), Net->IsCaptureWindowActive());
	TestTrue(TEXT("Down状態を維持"), Player->CurrentActionState == EPlayerActionState::Down);
	FireNotify(NotifyOn);
	TestFalse(TEXT("Downed後のNotifyで開かない"), Net->IsCaptureWindowActive());
	Player->FinishGadgetUseAnimation(true);
	TestTrue(TEXT("Downed後の終了通知でDownを戻さない"), Player->CurrentActionState == EPlayerActionState::Down);
	return true;
}
#endif
