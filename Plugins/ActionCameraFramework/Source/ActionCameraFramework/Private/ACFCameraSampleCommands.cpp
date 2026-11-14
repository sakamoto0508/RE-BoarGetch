// Sample console commands (ACF.Sample.*) for the manual checks of README 9 章 and the .Samples.* tests. They use the
// sample content in /ActionCameraFramework/Samples (presets, effect presets, sequences, the test map L_ACFCameraTest),
// referenced by path only: without that content they log a warning and do nothing.
// Phase 6B / B-10: split from ACFCameraDebugCommands.cpp (ACF.Debug.* / ACF.Profile.* stay there). Compiled only when
// ACF_WITH_SAMPLE_COMMANDS is 1 (default: every configuration except Shipping); README 23 章.
#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSubsystem.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "InputCoreTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Package.h"
#include "ACFCameraDebugShake.h"
#include "Effects/ACFCameraBuiltinEffects.h"
#include "UObject/StrongObjectPtr.h"
#include "ACFCameraSettings.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraProfiler.h"
#include "Engine/GameInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if ACF_WITH_SAMPLE_COMMANDS

namespace ACFSamples
{
	static UACFCameraSubsystem* FindSubsystem(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
		UACFCameraSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UACFCameraSubsystem>() : nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample: no local player in world %s."), *GetNameSafe(World));
		}
		return Subsystem;
	}

	static float ArgOr(const TArray<FString>& Args, int32 Index, float Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : Default;
	}

	static FACFCameraBlendSettings MakeLinear(float Seconds)
	{
		FACFCameraBlendSettings Blend;
		Blend.Curve = Seconds > 0.f ? EACFCameraBlendCurve::Linear : EACFCameraBlendCurve::Cut;
		Blend.Duration = FMath::Max(Seconds, 0.f);
		return Blend;
	}

	// ---------------------------------------------------------------------------------------------
	// Manual verification helpers (see README "手動確認"). Presets live in /ActionCameraFramework/Samples.
	//   ACF.Sample.Use <Follow|ThirdPerson|FreeLook|Composer> [BlendSeconds=0.6] [Live=1]
	//   ACF.Sample.Release [BlendSeconds=0.6]
	//   ACF.Sample.Shake [Scale=1]
	//   ACF.Sample.Teleport [Distance=3000]     (moves the pawn forward; auto teleport detection)
	//   ACF.Sample.Guard <0|1>                   (toggle the output collision guard)

	static FACFCameraRequestHandle GSampleHandle;

	static FACFCameraBlendSettings SampleBlend(float Seconds, bool bLive)
	{
		FACFCameraBlendSettings Blend;
		Blend.Curve = Seconds > 0.f ? EACFCameraBlendCurve::EaseInOut : EACFCameraBlendCurve::Cut;
		Blend.Duration = FMath::Max(Seconds, 0.f);
		Blend.Source = bLive ? EACFCameraBlendSource::Live : EACFCameraBlendSource::Snapshot;
		return Blend;
	}

	static void SampleUse(const TArray<FString>& Args, UWorld* World)
	{
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!Subsystem || !PC || Args.IsEmpty())
		{
			UE_LOG(LogActionCamera, Warning, TEXT("Usage: ACF.Sample.Use <Follow|ThirdPerson|FreeLook|Composer> [BlendSeconds] [Live 0/1]"));
			return;
		}
		const FString Name = Args[0];
		const FString Path = FString::Printf(TEXT("/ActionCameraFramework/Samples/DA_ACFSample_%s.DA_ACFSample_%s"), *Name, *Name);
		UACFCameraPreset* Preset = LoadObject<UACFCameraPreset>(nullptr, *Path);
		if (!Preset)
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Use: %s not found (run Scripts/CreateManualTestAssets.py)."), *Path);
			return;
		}
		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = PC->GetPawn();
		TArray<AActor*> Tagged;
		UGameplayStatics::GetAllActorsWithTag(World, TEXT("ACFLookTarget"), Tagged);
		Params.LookAtTarget.Actor = Tagged.IsEmpty() ? nullptr : Tagged[0];
		Params.bOverrideBlendIn = true;
		Params.BlendIn = SampleBlend(ArgOr(Args, 1, 0.6f), ArgOr(Args, 2, 1.f) != 0.f);
		Params.DebugName = *FString::Printf(TEXT("Sample_%s"), *Name);
		// Request the new one before releasing the old one so the switch is a camera-to-camera blend.
		const FACFCameraRequestHandle Previous = GSampleHandle;
		GSampleHandle = Subsystem->RequestCamera(Preset, Params);
		FACFCameraRequestHandle ToRelease = Previous;
		Subsystem->ReleaseCamera(ToRelease);
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Use %s -> %s (blend %.2fs %s)"), *Name, *GSampleHandle.ToString(),
			Params.BlendIn.Duration, Params.BlendIn.Source == EACFCameraBlendSource::Live ? TEXT("Live") : TEXT("Snapshot"));
	}

	static void SampleRelease(const TArray<FString>& Args, UWorld* World)
	{
		if (UACFCameraSubsystem* Subsystem = FindSubsystem(World))
		{
			Subsystem->ReleaseCameraWithBlend(GSampleHandle, SampleBlend(ArgOr(Args, 0, 0.6f), true));
		}
	}

	static void SampleShake(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (PC && PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraShake(UACFDebugCameraShake::StaticClass(), ArgOr(Args, 0, 1.f));
		}
	}

	static void SampleTeleport(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (APawn* Pawn = PC ? PC->GetPawn() : nullptr)
		{
			const FVector Delta = Pawn->GetActorForwardVector() * ArgOr(Args, 0, 3000.f);
			Pawn->TeleportTo(Pawn->GetActorLocation() + Delta, Pawn->GetActorRotation());
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Teleport moved %s by %s"), *Pawn->GetName(), *Delta.ToString());
		}
	}

	static void SampleGuard(const TArray<FString>& Args, UWorld* World)
	{
		GetMutableDefault<UACFCameraSettings>()->bEnableOutputCollisionGuard = ArgOr(Args, 0, 1.f) != 0.f;
		UE_LOG(LogActionCamera, Display, TEXT("ACF output collision guard: %d"), GetDefault<UACFCameraSettings>()->bEnableOutputCollisionGuard ? 1 : 0);
	}

	static FAutoConsoleCommandWithWorldAndArgs GSampleUseCmd(TEXT("ACF.Sample.Use"),
		TEXT("Request a sample preset for the player pawn: <Follow|ThirdPerson|FreeLook|Composer> [BlendSeconds=0.6] [Live=1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleUse));
	static FAutoConsoleCommandWithWorldAndArgs GSampleReleaseCmd(TEXT("ACF.Sample.Release"),
		TEXT("Release the sample camera: [BlendSeconds=0.6]"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleRelease));
	static FAutoConsoleCommandWithWorldAndArgs GSampleShakeCmd(TEXT("ACF.Sample.Shake"),
		TEXT("Start the debug camera shake (location + roll, 2s): [Scale=1]"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleShake));
	static FAutoConsoleCommandWithWorldAndArgs GSampleTeleportCmd(TEXT("ACF.Sample.Teleport"),
		TEXT("Teleport the pawn forward: [Distance=3000]"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleTeleport));
	static FAutoConsoleCommandWithWorldAndArgs GSampleGuardCmd(TEXT("ACF.Sample.Guard"),
		TEXT("Enable / disable the output collision guard: <0|1>"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleGuard));

	// ---------------------------------------------------------------------------------------------
	// Phase 7B feel check (README 31 章): Movement Assistance needs Input Source = Camera Input, and nothing in the sample
	// map feeds Add Camera Input / Set Camera Input Rate. This command requests a transient copy of a sample preset with the
	// Orbital Follow switched to Camera Input, and forwards the player's raw right stick (rate) and mouse (delta) to ACF
	// every frame, keeping the control rotation on the ACF camera so movement stays camera-relative. Sample code only:
	// it is not an input feature of ACF (a game feeds its own input, README 30 章).
	//   ACF.Sample.Assist On [Facing|Movement] [Preset=ThirdPerson] | Off [Preset] | Heading <Facing|Movement>
	//   ACF.Sample.Assist RealTime <0|1> | Drift <stick units> | Stick <deg/s> | Mouse <deg/count> | Nudge <yaw deg>
	//   ACF.Sample.Assist Release

	struct FAssistBridge
	{
		TWeakObjectPtr<UACFCameraSubsystem> Subsystem;
		TWeakObjectPtr<UACFCameraNode_OrbitalFollow> Orbit; // the transient copy's node (not the asset)
		TStrongObjectPtr<UACFCameraPreset> Preset;
		FDelegateHandle Hook;
		uint64 LastFrame = 0;
		float Drift = 0.f;
		float StickDegreesPerSecond = 180.f;
		float MouseDegreesPerCount = 0.1f;
	};
	static FAssistBridge GAssist;

	/** The orbit node(s) the request really uses: the transient copy, and the tuning copy while Camera Tuning runs. */
	template <typename TFunc>
	static void ForEachAssistOrbit(TFunc&& Func)
	{
		if (UACFCameraNode_OrbitalFollow* Orbit = GAssist.Orbit.Get())
		{
			Func(*Orbit);
		}
		UACFCameraSubsystem* Subsystem = GAssist.Subsystem.Get();
		const UACFCameraPreset* Used = Subsystem && Subsystem->GetDirector() ? Subsystem->GetDirector()->GetRequestPreset(GSampleHandle) : nullptr;
		if (Used && Used != GAssist.Preset.Get())
		{
			for (UACFCameraNode* Node : const_cast<UACFCameraPreset*>(Used)->Nodes)
			{
				if (UACFCameraNode_OrbitalFollow* Orbit = Cast<UACFCameraNode_OrbitalFollow>(Node))
				{
					Func(*Orbit);
				}
			}
		}
	}

	static void AssistForwardInput(UACFCameraSubsystem& Subsystem, float DeltaTime)
	{
		if (&Subsystem != GAssist.Subsystem.Get() || GAssist.LastFrame == GFrameCounter)
		{
			return;
		}
		GAssist.LastFrame = GFrameCounter;
		APlayerController* PC = Subsystem.GetLocalPlayer() ? Subsystem.GetLocalPlayer()->PlayerController.Get() : nullptr;
		if (!PC)
		{
			return;
		}
		// Right stick: a per-second rate (the engine's axis dead zone applies; Drift adds a constant offset for the drift check). Keys by
		// name: no InputCore link dependency for sample code.
		const FVector2D Stick(PC->GetInputAnalogKeyState(FKey(FName(TEXT("Gamepad_RightX")))) + GAssist.Drift, PC->GetInputAnalogKeyState(FKey(FName(TEXT("Gamepad_RightY")))));
		if (!Stick.IsZero())
		{
			Subsystem.SetCameraInputRate(Stick * GAssist.StickDegreesPerSecond);
		}
		// Mouse: this frame's counts, a displacement.
		float MouseX = 0.f;
		float MouseY = 0.f;
		PC->GetInputMouseDelta(MouseX, MouseY);
		if (MouseX != 0.f || MouseY != 0.f)
		{
			Subsystem.AddCameraInput(FVector2D(MouseX, MouseY) * GAssist.MouseDegreesPerCount);
		}
		// Movement input of the pawn is relative to the control rotation: keep it on the camera ACF shows.
		if (Subsystem.IsActiveCamera(GSampleHandle))
		{
			const FRotator Camera = Subsystem.GetLastEvaluationResult().Pose.Rotation;
			PC->SetControlRotation(FRotator(Camera.Pitch, Camera.Yaw, 0.f));
		}
	}

	static void AssistStop()
	{
		if (GAssist.Hook.IsValid())
		{
			UACFCameraSubsystem::OnPreEvaluate.Remove(GAssist.Hook);
			GAssist.Hook.Reset();
		}
		GAssist.Subsystem.Reset();
		GAssist.Orbit.Reset();
		GAssist.Preset.Reset();
	}

	static void AssistRequest(UWorld* World, bool bEnable, const FString& Heading, const FString& PresetName)
	{
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const FString Path = FString::Printf(TEXT("/ActionCameraFramework/Samples/DA_ACFSample_%s.DA_ACFSample_%s"), *PresetName, *PresetName);
		const UACFCameraPreset* Asset = LoadObject<UACFCameraPreset>(nullptr, *Path);
		if (!Subsystem || !PC || !Asset)
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Assist: needs a player and %s."), *Path);
			return;
		}
		// A transient copy (the shared asset is never changed). Values edited on the asset in the editor carry over.
		UACFCameraPreset* Copy = DuplicateObject<UACFCameraPreset>(Asset, GetTransientPackage());
		Copy->SetFlags(RF_Transient);
		UACFCameraNode_OrbitalFollow* Orbit = nullptr;
		for (UACFCameraNode* Node : Copy->Nodes)
		{
			Orbit = Orbit ? Orbit : Cast<UACFCameraNode_OrbitalFollow>(Node);
		}
		if (!Orbit)
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Assist: %s has no Orbital Follow."), *PresetName);
			return;
		}
		Orbit->InputSource = EACFCameraOrbitInputSource::CameraInput;
		Orbit->Sensitivity = FVector2D(1.f, 1.f); // input already in degrees (Stick / Mouse settings above)
		Orbit->InitialRotation = EACFCameraOrbitInitialRotation::UpstreamCamera; // no jump when switching ON / OFF
		Orbit->bEnableMovementAssistance = bEnable;
		Orbit->RecenterHeading = Heading.Equals(TEXT("Movement"), ESearchCase::IgnoreCase) ? EACFCameraRecenterHeading::MovementDirection : EACFCameraRecenterHeading::TargetFacing;

		AssistStop();
		GAssist.Subsystem = Subsystem;
		GAssist.Preset.Reset(Copy);
		GAssist.Orbit = Orbit;
		GAssist.LastFrame = 0;
		GAssist.Hook = UACFCameraSubsystem::OnPreEvaluate.AddStatic(&AssistForwardInput);

		FACFCameraRequestParams Params;
		Params.FollowTarget.Actor = PC->GetPawn();
		Params.bOverrideBlendIn = true;
		Params.BlendIn = SampleBlend(0.f, true);
		Params.DebugName = *FString::Printf(TEXT("Sample_Assist_%s"), bEnable ? TEXT("On") : TEXT("Off"));
		const FACFCameraRequestHandle Previous = GSampleHandle;
		GSampleHandle = Subsystem->RequestCamera(Copy, Params);
		FACFCameraRequestHandle ToRelease = Previous;
		Subsystem->ReleaseCamera(ToRelease);
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Assist %s: %s, heading %s, stick %.0f deg/s, mouse %.3f deg/count, drift %.3f -> %s"),
			bEnable ? TEXT("ON") : TEXT("OFF"), *PresetName, *UEnum::GetValueAsString(Orbit->RecenterHeading), GAssist.StickDegreesPerSecond,
			GAssist.MouseDegreesPerCount, GAssist.Drift, *GSampleHandle.ToString());
	}

	static void SampleAssist(const TArray<FString>& Args, UWorld* World)
	{
		const FString Kind = Args.IsValidIndex(0) ? Args[0] : FString(TEXT("On"));
		auto Arg = [&Args](int32 Index, const TCHAR* Default) { return Args.IsValidIndex(Index) ? Args[Index] : FString(Default); };
		auto IsHeading = [](const FString& Value) { return Value.Equals(TEXT("Facing"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Movement"), ESearchCase::IgnoreCase); };
		if (Kind.Equals(TEXT("On"), ESearchCase::IgnoreCase))
		{
			const bool bHeadingGiven = IsHeading(Arg(1, TEXT("Facing")));
			AssistRequest(World, true, bHeadingGiven ? Arg(1, TEXT("Facing")) : FString(TEXT("Facing")), Arg(bHeadingGiven ? 2 : 1, TEXT("ThirdPerson")));
		}
		else if (Kind.Equals(TEXT("Off"), ESearchCase::IgnoreCase))
		{
			AssistRequest(World, false, TEXT("Facing"), Arg(1, TEXT("ThirdPerson")));
		}
		else if (Kind.Equals(TEXT("Release"), ESearchCase::IgnoreCase))
		{
			AssistStop();
			if (UACFCameraSubsystem* Subsystem = FindSubsystem(World))
			{
				Subsystem->ReleaseCameraWithBlend(GSampleHandle, SampleBlend(0.6f, true));
			}
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Assist released"));
		}
		else if (!GAssist.Hook.IsValid())
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Assist %s: run ACF.Sample.Assist On first."), *Kind);
		}
		else if (Kind.Equals(TEXT("Heading"), ESearchCase::IgnoreCase))
		{
			const bool bMovement = Arg(1, TEXT("Facing")).Equals(TEXT("Movement"), ESearchCase::IgnoreCase);
			ForEachAssistOrbit([bMovement](UACFCameraNode_OrbitalFollow& Orbit) { Orbit.RecenterHeading = bMovement ? EACFCameraRecenterHeading::MovementDirection : EACFCameraRecenterHeading::TargetFacing; });
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Assist Heading %s"), bMovement ? TEXT("Movement Direction") : TEXT("Target Facing"));
		}
		else if (Kind.Equals(TEXT("RealTime"), ESearchCase::IgnoreCase))
		{
			const bool bReal = ArgOr(Args, 1, 1.f) != 0.f;
			ForEachAssistOrbit([bReal](UACFCameraNode_OrbitalFollow& Orbit) { Orbit.bInputRateIgnoresTimeDilation = bReal; });
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Assist RealTime %d (Input Rate Ignores Time Dilation)"), bReal ? 1 : 0);
		}
		else if (Kind.Equals(TEXT("Drift"), ESearchCase::IgnoreCase))
		{
			GAssist.Drift = ArgOr(Args, 1, 0.f);
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Assist Drift %.3f (= %.2f deg/s)"), GAssist.Drift, GAssist.Drift * GAssist.StickDegreesPerSecond);
		}
		else if (Kind.Equals(TEXT("Stick"), ESearchCase::IgnoreCase))
		{
			GAssist.StickDegreesPerSecond = ArgOr(Args, 1, 180.f);
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Assist Stick %.1f deg/s at full tilt"), GAssist.StickDegreesPerSecond);
		}
		else if (Kind.Equals(TEXT("Mouse"), ESearchCase::IgnoreCase))
		{
			GAssist.MouseDegreesPerCount = ArgOr(Args, 1, 0.1f);
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Assist Mouse %.3f deg/count"), GAssist.MouseDegreesPerCount);
		}
		else if (Kind.Equals(TEXT("Nudge"), ESearchCase::IgnoreCase))
		{
			if (UACFCameraSubsystem* Subsystem = GAssist.Subsystem.Get())
			{
				Subsystem->AddCameraInput(FVector2D(ArgOr(Args, 1, 30.f), 0.f)); // one delta, also while paused
				UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Assist Nudge %.1f deg"), ArgOr(Args, 1, 30.f));
			}
		}
		else
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Assist: unknown command %s"), *Kind);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs GSampleAssistCmd(TEXT("ACF.Sample.Assist"),
		TEXT("Movement Assistance feel check (README 31): <On [Facing|Movement] [Preset=ThirdPerson]|Off [Preset]|Heading <Facing|Movement>|RealTime <0|1>|Drift <x>|Stick <deg/s>|Mouse <deg/count>|Nudge <deg>|Release>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleAssist));

	// ACF.Sample.Effect <Recoil|Impulse|Shake|Punch|Motion|Stop>
	static FACFCameraEffectHandle GMotionHandle;

	static void SampleEffect(const TArray<FString>& Args, UWorld* World)
	{
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Subsystem || Args.IsEmpty())
		{
			UE_LOG(LogActionCamera, Warning, TEXT("Usage: ACF.Sample.Effect <Recoil|Impulse|Shake|Punch|Motion|Stop>"));
			return;
		}
		const FString Kind = Args[0];
		if (Kind == TEXT("Recoil"))
		{
			Subsystem->PlayCameraRecoil(ArgOr(Args, 1, 1.f));
		}
		else if (Kind == TEXT("Impulse"))
		{
			const FVector Source = Pawn ? Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 300.f : FVector::ZeroVector;
			Subsystem->PlayCameraImpulse(Source, ArgOr(Args, 1, 1.f));
		}
		else if (Kind == TEXT("Shake"))
		{
			Subsystem->PlayCameraShake(UACFDebugCameraShake::StaticClass(), ArgOr(Args, 1, 1.f));
		}
		else if (Kind == TEXT("Punch"))
		{
			UACFCameraEffectPreset* Punch = Subsystem->GetOrCreateTransientEffectPreset(TEXT("ACFSamplePunch"), [](UACFCameraEffectPreset* P)
			{
				UACFCameraEffect_Punch* Layer = NewObject<UACFCameraEffect_Punch>(P);
				Layer->Location = FVector(-60.f, 0.f, 0.f);
				Layer->FOV = 8.f;
				P->Layers.Add(Layer);
			});
			Subsystem->PlayCameraEffect(Punch, FACFCameraEffectParams());
		}
		else if (Kind == TEXT("Motion"))
		{
			// Toggle Speed FOV + Roll driven by the pawn.
			if (Subsystem->IsCameraEffectActive(GMotionHandle))
			{
				Subsystem->StopCameraEffect(GMotionHandle, false);
				UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Effect Motion: off"));
				return;
			}
			UACFCameraEffectPreset* Motion = Subsystem->GetOrCreateTransientEffectPreset(TEXT("ACFSampleMotion"), [](UACFCameraEffectPreset* P)
			{
				P->Layers.Add(NewObject<UACFCameraEffect_SpeedFOV>(P));
				P->Layers.Add(NewObject<UACFCameraEffect_Roll>(P));
			});
			GMotionHandle = Subsystem->PlayCameraEffect(Motion, FACFCameraEffectParams());
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Effect Motion: on (%s)"), *GMotionHandle.ToString());
		}
		else if (Kind == TEXT("Stop"))
		{
			Subsystem->StopAllCameraEffects(false);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs GSampleEffectCmd(TEXT("ACF.Sample.Effect"),
		TEXT("Play a sample camera effect: <Recoil|Impulse|Shake|Punch|Motion(toggle Speed FOV + Roll)|Stop> [Scale]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleEffect));
}


namespace ACFSamples
{
	// ---------------------------------------------------------------------------------------------
	// Phase 4A manual verification (README "手動確認"). Enemies are the actors tagged ACFEnemy in L_ACFCameraTest.
	//   ACF.Sample.LockOn [Index]                 lock on (or switch to) enemy Index (default: next one)
	//   ACF.Sample.LockOff [BlendSeconds=0.5]     release (control rotation is aligned to the camera first)
	//   ACF.Sample.Enemy <Circle|Teleport|Kill> [Value]   act on the locked enemy
	//   ACF.Sample.Group <Use|Add|Remove|Weight|Radius|Release> [Index] [Value]

	static FACFCameraRequestHandle GLockOnHandle;
	static FACFCameraRequestHandle GGroupHandle;
	static int32 GLockIndex = INDEX_NONE;
	static TWeakObjectPtr<AActor> GLockedEnemy;
	static FTSTicker::FDelegateHandle GCircleTicker;

	static void GetEnemies(UWorld* World, TArray<AActor*>& Out)
	{
		UGameplayStatics::GetAllActorsWithTag(World, TEXT("ACFEnemy"), Out);
		Out.Sort([](const AActor& A, const AActor& B) { return A.GetActorNameOrLabel() < B.GetActorNameOrLabel(); });
	}

	static UACFCameraPreset* LoadSamplePreset(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/ActionCameraFramework/Samples/DA_ACFSample_%s.DA_ACFSample_%s"), Name, Name);
		UACFCameraPreset* Preset = LoadObject<UACFCameraPreset>(nullptr, *Path);
		if (!Preset)
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample: %s not found (run Scripts/CreateManualTestAssets.py)."), *Path);
		}
		return Preset;
	}

	static void SampleLockOn(const TArray<FString>& Args, UWorld* World)
	{
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		TArray<AActor*> Enemies;
		GetEnemies(World, Enemies);
		if (!Subsystem || !PC || !PC->GetPawn() || Enemies.IsEmpty())
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.LockOn: needs a pawn and actors tagged ACFEnemy (L_ACFCameraTest)."));
			return;
		}
		GLockIndex = Args.IsValidIndex(0) ? FMath::Clamp(FCString::Atoi(*Args[0]), 0, Enemies.Num() - 1) : (GLockIndex + 1) % Enemies.Num();
		AActor* Enemy = Enemies[GLockIndex];
		GLockedEnemy = Enemy;
		if (Subsystem->IsCameraRequestAlive(GLockOnHandle))
		{
			// Gameplay switches the target; the camera only glides to it.
			Subsystem->SetCameraLockOnActor(GLockOnHandle, Enemy);
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.LockOn switch -> %s"), *Enemy->GetActorNameOrLabel());
			return;
		}
		UACFCameraPreset* Preset = LoadSamplePreset(TEXT("LockOn"));
		if (!Preset)
		{
			return;
		}
		FACFCameraRequestParams Params;
		Params.Priority = 10; // above the exploration camera (ACF.Sample.Use, priority 0)
		Params.FollowTarget.Actor = PC->GetPawn();
		Params.LookAtTarget.Actor = Enemy;
		Params.bReleaseWhenLookAtTargetLost = true;
		Params.DebugName = TEXT("Sample_LockOn");
		GLockOnHandle = Subsystem->RequestCamera(Preset, Params);
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.LockOn -> %s (%s)"), *Enemy->GetActorNameOrLabel(), *GLockOnHandle.ToString());
	}

	static void AlignControlRotationToCamera(UACFCameraSubsystem* Subsystem, APlayerController* PC)
	{
		// Sample-side gameplay choice: keep facing where the ACF camera looked, so the exploration camera
		// (Input Source = Control Rotation) does not swing back to the old control rotation.
		const FRotator Aim = Subsystem->GetAimCameraPose().Rotation;
		PC->SetControlRotation(FRotator(Aim.Pitch, Aim.Yaw, 0.f));
	}

	static void SampleLockOff(const TArray<FString>& Args, UWorld* World)
	{
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!Subsystem || !PC)
		{
			return;
		}
		if (Subsystem->IsCameraRequestAlive(GLockOnHandle))
		{
			AlignControlRotationToCamera(Subsystem, PC);
		}
		Subsystem->ReleaseCameraWithBlend(GLockOnHandle, SampleBlend(ArgOr(Args, 0, 0.5f), true));
		GLockedEnemy.Reset();
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.LockOff"));
	}

	static void StopCircle()
	{
		if (GCircleTicker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GCircleTicker);
			GCircleTicker.Reset();
		}
	}

	static void SampleEnemy(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AActor* Enemy = GLockedEnemy.Get();
		if (!PC || !PC->GetPawn() || !Enemy || Args.IsEmpty())
		{
			UE_LOG(LogActionCamera, Warning, TEXT("Usage (while locked on): ACF.Sample.Enemy <Circle [DegPerSec=60]|Teleport [Distance=1500]|Kill>"));
			return;
		}
		const FString Kind = Args[0];
		if (Kind == TEXT("Circle"))
		{
			if (GCircleTicker.IsValid())
			{
				StopCircle();
				UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Enemy Circle: off"));
				return;
			}
			const float Speed = ArgOr(Args, 1, 60.f);
			TWeakObjectPtr<AActor> WeakEnemy = Enemy;
			TWeakObjectPtr<APawn> WeakPawn = PC->GetPawn();
			const FVector Offset = Enemy->GetActorLocation() - WeakPawn->GetActorLocation();
			const float Radius = static_cast<float>(FMath::Max(FVector2D(Offset.X, Offset.Y).Size(), 300.0));
			float Angle = static_cast<float>(FMath::Atan2(Offset.Y, Offset.X));
			GCircleTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakEnemy, WeakPawn, Radius, Speed, Angle](float DeltaTime) mutable
			{
				if (!WeakEnemy.IsValid() || !WeakPawn.IsValid())
				{
					GCircleTicker.Reset();
					return false;
				}
				Angle += FMath::DegreesToRadians(Speed) * DeltaTime;
				const FVector Center = WeakPawn->GetActorLocation();
				WeakEnemy->SetActorLocation(FVector(Center.X + FMath::Cos(Angle) * Radius, Center.Y + FMath::Sin(Angle) * Radius, WeakEnemy->GetActorLocation().Z));
				return true;
			}));
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Enemy Circle: on (%.0f deg/s, radius %.0f)"), Speed, Radius);
		}
		else if (Kind == TEXT("Teleport"))
		{
			const FVector Side = FVector::CrossProduct(FVector::UpVector, (Enemy->GetActorLocation() - PC->GetPawn()->GetActorLocation()).GetSafeNormal2D());
			Enemy->SetActorLocation(Enemy->GetActorLocation() + Side * ArgOr(Args, 1, 1500.f));
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Enemy Teleport -> %s"), *Enemy->GetActorLocation().ToCompactString());
		}
		else if (Kind == TEXT("Kill"))
		{
			StopCircle();
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Enemy Kill %s"), *Enemy->GetActorNameOrLabel());
			Enemy->Destroy();
		}
	}

	static void SampleEnemyEx(const TArray<FString>& Args, UWorld* World);

	static void SampleGroup(const TArray<FString>& Args, UWorld* World)
	{
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		TArray<AActor*> Enemies;
		GetEnemies(World, Enemies);
		if (!Subsystem || !PC || !PC->GetPawn() || Args.IsEmpty())
		{
			UE_LOG(LogActionCamera, Warning, TEXT("Usage: ACF.Sample.Group <Use|Add|Remove|Weight|Radius|Release> [EnemyIndex] [Value]"));
			return;
		}
		const FString Kind = Args[0];
		const int32 Index = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : INDEX_NONE;
		AActor* Enemy = Enemies.IsValidIndex(Index) ? Enemies[Index] : nullptr;
		if (Kind == TEXT("Use"))
		{
			UACFCameraPreset* Preset = LoadSamplePreset(TEXT("Group"));
			if (!Preset || Subsystem->IsCameraRequestAlive(GGroupHandle))
			{
				return;
			}
			FACFCameraRequestParams Params;
			Params.Priority = 5;
			Params.FollowTarget.Actor = PC->GetPawn();
			Params.TargetGroup.Members.Add(UACFCameraBlueprintLibrary::MakeTargetGroupMember(PC->GetPawn(), 1.f, 60.f));
			Params.DebugName = TEXT("Sample_Group");
			GGroupHandle = Subsystem->RequestCamera(Preset, Params);
		}
		else if (Kind == TEXT("Release"))
		{
			if (Subsystem->IsActiveCamera(GGroupHandle))
			{
				AlignControlRotationToCamera(Subsystem, PC);
			}
			Subsystem->ReleaseCameraWithBlend(GGroupHandle, SampleBlend(ArgOr(Args, 1, 0.6f), true));
		}
		else if (Kind == TEXT("Add") && Enemy)
		{
			Subsystem->AddCameraGroupActor(GGroupHandle, Enemy, 1.f, 80.f);
		}
		else if (Kind == TEXT("Remove") && Enemy)
		{
			Subsystem->RemoveCameraGroupActor(GGroupHandle, Enemy);
		}
		else if ((Kind == TEXT("Weight") || Kind == TEXT("Radius")) && Enemy)
		{
			for (FACFCameraTargetGroupMember Member : Subsystem->GetCameraGroupMembers(GGroupHandle))
			{
				if (Member.Target.Actor.Get() == Enemy)
				{
					(Kind == TEXT("Weight") ? Member.Weight : Member.Radius) = ArgOr(Args, 2, 1.f);
					Subsystem->AddCameraGroupMember(GGroupHandle, Member);
				}
			}
		}
		else
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Group %s: unknown command or enemy index %d (0..%d)"), *Kind, Index, Enemies.Num() - 1);
		}
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Group %s -> %d members"), *Kind, Subsystem->GetCameraGroupMembers(GGroupHandle).Num());
	}

	static FAutoConsoleCommandWithWorldAndArgs GSampleLockOnCmd(TEXT("ACF.Sample.LockOn"),
		TEXT("Lock on to (or switch to) an ACFEnemy actor: [Index] (default: next)"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleLockOn));
	static FAutoConsoleCommandWithWorldAndArgs GSampleLockOffCmd(TEXT("ACF.Sample.LockOff"),
		TEXT("Release the lock-on camera: [BlendSeconds=0.5]"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleLockOff));
	static FAutoConsoleCommandWithWorldAndArgs GSampleEnemyCmd(TEXT("ACF.Sample.Enemy"),
		TEXT("Act on the locked enemy: <Circle [DegPerSec] (toggle)|Patrol (toggle)|Teleport [Distance]|Kill>"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleEnemyEx));
	static FAutoConsoleCommandWithWorldAndArgs GSampleGroupCmd(TEXT("ACF.Sample.Group"),
		TEXT("Target group sample: <Use|Add i|Remove i|Weight i w|Radius i r|Release>"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleGroup));
}

