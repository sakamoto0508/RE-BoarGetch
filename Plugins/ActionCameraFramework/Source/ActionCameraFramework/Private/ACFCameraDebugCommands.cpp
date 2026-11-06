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
#include "ACFCameraSettings.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

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
}

#endif // !UE_BUILD_SHIPPING
