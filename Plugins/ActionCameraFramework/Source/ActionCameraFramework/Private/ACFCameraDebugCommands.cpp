// Development-only console commands for exercising ACF without any project content.
//   ACF.Debug.PushFixed X Y Z Pitch Yaw Roll FOV [Priority] [BlendSeconds]
//   ACF.Debug.ReleaseLast [BlendSeconds]
//   ACF.Debug.ReleaseAll [Immediate(0/1)]
//   ACF.Debug.Dump
// They act on the first local player of the world the command runs in.

#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSubsystem.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
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

#if !UE_BUILD_SHIPPING

namespace ACFDebug
{
	static TArray<FACFCameraRequestHandle> GDebugHandles;

	static UACFCameraSubsystem* FindSubsystem(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
		UACFCameraSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UACFCameraSubsystem>() : nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogActionCamera, Warning, TEXT("ACF.Debug: no local player in world %s."), *GetNameSafe(World));
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

	static void PushFixed(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 7)
		{
			UE_LOG(LogActionCamera, Warning, TEXT("Usage: ACF.Debug.PushFixed X Y Z Pitch Yaw Roll FOV [Priority] [BlendSeconds]"));
			return;
		}
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		if (!Subsystem)
		{
			return;
		}

		// Transient preset; kept alive by the director's request entry while the request exists.
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UACFCameraPreset::StaticClass(), TEXT("ACFDebugFixedPreset")), RF_Transient);
		UACFCameraNode_FixedPose* Node = NewObject<UACFCameraNode_FixedPose>(Preset);
		Node->Location = FVector(ArgOr(Args, 0, 0.f), ArgOr(Args, 1, 0.f), ArgOr(Args, 2, 0.f));
		Node->Rotation = FRotator(ArgOr(Args, 3, 0.f), ArgOr(Args, 4, 0.f), ArgOr(Args, 5, 0.f));
		Node->FieldOfView = ArgOr(Args, 6, 90.f);
		Preset->Nodes.Add(Node);
		const float BlendSeconds = ArgOr(Args, 8, 0.f);
		Preset->DefaultBlendIn = MakeLinear(BlendSeconds);
		Preset->DefaultBlendOut = MakeLinear(BlendSeconds);

		FACFCameraRequestParams Params;
		Params.Priority = static_cast<int32>(ArgOr(Args, 7, 0.f));
		Params.DebugName = TEXT("ACF.Debug.PushFixed");
		const FACFCameraRequestHandle Handle = Subsystem->RequestCamera(Preset, Params);
		GDebugHandles.Add(Handle);
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Debug.PushFixed -> %s loc=%s rot=%s fov=%.2f priority=%d blend=%.2f"),
			*Handle.ToString(), *Node->Location.ToString(), *Node->Rotation.ToString(), Node->FieldOfView, Params.Priority, BlendSeconds);
	}

	static void ReleaseLast(const TArray<FString>& Args, UWorld* World)
	{
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		if (!Subsystem || GDebugHandles.IsEmpty())
		{
			return;
		}
		FACFCameraRequestHandle Handle = GDebugHandles.Pop();
		const FString Name = Handle.ToString();
		const bool bRemoved = Args.IsValidIndex(0)
			? Subsystem->ReleaseCameraWithBlend(Handle, MakeLinear(ArgOr(Args, 0, 0.f)))
			: Subsystem->ReleaseCamera(Handle);
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Debug.ReleaseLast %s removed=%d"), *Name, bRemoved ? 1 : 0);
	}

	static void ReleaseAll(const TArray<FString>& Args, UWorld* World)
	{
		if (UACFCameraSubsystem* Subsystem = FindSubsystem(World))
		{
			Subsystem->ReleaseAllCameras(ArgOr(Args, 0, 0.f) != 0.f);
			GDebugHandles.Reset();
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Debug.ReleaseAll"));
		}
	}

	static void Dump(const TArray<FString>& Args, UWorld* World)
	{
		UACFCameraSubsystem* Subsystem = FindSubsystem(World);
		if (!Subsystem)
		{
			return;
		}
		const FACFCameraEvaluationResult Result = Subsystem->GetLastEvaluationResult();
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Debug.Dump frame=%llu active=%s requests=%d weight=%.3f blending=%d alpha=%.3f pose loc=%s rot=%s fov=%.3f"),
			Result.FrameNumber, *Result.ActiveRequest.ToString(), Result.NumRequests, Result.Weight, Result.bIsBlending ? 1 : 0, Result.BlendAlpha,
			*Result.Pose.Location.ToString(), *Result.Pose.Rotation.ToString(), Result.Pose.FieldOfView);

		const APlayerController* PC = World->GetFirstPlayerController();
		if (const APlayerCameraManager* PCM = PC ? PC->PlayerCameraManager.Get() : nullptr)
		{
			// What UE will actually render (camera cache filled at the end of UpdateCamera).
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Debug.Dump PCM=%s viewtarget=%s camera loc=%s rot=%s fov=%.3f"),
				*PCM->GetName(), *GetNameSafe(PCM->GetViewTarget()),
				*PCM->GetCameraLocation().ToString(), *PCM->GetCameraRotation().ToString(), PCM->GetFOVAngle());
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs GPushFixedCmd(TEXT("ACF.Debug.PushFixed"),
		TEXT("Push a fixed ACF camera: X Y Z Pitch Yaw Roll FOV [Priority] [BlendSeconds]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PushFixed));
	static FAutoConsoleCommandWithWorldAndArgs GReleaseLastCmd(TEXT("ACF.Debug.ReleaseLast"),
		TEXT("Release the most recent ACF.Debug.PushFixed request: [BlendSeconds]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ReleaseLast));
	static FAutoConsoleCommandWithWorldAndArgs GReleaseAllCmd(TEXT("ACF.Debug.ReleaseAll"),
		TEXT("Release all ACF requests of the first local player: [Immediate 0/1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ReleaseAll));
	static FAutoConsoleCommandWithWorldAndArgs GDumpCmd(TEXT("ACF.Debug.Dump"),
		TEXT("Log the last ACF evaluation result and the PlayerCameraManager's final camera."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Dump));

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


namespace ACFDebug
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

namespace ACFDebug
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

namespace ACFDebug
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

namespace ACFDebug
{
	// ---------------------------------------------------------------------------------------------
	// Phase 6B / B-1 profiler (README "計測"). Recording itself is switched by the cvar ACF.Profile 0/1.
	//   ACF.Profile.Report [Name]       summary per local player (+ all players per frame) to the log, CSV to Saved/ACFProfile/
	//   ACF.Profile.Clear               drops the recorded frames
	//   ACF.Profile.Allocations <0|1>   counts game-thread allocations inside ACF scopes (installs the counting allocator once)
	//   ACF.Profile.Sites [N]           call stacks of the allocations counted so far

	static void ForEachDirector(UWorld* World, TFunctionRef<void(int32 PlayerIndex, UACFCameraDirector& Director)> Visit)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		if (!GameInstance)
		{
			return;
		}
		const TArray<ULocalPlayer*>& Players = GameInstance->GetLocalPlayers();
		for (int32 Index = 0; Index < Players.Num(); ++Index)
		{
			const UACFCameraSubsystem* Subsystem = Players[Index] ? Players[Index]->GetSubsystem<UACFCameraSubsystem>() : nullptr;
			if (UACFCameraDirector* Director = Subsystem ? Subsystem->GetDirector() : nullptr)
			{
				Visit(Index, *Director);
			}
		}
	}

	static void ProfileReport(const TArray<FString>& Args, UWorld* World)
	{
		const FString Name = Args.IsValidIndex(0) ? Args[0] : TEXT("ACFProfile");
		const FString Folder = FPaths::ProjectSavedDir() / TEXT("ACFProfile");
		TMap<uint64, double> TotalUsByFrame;
		TMap<uint64, int32> QueriesByFrame;
		ForEachDirector(World, [&](int32 PlayerIndex, UACFCameraDirector& Director)
		{
			const FACFCameraProfiler& Profiler = Director.GetProfiler();
			TArray<const FACFCameraProfileFrame*> Frames;
			Profiler.GetFrames(Frames);
			for (const FACFCameraProfileFrame* Frame : Frames)
			{
				TotalUsByFrame.FindOrAdd(Frame->FrameNumber) += Frame->GetTotalMicroseconds();
				QueriesByFrame.FindOrAdd(Frame->FrameNumber) += Frame->GetFrameQueries();
			}
			const FACFCameraProfileSummary Summary = FACFCameraProfileSummary::Summarize(Frames);
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Profile.Report %s player %d:\n%s"), *Name, PlayerIndex, *Summary.ToString());
			const FString Path = Folder / FString::Printf(TEXT("%s_P%d.csv"), *Name, PlayerIndex);
			FFileHelper::SaveStringToFile(ACFCameraProfiling::BuildCsv(Profiler), *Path);
		});
		TArray<double> Totals;
		TArray<double> Queries;
		for (const TPair<uint64, double>& Pair : TotalUsByFrame)
		{
			Totals.Add(Pair.Value);
			Queries.Add(QueriesByFrame[Pair.Key]);
		}
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Profile.Report %s all players per frame: total us %s, queries %s (CSV in %s)"), *Name,
			*FACFCameraProfileStats::Compute(Totals).ToString(), *FACFCameraProfileStats::Compute(Queries).ToString(), *Folder);
	}

	static void ProfileClear(const TArray<FString>& Args, UWorld* World)
	{
		ForEachDirector(World, [](int32, UACFCameraDirector& Director) { Director.GetProfiler().Clear(); });
		FACFCameraAllocationCounter::ResetSites();
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Profile.Clear"));
	}

	static void ProfileAllocations(const TArray<FString>& Args, UWorld* World)
	{
		const bool bOn = !Args.IsValidIndex(0) || FCString::Atoi(*Args[0]) != 0;
		if (bOn && !FACFCameraAllocationCounter::Install())
		{
			return;
		}
		FACFCameraAllocationCounter::SetCounting(bOn);
		UE_LOG(LogActionCamera, Display, TEXT("ACF.Profile.Allocations %d (counts only while ACF.Profile records)"), bOn ? 1 : 0);
	}

	static void ProfileSites(const TArray<FString>& Args, UWorld* World)
	{
		const bool bWasCounting = FACFCameraAllocationCounter::IsCounting();
		FACFCameraAllocationCounter::SetCounting(false); // symbolizing allocates
		TArray<FACFCameraAllocationCounter::FSite> Sites;
		uint32 Overflow = 0;
		FACFCameraAllocationCounter::GetSites(Sites, Overflow);
		Sites.Sort([](const FACFCameraAllocationCounter::FSite& A, const FACFCameraAllocationCounter::FSite& B) { return A.Count > B.Count; });
		const int32 Max = Args.IsValidIndex(0) ? FCString::Atoi(*Args[0]) : 10;
		for (int32 I = 0; I < FMath::Min(Max, Sites.Num()); ++I)
		{
			const FACFCameraAllocationCounter::FSite& Site = Sites[I];
			UE_LOG(LogActionCamera, Display, TEXT("ACF allocation site %d: %u allocations, %llu bytes, section %s\n%s"), I, Site.Count, Site.Bytes,
				Site.Section >= 0 ? LexToString(static_cast<EACFCameraProfileSection>(Site.Section)) : TEXT("(none)"), *FACFCameraAllocationCounter::DescribeSite(Site));
		}
		UE_LOG(LogActionCamera, Display, TEXT("ACF allocation sites: %d distinct, %u allocations at sites beyond the table"), Sites.Num(), Overflow);
		FACFCameraAllocationCounter::SetCounting(bWasCounting);
	}

	static FAutoConsoleCommandWithWorldAndArgs GProfileReportCmd(TEXT("ACF.Profile.Report"),
		TEXT("Summarizes the frames recorded with ACF.Profile 1 (log) and writes one CSV per local player to Saved/ACFProfile/. [Name]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ProfileReport));
	static FAutoConsoleCommandWithWorldAndArgs GProfileClearCmd(TEXT("ACF.Profile.Clear"),
		TEXT("Drops the recorded profiler frames and allocation sites."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ProfileClear));
	static FAutoConsoleCommandWithWorldAndArgs GProfileAllocationsCmd(TEXT("ACF.Profile.Allocations"),
		TEXT("<0|1> Counts game-thread heap allocations inside ACF scopes while ACF.Profile records."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ProfileAllocations));
	static FAutoConsoleCommandWithWorldAndArgs GProfileSitesCmd(TEXT("ACF.Profile.Sites"),
		TEXT("[N] Logs the call stacks of the allocations counted by ACF.Profile.Allocations."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ProfileSites));
}

#endif // !UE_BUILD_SHIPPING
