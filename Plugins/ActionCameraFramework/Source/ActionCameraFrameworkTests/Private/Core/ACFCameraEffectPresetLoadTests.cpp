#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestEnvironment.h"
#include "ACFCameraDirector.h"
#include "ACFCameraSettings.h"
#include "ACFCameraSubsystem.h"
#include "Effects/ACFCameraEffect.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"

/**
 * Phase D-2 (README 6 章「既定の Preset」): Play ACF Camera Impulse / Recoil without a preset use the project's
 * DefaultImpulsePreset / DefaultRecoilPreset (soft references). Before the fix nothing kept the loaded preset: after its
 * effects ended a GC unloaded it and the next impulse (in combat) loaded it again synchronously. The times are measured
 * here (editor build, uncooked package from disk: indicative only; a cooked build loads differently) and logged.
 */
namespace ACFEffectPresetLoad
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
	const TCHAR* HitPath = TEXT("/ActionCameraFramework/Samples/DA_ACFSample_FinisherHit.DA_ACFSample_FinisherHit");
	const TCHAR* TensionPath = TEXT("/ActionCameraFramework/Samples/DA_ACFSample_FinisherTension.DA_ACFSample_FinisherTension");

	static double TimeImpulseMs(UACFCameraSubsystem& Acf)
	{
		const double T0 = FPlatformTime::Seconds();
		Acf.PlayCameraImpulse(FVector::ZeroVector, 1.f, nullptr);
		return (FPlatformTime::Seconds() - T0) * 1000.0;
	}

	static void EndEffects(UACFCameraSubsystem& Acf)
	{
		Acf.StopAllCameraEffects(true);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFEffectPresetLoadTest, "ActionCameraFramework.Effects.DefaultPresetLoad", ACFEffectPresetLoad::Flags)
bool FACFEffectPresetLoadTest::RunTest(const FString& Parameters)
{
	using namespace ACFEffectPresetLoad;
	UWorld* World = ACFTestEnvironment::FindGameWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	UACFCameraSubsystem* Acf = PC && PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UACFCameraSubsystem>() : nullptr;
	if (!World || !Acf)
	{
		AddWarning(TEXT("Skipped: needs a game World with the first player's camera (run with -game)."));
		return true;
	}
	UACFCameraSettings* Settings = GetMutableDefault<UACFCameraSettings>();
	ACFTestEnvironment::FGlobalStateGuard Guard;
	Guard.Capture(World, { Settings });
	ON_SCOPE_EXIT { Acf->StopAllCameraEffects(true); Guard.Restore(); };

	const FSoftObjectPath Hit(HitPath);
	EndEffects(*Acf);
	if (Hit.ResolveObject())
	{
		AddWarning(TEXT("Skipped: the sample effect preset is already loaded here (another asset references it)."));
		return true;
	}
	Settings->DefaultImpulsePreset = TSoftObjectPtr<UACFCameraEffectPreset>(Hit);

	// First impulse: not loaded yet (loaded synchronously by this call, or earlier by a preload).
	const double FirstMs = TimeImpulseMs(*Acf);
	TestTrue(TEXT("The configured preset is used"), Hit.ResolveObject() != nullptr);
	const double SecondMs = TimeImpulseMs(*Acf);
	// The effects end and a GC runs (a level change, the periodic GC): the preset must stay loaded.
	EndEffects(*Acf);
	const bool bKept = Hit.ResolveObject() != nullptr;
	TestTrue(TEXT("The default preset stays loaded after its effects ended and a GC ran (no reload in combat)"), bKept);
	const double AfterGcMs = TimeImpulseMs(*Acf);
	AddInfo(FString::Printf(TEXT("Default impulse preset: first impulse %.3f ms, second %.3f ms, after effects ended + GC %.3f ms (still loaded: %s)."),
		FirstMs, SecondMs, AfterGcMs, bKept ? TEXT("yes") : TEXT("no, reloaded")));

	// The setting changes at run time: the new preset is used (no stale cache), the old one is released.
	const FSoftObjectPath Tension(TensionPath);
	Settings->DefaultImpulsePreset = TSoftObjectPtr<UACFCameraEffectPreset>(Tension);
	EndEffects(*Acf);
	Acf->PlayCameraImpulse(FVector::ZeroVector, 1.f, nullptr);
	TestTrue(TEXT("Changed setting: the new preset is used"), Tension.ResolveObject() != nullptr);
	EndEffects(*Acf);
	TestTrue(TEXT("Changed setting: the previous preset is no longer kept"), Hit.ResolveObject() == nullptr);

	// No preset configured: the built-in one, nothing loaded.
	Settings->DefaultImpulsePreset.Reset();
	EndEffects(*Acf);
	const FACFCameraEffectHandle Builtin = Acf->PlayCameraImpulse(FVector::ZeroVector, 1.f, nullptr);
	TestTrue(TEXT("No preset configured: the built-in impulse plays"), Builtin.IsValid() || !Acf->CanPlayCameraEffects());
	EndEffects(*Acf);
	TestTrue(TEXT("No preset configured: the previous preset is released"), Tension.ResolveObject() == nullptr);
	return true;
}

#endif
