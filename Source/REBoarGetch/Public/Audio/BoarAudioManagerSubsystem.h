#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BoarAudioManagerSubsystem.generated.h"

class UAudioComponent;
class USoundBase;

/**
 * LevelをまたいでBGMと共通SE再生を管理するGameInstance Subsystemです。
 * Blueprintから Get Game Instance Subsystem で取得して使用します。
 */
UCLASS(BlueprintType)
class REBOARGETCH_API UBoarAudioManagerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 現在のBGMを必要なら停止し、指定BGMを2D再生します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio", meta = (WorldContext = "WorldContextObject"))
	void PlayBGM(UObject* WorldContextObject, USoundBase* BGM, float FadeInDuration = 1.0f, bool bRestartIfAlreadyPlaying = false);

	/** 現在再生中のBGMをフェードアウトして停止します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio")
	void StopBGM(float FadeOutDuration = 1.0f);

	/** UIなど位置を持たないSEを再生します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio", meta = (WorldContext = "WorldContextObject"))
	void PlaySoundEffect2D(UObject* WorldContextObject, USoundBase* Sound, float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f);

	/** ワールド位置を持つSEを再生します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio", meta = (WorldContext = "WorldContextObject"))
	void PlaySoundEffectAtLocation(UObject* WorldContextObject, USoundBase* Sound, FVector Location, float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f);

	/** 全体音量を0～1で設定します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio")
	void SetMasterVolume(float NewVolume);

	/** BGM音量を0～1で設定します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio")
	void SetBGMVolume(float NewVolume);

	/** SE音量を0～1で設定します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio")
	void SetSoundEffectVolume(float NewVolume);

	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Audio")
	float GetMasterVolume() const { return MasterVolume; }

	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Audio")
	float GetBGMVolume() const { return BGMVolume; }

	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Audio")
	float GetSoundEffectVolume() const { return SoundEffectVolume; }

	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Audio")
	USoundBase* GetCurrentBGM() const { return CurrentBGM; }

	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Audio")
	bool IsBGMPlaying() const;

	virtual void Deinitialize() override;

private:
	void ApplyBGMVolume();

	UFUNCTION()
	void HandleBGMFinished();

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BGMComponent;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CurrentBGM;

	UPROPERTY(VisibleInstanceOnly, Category = "REBoarGetch|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterVolume = 1.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "REBoarGetch|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BGMVolume = 1.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "REBoarGetch|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SoundEffectVolume = 1.0f;
};
