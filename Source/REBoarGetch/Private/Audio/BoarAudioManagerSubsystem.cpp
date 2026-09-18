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
	// World ContextまたはSound未設定時はSpawn処理へ進まず、安全に無視する。
	if (!WorldContextObject || !BGM)
	{
		return;
	}

	// 同じBGMが再生中なら、明示的なRestart要求がない限り再生成しない。
	if (BGMComponent && BGMComponent->IsPlaying() && CurrentBGM == BGM && !bRestartIfAlreadyPlaying)
	{
		return;
	}

	// 曲を切り替える前に旧ComponentのDelegateを外し、終了通知の競合を防ぐ。
	if (BGMComponent)
	{
		BGMComponent->OnAudioFinished.RemoveDynamic(this, &UBoarAudioManagerSubsystem::HandleBGMFinished);
		BGMComponent->Stop();
		BGMComponent = nullptr;
	}

	// PersistAcrossLevelTransitionをtrueにし、Level遷移でBGMが途切れないようにする。
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

	// 自然終了時やFadeOut完了時に参照を解放できるよう通知を購読する。
	BGMComponent->OnAudioFinished.AddUniqueDynamic(this, &UBoarAudioManagerSubsystem::HandleBGMFinished);
	BGMComponent->FadeIn(FMath::Max(0.0f, FadeInDuration), MasterVolume * BGMVolume);
}

void UBoarAudioManagerSubsystem::StopBGM(float FadeOutDuration)
{
	// Componentが存在しない場合も、BGMアセット参照だけが残らないよう初期化する。
	if (!BGMComponent)
	{
		CurrentBGM = nullptr;
		return;
	}

	// 正の時間が指定されている場合だけFadeOutし、それ以外は即時停止する。
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
	// 個別倍率は負数を許可せず、MasterとSEカテゴリ音量を乗算して再生する。
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
	// 位置付きSEも2D SEと同じ音量規則を使い、Pitchはゼロ除算を避ける。
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
	// 設定値を有効範囲へ制限し、再生中BGMにも即時反映する。
	MasterVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
	ApplyBGMVolume();
}

void UBoarAudioManagerSubsystem::SetBGMVolume(float NewVolume)
{
	// BGMカテゴリだけを変更し、Master音量との積をComponentへ再適用する。
	BGMVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
	ApplyBGMVolume();
}

void UBoarAudioManagerSubsystem::SetSoundEffectVolume(float NewVolume)
{
	// SEは都度再生するため、次回再生に使用する倍率だけを保持する。
	SoundEffectVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
}

bool UBoarAudioManagerSubsystem::IsBGMPlaying() const
{
	return BGMComponent && BGMComponent->IsPlaying();
}

void UBoarAudioManagerSubsystem::Deinitialize()
{
	// GameInstance終了後にAudioFinishedからSubsystemへ通知されないよう先に解除する。
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
	// BGMの実効音量はMasterとBGMカテゴリの積で決定する。
	if (BGMComponent)
	{
		BGMComponent->SetVolumeMultiplier(MasterVolume * BGMVolume);
	}
}

void UBoarAudioManagerSubsystem::HandleBGMFinished()
{
	// 再生Componentが終了した時点で、現在BGMの管理状態も空へ戻す。
	BGMComponent = nullptr;
	CurrentBGM = nullptr;
}
