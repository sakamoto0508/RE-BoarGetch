#include "Audio/BoarAudioManagerSubsystem.h"

#include "Audio/BoarAudioSettings.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Core/BoarGameMode.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

void UBoarAudioManagerSubsystem::PlayBGM(
	UObject* WorldContextObject,
	USoundBase* BGM,
	float FadeInDuration,
	bool bRestartIfAlreadyPlaying)
{
	// World ContextまたはSound未設定時はSpawn処理へ進まず、安全に無視する。
	if (!WorldContextObject || !BGM)
		return;


	// 同じBGMが再生中なら、明示的なRestart要求がない限り再生成しない。
	if (BGMComponent && BGMComponent->IsPlaying() && CurrentBGM == BGM && !bRestartIfAlreadyPlaying)
		return;

	// 従来どおり、旧曲は即時停止してから新しい曲をフェードインする。
	StartBGM(WorldContextObject, BGM, FadeInDuration, 0.0f);
}

void UBoarAudioManagerSubsystem::ApplyLevelBGM(
	UObject* WorldContextObject,
	USoundBase* BGM,
	float FadeInDuration,
	float FadeOutDuration,
	bool bRestartIfAlreadyPlaying)
{
	// 未設定のLevelでは前のLevelのBGMを持ち込まない。
	if (!BGM)
	{
		StopBGM(FadeOutDuration);
		return;
	}

	if (!WorldContextObject)
		return;

	// LobbyとGadget Labのように同じ曲を使うLevel間では、曲を頭から鳴らし直さない。
	if (BGMComponent && BGMComponent->IsPlaying() && CurrentBGM == BGM && !bRestartIfAlreadyPlaying)
		return;

	StartBGM(WorldContextObject, BGM, FadeInDuration, FadeOutDuration);
}

void UBoarAudioManagerSubsystem::StopBGMUnless(USoundBase* KeepBGM, float FadeOutDuration)
{
	if (KeepBGM && CurrentBGM == KeepBGM && IsBGMPlaying())
		return;

	StopBGM(FadeOutDuration);
}

void UBoarAudioManagerSubsystem::StartBGM(
	UObject* WorldContextObject,
	USoundBase* BGM,
	float FadeInDuration,
	float OldFadeOutDuration)
{
	// 曲を切り替える前に旧ComponentのDelegateを外し、終了通知の競合を防ぐ。
	ReleaseCurrentBGM(OldFadeOutDuration);

	// PersistAcrossLevelTransitionをtrueにし、Level遷移でBGMが途切れないようにする。
	// 2D Sound（UI扱い）なのでゲームのPause中も再生を続ける。終了時は自動で破棄する。
	CurrentBGM = BGM;
	BGMComponent = UGameplayStatics::SpawnSound2D(
		WorldContextObject,
		BGM,
		MasterVolume * BGMVolume,
		1.0f,
		0.0f,
		nullptr,
		true,
		true);

	if (!BGMComponent)
	{
		CurrentBGM = nullptr;
		return;
	}

	// 自然終了時やFadeOut完了時に参照を解放できるよう通知を購読する。
	BGMComponent->OnAudioFinished.AddUniqueDynamic(this, &UBoarAudioManagerSubsystem::HandleBGMFinished);
	BGMComponent->FadeIn(FMath::Max(0.0f, FadeInDuration), MasterVolume * BGMVolume);
}

void UBoarAudioManagerSubsystem::ReleaseCurrentBGM(float FadeOutDuration)
{
	if (BGMComponent)
	{
		// 管理対象から先に外し、フェード中の旧曲が新しいBGMの参照を消さないようにする。
		BGMComponent->OnAudioFinished.RemoveDynamic(this, &UBoarAudioManagerSubsystem::HandleBGMFinished);

		if (FadeOutDuration > 0.0f && BGMComponent->IsPlaying())
			BGMComponent->FadeOut(FadeOutDuration, 0.0f);
		else
			BGMComponent->Stop();
	}

	BGMComponent = nullptr;
	CurrentBGM = nullptr;
}

void UBoarAudioManagerSubsystem::StopBGM(float FadeOutDuration)
{
	// 正の時間が指定されている場合だけFadeOutし、それ以外は即時停止する。
	// フェード中の旧曲は管理対象から外すため、直後のPlayBGM / ApplyLevelBGMと競合しない。
	ReleaseCurrentBGM(FadeOutDuration);
}

