#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BoarAudioManagerSubsystem.generated.h"

class UAudioComponent;
class USoundBase;

/**
 * @brief ゲーム全体のBGMと共通SEを一元管理するGameInstance Subsystemです。
 *
 * GameInstanceと同じ寿命を持つため、Level遷移中もBGM再生状態と各音量設定を保持します。
 * Blueprintからは Get Game Instance Subsystem で取得し、BGM、2D SE、位置付きSEを再生します。
 * Master／BGM／SE音量は0～1に制限し、BGM音量の変更は再生中のAudio Componentへ即時反映します。
 */
UCLASS(BlueprintType)
class REBOARGETCH_API UBoarAudioManagerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** @brief 現在のBGMを必要なら停止し、指定BGMをLevel遷移後も維持される2D音声として再生します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio", meta = (WorldContext = "WorldContextObject"))
	void PlayBGM(UObject* WorldContextObject, USoundBase* BGM, float FadeInDuration = 1.0f, bool bRestartIfAlreadyPlaying = false);

	/** @brief 現在再生中のBGMを指定時間でフェードアウトし、0秒なら即時停止します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio")
	void StopBGM(float FadeOutDuration = 1.0f);

	/** @brief UIなど、ワールド上の位置を持たないSEを一度だけ再生します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio", meta = (WorldContext = "WorldContextObject"))
	void PlaySoundEffect2D(UObject* WorldContextObject, USoundBase* Sound, float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f);

	/** @brief 指定したワールド位置から聞こえるSEを一度だけ再生します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio", meta = (WorldContext = "WorldContextObject"))
	void PlaySoundEffectAtLocation(UObject* WorldContextObject, USoundBase* Sound, FVector Location, float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f);

	/** @brief BGMとSEの両方に掛かるMaster音量を0～1で設定します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio")
	void SetMasterVolume(float NewVolume);

	/** @brief BGM音量を0～1で設定し、再生中BGMへ即時反映します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio")
	void SetBGMVolume(float NewVolume);

	/** @brief 今後再生するSEの音量を0～1で設定します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio")
	void SetSoundEffectVolume(float NewVolume);

	/** @brief 現在のMaster音量を返します。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Audio")
	float GetMasterVolume() const { return MasterVolume; }

	/** @brief 現在のBGM音量を返します。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Audio")
	float GetBGMVolume() const { return BGMVolume; }

	/** @brief 現在のSE音量を返します。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Audio")
	float GetSoundEffectVolume() const { return SoundEffectVolume; }

	/** @brief 現在管理しているBGMアセットを返します。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Audio")
	USoundBase* GetCurrentBGM() const { return CurrentBGM; }

	/** @brief BGM用Audio Componentが再生中ならtrueを返します。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Audio")
	bool IsBGMPlaying() const;

	/** @brief Subsystem終了時にDelegateとBGM再生を安全に破棄します。 */
	virtual void Deinitialize() override;

private:
	/** @brief Master音量とBGM音量を合成し、再生中Componentへ適用します。 */
	void ApplyBGMVolume();

	/** @brief BGMが自然終了またはFadeOut完了したとき、保持参照を解放します。 */
	UFUNCTION()
	void HandleBGMFinished();

	/** Level遷移をまたいで維持するBGM再生Componentです。 */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BGMComponent;

	/** 現在再生対象として管理しているBGMアセットです。 */
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CurrentBGM;

	/** BGMとSEへ共通で乗算する音量です。 */
	UPROPERTY(VisibleInstanceOnly, Category = "REBoarGetch|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterVolume = 1.0f;

	/** BGMへ個別に乗算する音量です。 */
	UPROPERTY(VisibleInstanceOnly, Category = "REBoarGetch|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BGMVolume = 1.0f;

	/** SEへ個別に乗算する音量です。 */
	UPROPERTY(VisibleInstanceOnly, Category = "REBoarGetch|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SoundEffectVolume = 1.0f;
};