namespace ACFSamples
{
	// ---------------------------------------------------------------------------------------------
	// Phase 4B manual verification (README "手動確認").
	//   ACF.Sample.Combat <On [Intensity=1]|Off>      gameplay-side combat state (focus = locked enemy, if any)
	//   ACF.Sample.Goto <Start|Wall|Pillars|Corridor>  moves the pawn to a test spot
	//   ACF.Sample.Enemy Patrol                       (toggle) the locked enemy walks around the pillar row
	//   ACF.Debug.Solver <0|1>                        (cvar) bypass the constraint solver for comparison

	static void SampleCombat(const TArray<FString>& Args, UWorld* World)
	{
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		if (!Subsystem || Args.IsEmpty())
		{
			UE_LOG(LogActionCamera, Warning, TEXT("Usage: ACF.Sample.Combat <On [Intensity]|Off>"));
			return;
		}
		if (Args[0] == TEXT("Off"))
		{
			Subsystem->ExitCombat();
		}
		else
		{
			Subsystem->EnterCombat(ArgOr(Args, 1, 1.f), GLockedEnemy.Get());
		}
		const FACFCameraCombatState State = Subsystem->GetCombatState();
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Combat in combat=%d intensity=%.2f focus=%s"), State.bInCombat ? 1 : 0, State.Intensity,
			*GetNameSafe(State.FocusTarget.Actor.Get()));
	}

	static void SampleGoto(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn || Args.IsEmpty())
		{
			UE_LOG(LogActionCamera, Warning, TEXT("Usage: ACF.Sample.Goto <Start|Wall|Pillars|Corridor|Finisher|CorridorFinisher>"));
			return;
		}
		struct FSpot { const TCHAR* Name; FVector Location; float Yaw; };
		static const FSpot Spots[] = {
			{ TEXT("Start"), FVector(0, 0, 120), 0.f },
			{ TEXT("Wall"), FVector(590, -150, 120), 180.f },     // back to the wall, Enemy_0 in front
			{ TEXT("Pillars"), FVector(-200, -1200, 120), 180.f }, // facing the pillar row (Enemy_4 behind it)
			{ TEXT("Corridor"), FVector(-600, 1500, 120), 0.f },  // inside the corridor, Enemy_5 at its far end
			{ TEXT("Finisher"), FVector(1150, -1500, 120), 0.f }, // open ground, Enemy_6 3.5 m ahead (Phase 4C)
			{ TEXT("CorridorFinisher"), FVector(350, 1500, 120), 0.f }, // in the corridor, Enemy_5 3 m ahead (fallback shots)
		};
		for (const FSpot& Spot : Spots)
		{
			if (Args[0] == Spot.Name)
			{
				Pawn->TeleportTo(Spot.Location, FRotator(0.f, Spot.Yaw, 0.f));
				PC->SetControlRotation(FRotator(-15.f, Spot.Yaw, 0.f));
				if (UACFCameraSubsystem* Subsystem = FindSubsystem(World))
				{
					Subsystem->NotifyTeleport(true);
				}
				UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Goto %s"), Spot.Name);
				return;
			}
		}
	}

	static void SamplePatrol(UWorld* World)
	{
		if (GCircleTicker.IsValid())
		{
			StopCircle();
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Enemy Patrol: off"));
			return;
		}
		TWeakObjectPtr<AActor> WeakEnemy = GLockedEnemy;
		if (!WeakEnemy.IsValid())
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Enemy Patrol: lock on to an enemy first."));
			return;
		}
		// Ellipse around the pillar row (pillars at X = -600, Y = -1600 / -1200 / -800).
		float Angle = 0.f;
		GCircleTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakEnemy, Angle](float DeltaTime) mutable
		{
			if (!WeakEnemy.IsValid())
			{
				GCircleTicker.Reset();
				return false;
			}
			Angle += FMath::DegreesToRadians(35.f) * DeltaTime;
			WeakEnemy->SetActorLocation(FVector(-600.f + FMath::Cos(Angle) * 350.f, -1200.f + FMath::Sin(Angle) * 750.f, WeakEnemy->GetActorLocation().Z));
			return true;
		}));
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Enemy Patrol: on"));
	}

	static void SampleEnemyEx(const TArray<FString>& Args, UWorld* World)
	{
		if (!Args.IsEmpty() && Args[0] == TEXT("Patrol"))
		{
			SamplePatrol(World);
			return;
		}
		SampleEnemy(Args, World);
	}

	static FAutoConsoleCommandWithWorldAndArgs GSampleCombatCmd(TEXT("ACF.Sample.Combat"),
		TEXT("Gameplay-side combat state for the sample: <On [Intensity]|Off>"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleCombat));
	static FAutoConsoleCommandWithWorldAndArgs GSampleGotoCmd(TEXT("ACF.Sample.Goto"),
		TEXT("Move the pawn to a test spot: <Start|Wall|Pillars|Corridor|Finisher|CorridorFinisher>"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleGoto));
}

