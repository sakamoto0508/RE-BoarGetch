#include "Audio/BoarAudioSettings.h"

#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"

float UBoarAudioSettings::GetSoundVolume(const USoundBase* Sound) const
{
	if (!Sound)
		return 1.0f;

	if (!bSoundVolumeCacheBuilt)
	{
		SoundVolumeCache.Reset();
		for (const FBoarSoundVolumeEntry& Entry : SoundVolumes)
			if (!Entry.Sound.IsNull())
				SoundVolumeCache.Add(Entry.Sound.ToSoftObjectPath(), FMath::Max(0.0f, Entry.Volume));
		bSoundVolumeCacheBuilt = true;
	}

	const float* Found = SoundVolumeCache.Find(FSoftObjectPath(Sound));
	return Found ? *Found : 1.0f;
}

#if WITH_EDITOR
void UBoarAudioSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Project Settingsで表を編集したら、次の再生から新しい値を使う。
	bSoundVolumeCacheBuilt = false;
}
#endif

const FBoarLevelBGMEntry* UBoarAudioSettings::FindLevelBGM(const FString& LevelPackageName) const
{
	for (const FBoarLevelBGMEntry& Entry : LevelBGMs)
		if (!Entry.Level.IsNull() && Entry.Level.ToSoftObjectPath().GetLongPackageName() == LevelPackageName)
			return &Entry;

	return nullptr;
}

USoundBase* UBoarAudioSettings::GetUISound(EBoarUISound Sound) const
{
	switch (Sound)
	{
	case EBoarUISound::Select: return UISelect.LoadSynchronous();
	case EBoarUISound::Confirm: return UIConfirm.LoadSynchronous();
	case EBoarUISound::Cancel: return UICancel.LoadSynchronous();
	case EBoarUISound::Reject: return UIReject.LoadSynchronous();
	case EBoarUISound::Open: return UIOpen.LoadSynchronous();
	case EBoarUISound::Close: return UIClose.LoadSynchronous();
	}
	return nullptr;
}

USoundBase* UBoarAudioSettings::GetPortalSound(EBoarPortalSound Sound) const
{
	switch (Sound)
	{
	case EBoarPortalSound::InteractAccepted: return PortalInteractAccepted.LoadSynchronous();
	case EBoarPortalSound::Activate: return PortalActivate.LoadSynchronous();
	case EBoarPortalSound::Reject: return PortalReject.LoadSynchronous();
	}
	return nullptr;
}

USoundBase* UBoarAudioSettings::GetAmbienceSound(EBoarAmbienceType Type) const
{
	switch (Type)
	{
	case EBoarAmbienceType::Custom: return nullptr;
	case EBoarAmbienceType::Water: return AmbienceWater.LoadSynchronous();
	case EBoarAmbienceType::Forest: return AmbienceForest.LoadSynchronous();
	case EBoarAmbienceType::Wind: return AmbienceWind.LoadSynchronous();
	case EBoarAmbienceType::Windmill: return AmbienceWindmill.LoadSynchronous();
	case EBoarAmbienceType::Facility: return AmbienceFacility.LoadSynchronous();
	case EBoarAmbienceType::Hologram: return AmbienceHologram.LoadSynchronous();
	case EBoarAmbienceType::Ranch: return AmbienceRanch.LoadSynchronous();
	case EBoarAmbienceType::PortalIdle: return AmbiencePortalIdle.LoadSynchronous();
	}
	return nullptr;
}