void UBoarAudioManagerSubsystem::PlaySoundEffect2D(
	UObject* WorldContextObject,
	USoundBase* Sound,
	float VolumeMultiplier,
	float PitchMultiplier)
{
	// 個別倍率は負数を許可せず、MasterとSEカテゴリ音量を乗算して再生する。
	if (!WorldContextObject || !Sound)
		return;


	UGameplayStatics::PlaySound2D(
		WorldContextObject,
		Sound,
		MasterVolume * SoundEffectVolume * FMath::Max(0.0f, VolumeMultiplier),
		FMath::Max(0.01f, PitchMultiplier));
}

UAudioComponent* UBoarAudioManagerSubsystem::SpawnSoundEffect2D(
	UObject* WorldContextObject, USoundBase* Sound, float VolumeMultiplier, float PitchMultiplier)
{
	if (!WorldContextObject || !Sound)
		return nullptr;

	const float BaseVolume = FMath::Max(0.f, VolumeMultiplier);
	UAudioComponent* Component = UGameplayStatics::CreateSound2D(
		WorldContextObject, Sound, MasterVolume * SoundEffectVolume * BaseVolume,
		FMath::Max(.01f, PitchMultiplier));
	if (Component)
	{
		Component->bIsUISound = false;
		Component->Play();
		ManagedSoundEffects.Add(Component, BaseVolume);
	}
	return Component;
}

void UBoarAudioManagerSubsystem::PlaySoundEffectAtLocation(
	UObject* WorldContextObject,
	USoundBase* Sound,
	FVector Location,
	float VolumeMultiplier,
	float PitchMultiplier)
{
	// 位置付きSEも2D SEと同じ音量規則を使い、Pitchはゼロ除算を避ける。
	if (!WorldContextObject || !Sound)
		return;


	UGameplayStatics::PlaySoundAtLocation(
		WorldContextObject,
		Sound,
		Location,
		MasterVolume * SoundEffectVolume * FMath::Max(0.0f, VolumeMultiplier),
		FMath::Max(0.01f, PitchMultiplier));
}

void UBoarAudioManagerSubsystem::PlayUISound(UObject* WorldContextObject, EBoarUISound Sound)
{
	// UI音はPause中も鳴る2D再生（PlaySound2DのUI扱い）。
	PlaySoundEffect2D(WorldContextObject, GetDefault<UBoarAudioSettings>()->GetUISound(Sound));
}

void UBoarAudioManagerSubsystem::PlayPortalSound(UObject* WorldContextObject, EBoarPortalSound Sound)
{
	PlaySoundEffect2D(WorldContextObject, GetDefault<UBoarAudioSettings>()->GetPortalSound(Sound));
}

USoundAttenuation* UBoarAudioManagerSubsystem::ResolveWorldAttenuation(USoundAttenuation* Attenuation)
{
	return Attenuation ? Attenuation : GetDefault<UBoarAudioSettings>()->GetDefaultWorldAttenuation();
}

void UBoarAudioManagerSubsystem::PlayWorldSoundEffect(
	UObject* WorldContextObject,
	USoundBase* Sound,
	FVector Location,
	USceneComponent* AttachTo,
	USoundAttenuation* Attenuation,
	float VolumeMultiplier,
	float PitchMultiplier)
{
	if (!Sound || (!WorldContextObject && !AttachTo))
		return;

	const float Volume = MasterVolume * SoundEffectVolume * FMath::Max(0.0f, VolumeMultiplier);
	const float Pitch = FMath::Max(0.01f, PitchMultiplier);
	USoundAttenuation* ResolvedAttenuation = ResolveWorldAttenuation(Attenuation);

	if (AttachTo)
	{
		// 一回きりの追従音。終了時に自動破棄し、所有Actorが消えたら止める。
		UGameplayStatics::SpawnSoundAttached(Sound, AttachTo, NAME_None, FVector::ZeroVector,
			EAttachLocation::KeepRelativeOffset, true, Volume, Pitch, 0.0f, ResolvedAttenuation);
		return;
	}

	UGameplayStatics::PlaySoundAtLocation(WorldContextObject, Sound, Location, FRotator::ZeroRotator,
		Volume, Pitch, 0.0f, ResolvedAttenuation);
}

UAudioComponent* UBoarAudioManagerSubsystem::SpawnManagedSoundAttached(
	USoundBase* Sound,
	USceneComponent* AttachTo,
	USoundAttenuation* Attenuation,
	float VolumeMultiplier,
	float PitchMultiplier,
	float FadeInDuration)
{
	if (!Sound || !AttachTo)
		return nullptr;

	const float BaseVolume = FMath::Max(0.0f, VolumeMultiplier);
	const float Volume = MasterVolume * SoundEffectVolume * BaseVolume;
	UAudioComponent* Component = UGameplayStatics::SpawnSoundAttached(Sound, AttachTo, NAME_None,
		FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, Volume,
		FMath::Max(0.01f, PitchMultiplier), 0.0f, ResolveWorldAttenuation(Attenuation));

	if (!Component)
		return nullptr;

	if (FadeInDuration > 0.0f)
		Component->FadeIn(FadeInDuration, Volume);

	ManagedSoundEffects.Add(Component, BaseVolume);
	return Component;
}