namespace ACFSamples
{
	// ---------------------------------------------------------------------------------------------
	// Phase 4C manual verification (README "手動確認"). The game side (attack / montage) is simulated by commands.
	//   ACF.Sample.Finisher Play [EnemyIndex] [PlayRate]   DA_ACFSample_FinisherSequence on the locked / given / nearest enemy
	//   ACF.Sample.Finisher Preset [Seconds=0]             single-preset finisher (Shot_Approach); 0 = until Finish
	//   ACF.Sample.Finisher <Advance|Hold|Release|Finish|Cancel|Status>
	//   ACF.Sample.Finisher Jump <ShotName>                 Approach / Strike / Overhead
	//   ACF.Sample.Finisher Rate <PlayRate>                 like a montage play-rate change
	//   ACF.Sample.Finisher Notify <Advance|Hold|Release|Finish|Cancel>   same route as the anim notifies (by actor)

	static FACFCameraSequenceHandle GFinisherHandle;

	static AActor* PickFinisherTarget(UWorld* World, const APawn* Pawn, const TArray<FString>& Args)
	{
		TArray<AActor*> Enemies;
		GetEnemies(World, Enemies);
		if (Args.IsValidIndex(1) && Enemies.IsValidIndex(FCString::Atoi(*Args[1])))
		{
			return Enemies[FCString::Atoi(*Args[1])];
		}
		if (GLockedEnemy.IsValid())
		{
			return GLockedEnemy.Get();
		}
		AActor* Best = nullptr;
		for (AActor* Enemy : Enemies)
		{
			if (!Best || FVector::DistSquared(Enemy->GetActorLocation(), Pawn->GetActorLocation()) < FVector::DistSquared(Best->GetActorLocation(), Pawn->GetActorLocation()))
			{
				Best = Enemy;
			}
		}
		return Best;
	}

