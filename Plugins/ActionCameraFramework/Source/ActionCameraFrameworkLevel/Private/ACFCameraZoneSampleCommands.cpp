// Phase 7C-3A: ACF.Sample.Zone, the console command of the Camera Zone sample in L_ACFCameraTest (README 32 章 "サンプル").
// Development builds only. The sample zones are the actors tagged ACFSampleZone (Scripts/CreateZoneSampleAssets.py);
// ACFSampleZone_<Name> names one of them (Plaza, Corridor, Alcove).

#if !UE_BUILD_SHIPPING

#include "ACFCameraSubsystem.h"
#include "ACFCameraTypes.h"
#include "ACFCameraZone.h"
#include "ACFCameraZoneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "HAL/IConsoleManager.h"

namespace ACFCameraZoneSample
{
	/** Where Go puts the pawn (L_ACFCameraTest: the zone area in the north-west corner of the floor). */
	static const FVector Entrance(-1800.0, 2900.0, 120.0);

	static void GetZones(UWorld* World, TArray<UACFCameraZoneComponent*>& Out, const FString& Name = FString())
	{
		Out.Reset();
		const FName Tag = Name.IsEmpty() ? FName(TEXT("ACFSampleZone")) : FName(*FString::Printf(TEXT("ACFSampleZone_%s"), *Name));
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(Tag))
			{
				if (UACFCameraZoneComponent* Zone = It->FindComponentByClass<UACFCameraZoneComponent>())
				{
					Out.Add(Zone);
				}
			}
		}
	}

	static FACFCameraBlendSettings Blend(float Seconds)
	{
		if (Seconds <= 0.f)
		{
			return FACFCameraBlendSettings::MakeCut();
		}
		FACFCameraBlendSettings Settings;
		Settings.Curve = EACFCameraBlendCurve::EaseInOut;
		Settings.Duration = Seconds;
		return Settings;
	}

	static void Usage()
	{
		UE_LOG(LogActionCamera, Display, TEXT("Usage: ACF.Sample.Zone Go [Entrance|Plaza|Corridor|Alcove] | Delay <Enter> [Exit] | Blend <Seconds> | KeepView <On|Off> | Enable <Plaza|Corridor|Alcove> <0|1> | Priority <Plaza|Corridor|Alcove> <N> | Status"));
	}

	static void Run(const TArray<FString>& Args, UWorld* World)
	{
		TArray<UACFCameraZoneComponent*> Zones;
		GetZones(World, Zones);
		if (Zones.IsEmpty() || Args.IsEmpty())
		{
			if (Zones.IsEmpty())
			{
				UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Zone: no sample zones (tag ACFSampleZone) in %s. Open /ActionCameraFramework/Samples/L_ACFCameraTest (Scripts/CreateZoneSampleAssets.py adds them)."), *GetNameSafe(World));
			}
			Usage();
			return;
		}
		const FString Kind = Args[0];
		const float Value = Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 0.f;
		if (Kind == TEXT("Go"))
		{
			APlayerController* PC = World->GetFirstPlayerController();
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (!Pawn)
			{
				UE_LOG(LogActionCamera, Warning, TEXT("ACF.Sample.Zone Go: no pawn."));
				return;
			}
			FVector Where = Entrance;
			FRotator Facing(0.0, 180.0, 0.0); // looking west, into the zone area
			if (Args.IsValidIndex(1) && Args[1] != TEXT("Entrance"))
			{
				TArray<UACFCameraZoneComponent*> Named;
				GetZones(World, Named, Args[1]);
				if (Named.IsEmpty())
				{
					Usage();
					return;
				}
				Where = Named[0]->GetOwner()->GetActorLocation();
				Where.Z = Entrance.Z;
			}
			Pawn->TeleportTo(Where, Facing);
			PC->SetControlRotation(FRotator(-15.0, Facing.Yaw, 0.0));
			// What a game does after a teleport (7C-3B): the cameras drop their history, and the zone subsystem resolves the
			// zone at once with a cut (also for short jumps below Teleport Distance).
			if (UACFCameraSubsystem* Acf = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UACFCameraSubsystem>() : nullptr)
			{
				Acf->NotifyTeleport(true);
			}
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Zone Go: %s at %s. Walk west into the Plaza; the Corridor runs along the south wall, the Alcove is the north-west corner."),
				*Pawn->GetName(), *Where.ToCompactString());
		}
		else if (Kind == TEXT("Delay"))
		{
			const float Exit = Args.IsValidIndex(2) ? FCString::Atof(*Args[2]) : Value;
			for (UACFCameraZoneComponent* Zone : Zones)
			{
				Zone->EnterDelay = FMath::Max(Value, 0.f);
				Zone->ExitDelay = FMath::Max(Exit, 0.f);
			}
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Zone Delay: Enter %.2f s, Exit %.2f s on %d zone(s)"), FMath::Max(Value, 0.f), FMath::Max(Exit, 0.f), Zones.Num());
		}
		else if (Kind == TEXT("Blend"))
		{
			for (UACFCameraZoneComponent* Zone : Zones)
			{
				Zone->bOverrideBlendIn = true;
				Zone->BlendIn = Blend(Value);
				Zone->bOverrideBlendOut = true;
				Zone->BlendOut = Blend(Value);
			}
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Zone Blend: %.2f s (ease in / out) in and out on %d zone(s); used from the next zone change"), Value, Zones.Num());
		}
		else if (Kind == TEXT("KeepView"))
		{
			const bool bOn = Args.IsValidIndex(1) && (Args[1] == TEXT("On") || Args[1] == TEXT("1"));
			const EACFCameraZoneRotationHandoff Handoff = bOn ? EACFCameraZoneRotationHandoff::KeepView : EACFCameraZoneRotationHandoff::UseCameraSetting;
			for (UACFCameraZoneComponent* Zone : Zones)
			{
				Zone->EnterRotation = Handoff;
				Zone->ExitRotation = Handoff;
			}
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Zone KeepView %s on %d zone(s)"), bOn ? TEXT("On") : TEXT("Off"), Zones.Num());
		}
		else if ((Kind == TEXT("Enable") || Kind == TEXT("Priority")) && Args.IsValidIndex(2))
		{
			TArray<UACFCameraZoneComponent*> Named;
			GetZones(World, Named, Args[1]);
			for (UACFCameraZoneComponent* Zone : Named)
			{
				if (Kind == TEXT("Enable"))
				{
					Zone->SetZoneEnabled(FCString::Atoi(*Args[2]) != 0);
				}
				else
				{
					Zone->ZonePriority = FCString::Atoi(*Args[2]);
				}
			}
			UE_LOG(LogActionCamera, Display, TEXT("ACF.Sample.Zone %s %s %s (%d zone(s))"), *Kind, *Args[1], *Args[2], Named.Num());
		}
		else if (Kind != TEXT("Status"))
		{
			Usage();
			return;
		}
		GEngine->Exec(World, TEXT("ACF.Zone.Status"));
	}

	static FAutoConsoleCommandWithWorldAndArgs GSampleZoneCmd(TEXT("ACF.Sample.Zone"),
		TEXT("Camera Zone sample in L_ACFCameraTest: Go [Entrance|Plaza|Corridor|Alcove] | Delay <Enter> [Exit] | Blend <Seconds> | KeepView <On|Off> | Enable <Name> <0|1> | Priority <Name> <N> | Status"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
