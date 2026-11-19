#include "Audio/BoarLoopingSoundComponent.h"

#include "Audio/BoarAudioManagerSubsystem.h"
#include "Audio/BoarAudioSettings.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
	UBoarAudioManagerSubsystem* GetAudio(const UActorComponent* Component)
	{
		const UWorld* World = Component ? Component->GetWorld() : nullptr;
		UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
		return Instance ? Instance->GetSubsystem<UBoarAudioManagerSubsystem>() : nullptr;
	}
}

UBoarLoopingSoundComponent::UBoarLoopingSoundComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

USoundBase* UBoarLoopingSoundComponent::ResolveSound() const
{
	if (Sound)
		return Sound;

	return GetDefault<UBoarAudioSettings>()->GetAmbienceSound(AmbienceType);
}

USoundAttenuation* UBoarLoopingSoundComponent::ResolveAttenuation() const
{
	return Attenuation ? Attenuation.Get() : GetDefault<UBoarAudioSettings>()->GetDefaultAmbienceAttenuation();
}

void UBoarLoopingSoundComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bStartOnBeginPlay)
		StartSound();
}

void UBoarLoopingSoundComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Level遷移・Actor破棄でループを残さない。フェードは待たずに止める。
	const float SavedFade = FadeOutDuration;
	FadeOutDuration = 0.0f;
	StopSound();
	FadeOutDuration = SavedFade;
	Super::EndPlay(EndPlayReason);
}

void UBoarLoopingSoundComponent::StartSound()
{
	if (bActive || !GetWorld() || !GetWorld()->IsGameWorld())
		return;

	// 音源未設定は正常な状態として何もしない（後からEditorで設定する）。
	USoundBase* Resolved = ResolveSound();
	if (!Resolved)
		return;

	bActive = true;

	if (bIntermittent)
	{
		ScheduleNextOneShot();
		return;
	}

	if (UBoarAudioManagerSubsystem* Audio = GetAudio(this))
		LoopComponent = Audio->SpawnManagedSoundAttached(Resolved, this, ResolveAttenuation(), VolumeMultiplier,
			PitchMultiplier, FadeInDuration);
}

void UBoarLoopingSoundComponent::StopSound()
{
	bActive = false;

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(IntermittentTimer);

	if (LoopComponent)
	{
		if (UBoarAudioManagerSubsystem* Audio = GetAudio(this))
			Audio->StopManagedSound(LoopComponent, FadeOutDuration);
		else
			LoopComponent->Stop();
	}

	LoopComponent = nullptr;
}

void UBoarLoopingSoundComponent::ScheduleNextOneShot()
{
	UWorld* World = GetWorld();
	if (!bActive || !World)
		return;

	// ゲーム時間のTimerなので、Pause中は次の再生も止まる。
	const float Delay = FMath::FRandRange(FMath::Max(0.1f, MinInterval), FMath::Max(MinInterval, MaxInterval));
	World->GetTimerManager().SetTimer(IntermittentTimer, this, &UBoarLoopingSoundComponent::PlayOneShot, Delay, false);
}

void UBoarLoopingSoundComponent::PlayOneShot()
{
	if (!bActive)
		return;

	if (UBoarAudioManagerSubsystem* Audio = GetAudio(this))
	{
		FVector Location = GetComponentLocation();
		if (RandomRadius > 0.0f)
		{
			const FVector2D Offset = FMath::RandPointInCircle(RandomRadius);
			Location += FVector(Offset.X, Offset.Y, 0.0f);
		}

		Audio->PlayWorldSoundEffect(this, ResolveSound(), Location, nullptr, ResolveAttenuation(), VolumeMultiplier,
			PitchMultiplier);
	}

	ScheduleNextOneShot();
}
