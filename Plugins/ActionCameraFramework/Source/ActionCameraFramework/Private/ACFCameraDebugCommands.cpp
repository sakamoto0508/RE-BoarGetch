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
		TEXT("Act on the locked enemy: <Circle [DegPerSec] (toggle)|Teleport [Distance]|Kill>"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleEnemy));
	static FAutoConsoleCommandWithWorldAndArgs GSampleGroupCmd(TEXT("ACF.Sample.Group"),
		TEXT("Target group sample: <Use|Add i|Remove i|Weight i w|Radius i r|Release>"), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SampleGroup));
}

#endif // !UE_BUILD_SHIPPING
