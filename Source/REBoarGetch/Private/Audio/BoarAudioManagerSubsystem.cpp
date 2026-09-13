#include "Audio/BoarAudioManagerSubsystem.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

void UBoarAudioManagerSubsystem::PlayBGM(
	UObject* WorldContextObject,
	USoundBase* BGM,
	float FadeInDuration,
	bool bRestartIfAlreadyPlaying)
{
	if (!WorldContextObject || !BGM)
	{
		return;
	}

	if (BGMComponent && BGMComponent->IsPlaying() && CurrentBGM == BGM && !bRestartIfAlreadyPlaying)
	{
		return;
	}

	if (BGMComponent)
	{
		BGMComponent->OnAudioFinished.RemoveDynamic(this, &UBoarAudioManagerSubsystem::HandleBGMFinished);
		BGMComponent->Stop();
		BGMComponent = nullptr;
	}

	CurrentBGM = BGM;
	BGMComponent = UGameplayStatics::SpawnSound2D(
		WorldContextObject,
		BGM,
		MasterVolume * BGMVolume,
		1.0f,
		0.0f,
		nullptr,
		true,
		false);

	if (!BGMComponent)
	{
		CurrentBGM = nullptr;
		return;
	}

	BGMComponent->OnAudioFinished.AddUniqueDynamic(this, &UBoarAudioManagerSubsystem::HandleBGMFinished);
	BGMComponent->FadeIn(FMath::Max(0.0f, FadeInDuration), MasterVolume * BGMVolume);
}

void UBoarAudioManagerSubsystem::StopBGM(float FadeOutDuration)
{
	if (!BGMComponent)
	{
		CurrentBGM = nullptr;
		return;
	}

	if (FadeOutDuration > 0.0f && BGMComponent->IsPlaying())
	{
		BGMComponent->FadeOut(FadeOutDuration, 0.0f);
	}
	else
	{
		BGMComponent->Stop();
		BGMComponent = nullptr;
		CurrentBGM = nullptr;
	}
}

void UBoarAudioManagerSubsystem::PlaySoundEffect2D(
	UObject* WorldContextObject,
	USoundBase* Sound,
	float VolumeMultiplier,
	float PitchMultiplier)
{
	if (!WorldContextObject || !Sound)
	{
		return;
	}

	UGameplayStatics::PlaySound2D(
		WorldContextObject,
		Sound,
		MasterVolume * SoundEffectVolume * FMath::Max(0.0f, VolumeMultiplier),
		FMath::Max(0.01f, PitchMultiplier));
}

void UBoarAudioManagerSubsystem::PlaySoundEffectAtLocation(
	UObject* WorldContextObject,
	USoundBase* Sound,
	FVector Location,
	float VolumeMultiplier,
	float PitchMultiplier)
{
	if (!WorldContextObject || !Sound)
	{
		return;
	}

	UGameplayStatics::PlaySoundAtLocation(
		WorldContextObject,
		Sound,
		Location,
		MasterVolume * SoundEffectVolume * FMath::Max(0.0f, VolumeMultiplier),
		FMath::Max(0.01f, PitchMultiplier));
}

void UBoarAudioManagerSubsystem::SetMasterVolume(float NewVolume)
{
	MasterVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
	ApplyBGMVolume();
}

void UBoarAudioManagerSubsystem::SetBGMVolume(float NewVolume)
{
	BGMVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
	ApplyBGMVolume();
}

void UBoarAudioManagerSubsystem::SetSoundEffectVolume(float NewVolume)
{
	SoundEffectVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
}

bool UBoarAudioManagerSubsystem::IsBGMPlaying() const
{
	return BGMComponent && BGMComponent->IsPlaying();
}

void UBoarAudioManagerSubsystem::Deinitialize()
{
	if (BGMComponent)
	{
		BGMComponent->OnAudioFinished.RemoveDynamic(this, &UBoarAudioManagerSubsystem::HandleBGMFinished);
		BGMComponent->Stop();
		BGMComponent = nullptr;
	}

	CurrentBGM = nullptr;
	Super::Deinitialize();
}

void UBoarAudioManagerSubsystem::ApplyBGMVolume()
{
	if (BGMComponent)
	{
		BGMComponent->SetVolumeMultiplier(MasterVolume * BGMVolume);
	}
}

void UBoarAudioManagerSubsystem::HandleBGMFinished()
{
	BGMComponent = nullptr;
	CurrentBGM = nullptr;
}