	static void SampleFinisher(const TArray<FString>& Args, UWorld* World)
	{
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Subsystem || !Pawn || Args.IsEmpty())
		{
			UE_LOG(LogActionCamera, Warning, TEXT("Usage: ACF.Sample.Finisher <Play [Enemy] [Rate]|Preset [Seconds]|Advance|Jump <Shot>|Hold|Release|Rate <x>|Finish|Cancel|Notify <Cmd>|Status>"));
			return;
		}
		const FString& Kind = Args[0];
		if (Kind == TEXT("Play") || Kind == TEXT("Preset"))
		{
			AActor* Target = PickFinisherTarget(World, Pawn, Args);
			if (!Target)
			{
				UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Finisher: no actor tagged ACFEnemy (L_ACFCameraTest)."));
				return;
			}
			if (Kind == TEXT("Play"))
			{
				UACFCameraSequence* Sequence = LoadObject<UACFCameraSequence>(nullptr, TEXT("/ActionCameraFramework/Samples/DA_ACFSample_FinisherSequence.DA_ACFSample_FinisherSequence"));
				if (!Sequence)
				{
					UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Finisher: DA_ACFSample_FinisherSequence not found (run Scripts/CreateManualTestAssets.py)."));
					return;
				}
				GFinisherHandle = Subsystem->StartFinisherCamera(Sequence, Pawn, Target, ArgOr(Args, 2, 1.f));
			}
			else
			{
				FACFCameraBlendSettings In = MakeLinear(0.3f);
				GFinisherHandle = Subsystem->StartFinisherCameraWithPreset(LoadSamplePreset(TEXT("Shot_Approach")), Pawn, Target, ArgOr(Args, 1, 0.f), In, MakeLinear(0.5f), 1000);
			}
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Finisher %s -> %s on %s"), *Kind, *GFinisherHandle.ToString(), *Target->GetActorNameOrLabel());
			return;
		}
		bool bOk = false;
		if (Kind == TEXT("Advance")) { bOk = Subsystem->AdvanceCameraShot(GFinisherHandle); }
		else if (Kind == TEXT("Jump")) { bOk = Args.IsValidIndex(1) && Subsystem->JumpToCameraShot(GFinisherHandle, FName(*Args[1])); }
		else if (Kind == TEXT("Hold")) { bOk = Subsystem->HoldCameraShot(GFinisherHandle); }
		else if (Kind == TEXT("Release")) { bOk = Subsystem->ReleaseCameraShot(GFinisherHandle); }
		else if (Kind == TEXT("Rate")) { bOk = Subsystem->SetCameraSequencePlayRate(GFinisherHandle, ArgOr(Args, 1, 1.f)); }
		else if (Kind == TEXT("Finish")) { bOk = Subsystem->FinishCameraSequence(GFinisherHandle); }
		else if (Kind == TEXT("Cancel")) { bOk = Subsystem->CancelCameraSequence(GFinisherHandle); }
		else if (Kind == TEXT("Notify") && Args.IsValidIndex(1))
		{
			const int64 Value = StaticEnum<EACFCameraSequenceCommand>()->GetValueByNameString(Args[1] == TEXT("Advance") ? TEXT("AdvanceShot")
				: Args[1] == TEXT("Hold") ? TEXT("HoldShot") : Args[1] == TEXT("Release") ? TEXT("ReleaseShot") : Args[1]);
			if (Value != INDEX_NONE)
			{
				const int32 Count = UACFCameraBlueprintLibrary::SendCameraSequenceCommandForActor(Pawn, static_cast<EACFCameraSequenceCommand>(Value), Args.IsValidIndex(2) ? FName(*Args[2]) : NAME_None);
				UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Finisher Notify %s -> %d playback(s)"), *Args[1], Count);
				return;
			}
		}
		else if (Kind != TEXT("Status"))
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Finisher: unknown command %s"), *Kind);
			return;
		}
		const FACFCameraSequenceState S = Subsystem->GetCameraSequenceState(GFinisherHandle);
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Finisher %s (%s): playing=%d shot=%d '%s' t=%.2f/%.2f held=%d fallback=%d in control=%d effects=%d"),
			*Kind, bOk || Kind == TEXT("Status") ? TEXT("ok") : TEXT("no playback"), S.bIsPlaying ? 1 : 0, S.ShotIndex, *S.ShotName.ToString(), S.ShotTime, S.ShotDuration,
			S.bHeld ? 1 : 0, S.bUsingFallback ? 1 : 0, S.bInControl ? 1 : 0, S.NumOwnedEffects);
	}

	static FAutoConsoleCommandWithWorldAndArgs GSampleFinisherCmd(TEXT("ACF.Sample.Finisher"),
		TEXT("Finisher camera sample: <Play [Enemy] [Rate]|Preset [Seconds]|Advance|Jump <Shot>|Hold|Release|Rate <x>|Finish|Cancel|Notify <Cmd>|Status>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleFinisher));
}

#endif // ACF_WITH_SAMPLE_COMMANDS