void UBoarAudioManagerSubsystem::StopManagedSound(UAudioComponent* Component, float FadeOutDuration)
{
	if (!Component)
		return;

	ManagedSoundEffects.Remove(Component);

	if (FadeOutDuration > 0.0f && Component->IsPlaying())
		Component->FadeOut(FadeOutDuration, 0.0f);
	else
		Component->Stop();
}

void UBoarAudioManagerSubsystem::SetMasterVolume(float NewVolume)
{
	// 設定値を有効範囲へ制限し、再生中BGMにも即時反映する。
	MasterVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
	ApplyBGMVolume();
	ApplyManagedSoundEffectVolume();
}

void UBoarAudioManagerSubsystem::SetBGMVolume(float NewVolume)
{
	// BGMカテゴリだけを変更し、Master音量との積をComponentへ再適用する。
	BGMVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
	ApplyBGMVolume();
}

void UBoarAudioManagerSubsystem::SetSoundEffectVolume(float NewVolume)
{
	// 一回きりのSEは次回再生から、ループ・継続音は登録済みComponentへ即時反映する。
	SoundEffectVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
	ApplyManagedSoundEffectVolume();
}

bool UBoarAudioManagerSubsystem::IsBGMPlaying() const
{
	return BGMComponent && BGMComponent->IsPlaying();
}

void UBoarAudioManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// LoadMap・PIE開始のどちらでもActor初期化後に呼ばれる。GameModeが確定しているのでStageかどうかを判定できる。
	WorldInitializedHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(
		this, &UBoarAudioManagerSubsystem::HandleWorldActorsInitialized);
}

void UBoarAudioManagerSubsystem::HandleWorldActorsInitialized(const FActorsInitializedParams& Params)
{
	UWorld* World = Params.World;
	if (!World || !World->IsGameWorld() || World->GetGameInstance() != GetGameInstance())
		return;

	// StageのBGMはABoarGameMode（StageConfig）がPlaying開始に合わせて制御する。
	if (Cast<ABoarGameMode>(World->GetAuthGameMode()))
		return;

	const UBoarAudioSettings* Settings = GetDefault<UBoarAudioSettings>();
	const FString Package = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	const FBoarLevelBGMEntry* Entry = Settings->FindLevelBGM(Package);

	if (!Entry)
	{
		// 一覧に無いLevel（Title等）へ前のLevelのBGMを持ち込まない。
		if (Settings->bStopBGMInUnlistedLevels)
			StopBGM(Settings->UnlistedLevelFadeOutSeconds);
		return;
	}

	USoundBase* BGM = Entry->BGM.LoadSynchronous();
	if (!BGM && Entry->bKeepCurrentBGMWhenUnset)
		return;

	ApplyLevelBGM(World, BGM, Entry->FadeInSeconds, Entry->PreviousBGMFadeOutSeconds, false);
}

void UBoarAudioManagerSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldInitializedActors.Remove(WorldInitializedHandle);
	// GameInstance終了後にAudioFinishedからSubsystemへ通知されないよう先に解除する。
	ReleaseCurrentBGM(0.0f);
	ManagedSoundEffects.Empty();
	Super::Deinitialize();
}

void UBoarAudioManagerSubsystem::ApplyBGMVolume()
{
	// BGMの実効音量はMasterとBGMカテゴリの積で決定する。
	if (BGMComponent)
	{
		BGMComponent->SetVolumeMultiplier(MasterVolume * BGMVolume);
	}
}

void UBoarAudioManagerSubsystem::ApplyManagedSoundEffectVolume()
{
	for (auto It = ManagedSoundEffects.CreateIterator(); It; ++It)
	{
		UAudioComponent* Component = It.Key().Get();
		if (!Component)
		{
			It.RemoveCurrent();
			continue;
		}

		Component->SetVolumeMultiplier(MasterVolume * SoundEffectVolume * It.Value());
	}
}

void UBoarAudioManagerSubsystem::HandleBGMFinished()
{
	// 再生Componentが終了した時点で、現在BGMの管理状態も空へ戻す。
	BGMComponent = nullptr;
	CurrentBGM = nullptr;
}
