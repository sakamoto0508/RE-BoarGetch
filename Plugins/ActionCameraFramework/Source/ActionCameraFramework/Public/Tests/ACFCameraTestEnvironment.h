#pragma once

// Phase 6B / B-9: test hygiene shared by the scenario runners of the Core and Sequencer test modules (real game World,
// real PlayerController / PCM). Header only, compiled only with WITH_DEV_AUTOMATION_TESTS (never in Shipping), no
// runtime hooks: nothing here runs unless a test calls it. README 22 章.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraDirector.h"
#include "ACFCameraProfiler.h"
#include "ACFCameraSettings.h"
#include "ACFCameraSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace ACFTestEnvironment
{
	inline UWorld* FindGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	/**
	 * Process-wide state the scenarios change: every "ACF." console variable, the ACF settings objects (e.g. the output
	 * collision guard), the fixed time step, pause and global time dilation of the game World. Captured when a scenario
	 * starts, compared and restored when it ends (also when it aborts half way), so a failing test cannot change the
	 * conditions of the following ones. A test that leaves any of it changed gets an error naming it.
	 */
	class FGlobalStateGuard
	{
	public:
		void Capture(UWorld* InWorld, TConstArrayView<UObject*> ExtraSettings = {})
		{
			bCaptured = true;
			World = InWorld;
			CVars.Reset();
			IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda([this](const TCHAR* Name, IConsoleObject* Object)
			{
				if (IConsoleVariable* Var = Object ? Object->AsVariable() : nullptr)
				{
					CVars.Add({ FString(Name), Var->GetString() });
				}
			}), TEXT("ACF."));
			Settings.Reset();
			AddSettings(GetMutableDefault<UACFCameraSettings>());
			for (UObject* Object : ExtraSettings)
			{
				AddSettings(Object);
			}
			bFixedStep = FApp::UseFixedTimeStep();
			FixedDt = FApp::GetFixedDeltaTime();
			bPaused = InWorld && UGameplayStatics::IsGamePaused(InWorld);
			TimeDilation = InWorld && InWorld->GetWorldSettings() ? InWorld->GetWorldSettings()->TimeDilation : 1.f;
		}

		/** Restores what differs from the capture. Returns the names of what had to be restored (empty = nothing leaked). */
		TArray<FString> Restore()
		{
			TArray<FString> Restored;
			if (!bCaptured)
			{
				return Restored;
			}
			bCaptured = false;
			for (const TPair<FString, FString>& Saved : CVars)
			{
				IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Saved.Key);
				if (Var && Var->GetString() != Saved.Value)
				{
					Restored.Add(FString::Printf(TEXT("console variable %s (%s -> %s)"), *Saved.Key, *Var->GetString(), *Saved.Value));
					Var->Set(*Saved.Value, ECVF_SetByCode);
				}
			}
			for (FSettingsSnapshot& Snapshot : Settings)
			{
				UObject* Object = Snapshot.Object.Get();
				if (Object && Serialize(Object) != Snapshot.Bytes)
				{
					Restored.Add(FString::Printf(TEXT("settings %s"), *Object->GetClass()->GetName()));
					FObjectReader Reader(Object, Snapshot.Bytes);
				}
			}
			if (FApp::UseFixedTimeStep() != bFixedStep || FApp::GetFixedDeltaTime() != FixedDt)
			{
				Restored.Add(TEXT("fixed time step"));
				FApp::SetUseFixedTimeStep(bFixedStep);
				FApp::SetFixedDeltaTime(FixedDt);
			}
			if (UWorld* W = World.Get())
			{
				if (UGameplayStatics::IsGamePaused(W) != bPaused)
				{
					Restored.Add(TEXT("game pause"));
					UGameplayStatics::SetGamePaused(W, bPaused);
				}
				AWorldSettings* WorldSettings = W->GetWorldSettings();
				if (WorldSettings && WorldSettings->TimeDilation != TimeDilation)
				{
					Restored.Add(TEXT("global time dilation"));
					WorldSettings->SetTimeDilation(TimeDilation);
				}
			}
			return Restored;
		}

		~FGlobalStateGuard() { Restore(); }

	private:
		struct FSettingsSnapshot
		{
			TWeakObjectPtr<UObject> Object;
			TArray<uint8> Bytes;
		};

		static TArray<uint8> Serialize(UObject* Object)
		{
			TArray<uint8> Bytes;
			FObjectWriter Writer(Object, Bytes); // read back with FObjectReader(Object, Bytes): same flags
			return Bytes;
		}

		void AddSettings(UObject* Object)
		{
			if (Object)
			{
				Settings.Add({ Object, Serialize(Object) });
			}
		}

		bool bCaptured = false;
		TWeakObjectPtr<UWorld> World;
		TArray<TPair<FString, FString>> CVars;
		TArray<FSettingsSnapshot> Settings;
		bool bFixedStep = false;
		double FixedDt = 0.0;
		bool bPaused = false;
		float TimeDilation = 1.f;
	};

	/** Start of a scenario on a real player: diagnostics count from zero, so a check cannot pass on an earlier test's reports. */
	inline void BeginScenario(UACFCameraSubsystem& Acf)
	{
		if (UACFCameraDirector* Director = Acf.GetDirector())
		{
			Director->GetHealth().Reset();
		}
	}

	/**
	 * End of a scenario, after its own cleanup: stops what is left (requests, sequences, effects, suppressions are the
	 * scenario's job; this only catches what slipped through) and reports it as an error of the test that leaked it.
	 */
	inline void EndScenario(FAutomationTestBase& Test, UACFCameraSubsystem* Acf, const APlayerCameraManager* PCM, FGlobalStateGuard& Guard)
	{
		TArray<FString> Leaks = Guard.Restore();
		if (Acf)
		{
			UACFCameraDirector* Director = Acf->GetDirector();
			TArray<FACFCameraSequenceHandle> Sequences;
			if (Director)
			{
				Director->GetActiveSequences(Sequences);
			}
			if (!Sequences.IsEmpty())
			{
				Leaks.Add(FString::Printf(TEXT("%d camera sequence(s)"), Sequences.Num()));
				Director->StopAllSequences(true);
			}
			if (Director && Director->GetNumRequests() > 0)
			{
				Leaks.Add(FString::Printf(TEXT("%d camera request(s)"), Director->GetNumRequests()));
				Acf->ReleaseAllCameras(true);
			}
			if (Director && Director->GetNumActiveEffects() > 0)
			{
				Leaks.Add(FString::Printf(TEXT("%d camera effect(s)"), Director->GetNumActiveEffects()));
				Acf->StopAllCameraEffects(true);
			}
			if (Acf->GetNumOutputSuppressions() > 0)
			{
				Leaks.Add(FString::Printf(TEXT("%d output suppression(s)"), Acf->GetNumOutputSuppressions()));
			}
			if (PCM && PCM->GetViewTarget() && Acf->IsExternalViewTarget(PCM->GetViewTarget()))
			{
				Leaks.Add(FString::Printf(TEXT("external view target %s still registered and on screen"), *GetNameSafe(PCM->GetViewTarget())));
			}
		}
#if ACF_WITH_PROFILER
		if (FACFCameraProfiler::GetActive())
		{
			Leaks.Add(TEXT("an active camera profiler recording"));
		}
#endif
		for (const FString& Leak : Leaks)
		{
			Test.AddError(FString::Printf(TEXT("Test environment: the scenario left %s behind (restored / stopped for the next test)."), *Leak));
		}
	}

	/**
	 * A second local player's PlayerController runs BeginPlay before its LocalPlayer is attached. Host-project controllers
	 * that create widgets in BeginPlay (REBoarGetch's BP_PC_Lobby) make UMG log an error that has nothing to do with ACF.
	 * Only when the World's PlayerController class is NOT native (a Blueprint of the host project) is exactly that message
	 * expected; with the engine / ACF test environment (README 22 章) nothing is ignored and such an error fails the test.
	 */
	inline void ExpectHostWidgetErrorForExtraLocalPlayers(FAutomationTestBase& Test)
	{
		const UWorld* World = FindGameWorld();
		const AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
		const UClass* ControllerClass = GameMode ? GameMode->PlayerControllerClass.Get() : nullptr;
		if (ControllerClass && !ControllerClass->HasAnyClassFlags(CLASS_Native))
		{
			Test.AddInfo(FString::Printf(TEXT("Host PlayerController %s is a Blueprint: its CreateWidget error for a player without a LocalPlayer is expected."), *ControllerClass->GetName()));
			Test.AddExpectedErrorPlain(TEXT("CreateWidget cannot be used on Player Controller with no attached player"), EAutomationExpectedErrorFlags::Contains, -1);
		}
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
