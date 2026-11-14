#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestEnvironment.h"
#include "ACFCameraZoneTestListener.h"
#include "Core/ACFCameraTestUtils.h"
#include "ACFCameraZone.h"
#include "ACFCameraZoneComponent.h"
#include "ACFCameraZoneSubsystem.h"
#include "Components/BoxComponent.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 7C-3B: the Blueprint API of the zone subsystem (Get ACF Camera Zone, On ACF Camera Zone Changed) and the zone request
 * suppression (README 32 章). Synchronous: UpdatePlayer with chosen locations in the real game World, far from the map.
 */
namespace ACFZoneApiTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
	const FVector Far(0.0, 0.0, -350000.0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFZoneApiTest, "ActionCameraFramework.Zone.World.BlueprintApiAndSuppression", ACFZoneApiTests::Flags)
bool FACFZoneApiTest::RunTest(const FString& Parameters)
{
	using namespace ACFZoneApiTests;
	UWorld* World = ACFTestEnvironment::FindGameWorld();
	UACFCameraZoneSubsystem* Zones = World ? World->GetSubsystem<UACFCameraZoneSubsystem>() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
	UACFCameraSubsystem* Acf = Player ? Player->GetSubsystem<UACFCameraSubsystem>() : nullptr;
	if (!World || !World->HasBegunPlay() || !Zones || !Acf)
	{
		AddWarning(TEXT("Skipped: needs a game World with the first player (run with -game)."));
		return true;
	}
	const int32 RequestsAtStart = Acf->GetDirector()->GetNumRequests();
	TStrongObjectPtr<UACFCameraPreset> Preset(ACFTests::MakeFixedPreset(Far + FVector(0.0, 0.0, 800.0), 70.f));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AACFCameraZone* A = World->SpawnActor<AACFCameraZone>(Far, FRotator::ZeroRotator, Params);
	A->GetBox()->SetBoxExtent(FVector(400.0, 400.0, 400.0), false);
	A->GetZone()->Preset = Preset.Get();
	TStrongObjectPtr<UACFCameraZoneTestListener> Listener(NewObject<UACFCameraZoneTestListener>());
	Zones->OnZoneChanged.AddDynamic(Listener.Get(), &UACFCameraZoneTestListener::OnZoneChanged);

	// Blueprint: the event and the getter.
	const FVector Outside = Far + FVector(3000.0, 0.0, 0.0);
	const FVector Inside = Far;
	Zones->UpdatePlayer(Player, &Outside);
	Listener->Calls.Reset();
	Zones->UpdatePlayer(Player, &Inside);
	TestTrue(TEXT("Get ACF Camera Zone: A"), Zones->GetZoneOfPlayer(PC) == A->GetZone());
	TestNull(TEXT("Get ACF Camera Zone: no controller, no zone"), Zones->GetZoneOfPlayer(nullptr));
	TestTrue(TEXT("On ACF Camera Zone Changed: once, with the controller, none -> A"), Listener->Calls.Num() == 1 && Listener->Calls[0].PlayerController.Get() == PC
		&& Listener->Calls[0].Previous.Get() == nullptr && Listener->Calls[0].New.Get() == A->GetZone());
	Zones->UpdatePlayer(Player, &Inside);
	TestEqual(TEXT("No event without a change"), Listener->Calls.Num(), 1);

	// Suppression: the request goes at once, the selection stays, nothing is requested again while suppressed.
	const FACFCameraRequestHandle Request = Zones->GetZoneRequest(Player);
	TestTrue(TEXT("A requested"), Request.IsValid() && Acf->GetDirector()->HasRequest(Request));
	TStrongObjectPtr<UObject> Menu(NewObject<UACFCameraZoneTestListener>());
	TStrongObjectPtr<UObject> Cutscene(NewObject<UACFCameraZoneTestListener>());
	Zones->SuppressZoneRequests(Menu.Get());
	TestTrue(TEXT("Suppressed"), Zones->AreZoneRequestsSuppressed());
	TestFalse(TEXT("Suppress: the zone request released at once"), Acf->GetDirector()->HasRequest(Request) || Zones->GetZoneRequest(Player).IsValid());
	TestTrue(TEXT("Suppress: the selection stays"), Zones->GetSelectedZone(Player) == A->GetZone());
	FACFCameraZonePlayerUpdate Update;
	Update.Location = &Inside;
	Update.DeltaTime = 0.5f;
	for (int32 I = 0; I < 40; ++I) // 20 s: longer than any retry delay
	{
		Zones->UpdatePlayer(Player, Update);
	}
	TestFalse(TEXT("20 s suppressed: never requested again (no retry)"), Zones->GetZoneRequest(Player).IsValid());
	FACFCameraZonePlayerDebug Debug;
	TestTrue(TEXT("Debug view says suppressed"), Zones->GetPlayerDebug(Player, Debug) && Debug.bRequestsSuppressed && Debug.Selected.Get() == A->GetZone());
	Zones->SuppressZoneRequests(Cutscene.Get());
	Zones->SuppressZoneRequests(Cutscene.Get()); // counts once
	TestTrue(TEXT("Releasing one of two sources: still suppressed"), Zones->ReleaseZoneRequestSuppression(Menu.Get()) && Zones->AreZoneRequestsSuppressed());
	TestFalse(TEXT("... no request"), Zones->GetZoneRequest(Player).IsValid());
	TestFalse(TEXT("Releasing a source twice does nothing"), Zones->ReleaseZoneRequestSuppression(Menu.Get()));
	TestTrue(TEXT("Releasing the last source"), Zones->ReleaseZoneRequestSuppression(Cutscene.Get()) && !Zones->AreZoneRequestsSuppressed());
	TestTrue(TEXT("... the zone camera is requested again at once"), Zones->GetZoneRequest(Player).IsValid() && Acf->GetDirector()->HasRequest(Zones->GetZoneRequest(Player)));

	// A destroyed source no longer suppresses (the next update requests again).
	{
		UACFCameraZoneTestListener* Gone = NewObject<UACFCameraZoneTestListener>();
		Zones->SuppressZoneRequests(Gone);
		TestFalse(TEXT("Suppressed by a temporary source"), Zones->GetZoneRequest(Player).IsValid());
		Gone->MarkAsGarbage();
		Zones->UpdatePlayer(Player, &Inside);
		TestTrue(TEXT("Its source destroyed: requested again"), !Zones->AreZoneRequestsSuppressed() && Zones->GetZoneRequest(Player).IsValid());
	}

	Zones->OnZoneChanged.RemoveDynamic(Listener.Get(), &UACFCameraZoneTestListener::OnZoneChanged);
	Zones->UpdatePlayer(Player, static_cast<const FVector*>(nullptr));
	A->Destroy();
	TestEqual(TEXT("End: the player's requests as before"), Acf->GetDirector()->GetNumRequests(), RequestsAtStart);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
