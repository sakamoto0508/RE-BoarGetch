#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Audio/BoarAudioTypes.h"
#include "BoarAudioManagerSubsystem.generated.h"

class UAudioComponent;
class USceneComponent;
class USoundAttenuation;
class USoundBase;

/**
 * @brief ゲーム全体のBGMと共通SEを一元管理するGameInstance Subsystemです。
 *
 * GameInstanceと同じ寿命を持つため、Level遷移中もBGM再生状態と各音量設定を保持します。
 * Blueprintからは Get Game Instance Subsystem で取得し、BGM、2D SE、位置付きSEを再生します。
 * Master／BGM／SE音量は0～1に制限し、BGM音量の変更は再生中のAudio Componentへ即時反映します。
 * Spawn系（ループ・追従SE）で生成したComponentも登録し、SE音量の変更を再生中の音へ反映します。
 * BGMはUI扱いの2D音声のため、Pause中も止まらず、Pause解除で頭から再生し直すこともありません。
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

	/** 停止・Pause・音色更新が必要な演出用。再生の寿命は呼び出し側で管理する。 */
	UAudioComponent* SpawnSoundEffect2D(UObject* WorldContextObject, USoundBase* Sound, float VolumeMultiplier, float PitchMultiplier);

	/**
	 * Levelごとの既定BGMを適用します。BGMが未設定ならフェードアウトして止め、前のLevelのBGMを残しません。
	 * 別の曲が鳴っていれば旧曲をFadeOutDurationでフェードアウトしながら新曲をフェードインします。
	 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio", meta = (WorldContext = "WorldContextObject"))
	void ApplyLevelBGM(UObject* WorldContextObject, USoundBase* BGM, float FadeInDuration = 1.0f, float FadeOutDuration = 1.0f, bool bRestartIfAlreadyPlaying = false);

	/** 現在のBGMが指定曲でなければフェードアウトします（Stage読み込み直後に前Levelの曲だけを止める用途）。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio")
	void StopBGMUnless(USoundBase* KeepBGM, float FadeOutDuration = 1.0f);

	/** Project Settings（REBoarGetch Audio）のUI共通SEを2Dで再生します。未設定なら何もしません。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio", meta = (WorldContext = "WorldContextObject"))
	void PlayUISound(UObject* WorldContextObject, EBoarUISound Sound);

	/** Project Settings（REBoarGetch Audio）のPortal共通SEを2Dで再生します。未設定なら何もしません。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio", meta = (WorldContext = "WorldContextObject"))
	void PlayPortalSound(UObject* WorldContextObject, EBoarPortalSound Sound);

	/**
	 * 位置付きSEを一度だけ再生します。Attenuation未指定時はProject SettingsのDefault World Attenuationを使います。
	 * AttachToを指定すると移動するActorへ追従します（Boarの鳴き声など）。
	 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Attenuation,VolumeMultiplier,PitchMultiplier"))
	void PlayWorldSoundEffect(UObject* WorldContextObject, USoundBase* Sound, FVector Location, USceneComponent* AttachTo = nullptr, USoundAttenuation* Attenuation = nullptr, float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f);

	/**
	 * AttachToに追従するループ・継続音を開始し、返したComponentを音量設定の反映対象として登録します。
	 * 停止はStopManagedSound（または所有ActorのEndPlay）で行います。ゲームPause中は一時停止します。
	 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio", meta = (AdvancedDisplay = "Attenuation,VolumeMultiplier,PitchMultiplier,FadeInDuration"))
	UAudioComponent* SpawnManagedSoundAttached(USoundBase* Sound, USceneComponent* AttachTo, USoundAttenuation* Attenuation = nullptr, float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f, float FadeInDuration = 0.0f);

	/** SpawnManagedSoundAttached / SpawnSoundEffect2Dで開始した音をフェードアウトして止めます。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Audio")
	void StopManagedSound(UAudioComponent* Component, float FadeOutDuration = 0.0f);

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

	/** @brief Level初期化の通知を購読し、Stage以外のLevelのBGMを適用できるようにします。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** @brief Subsystem終了時にDelegateとBGM再生を安全に破棄します。 */
	virtual void Deinitialize() override;

private:
	/** @brief Master音量とBGM音量を合成し、再生中Componentへ適用します。 */
	void ApplyBGMVolume();

	/** @brief 登録済みSE ComponentへMaster×SE×個別倍率を再適用し、破棄済みの登録を除きます。 */
	void ApplyManagedSoundEffectVolume();

	/** @brief 旧BGMをOldFadeOutで止め（0なら即停止）、新BGMをFadeInで開始します。 */
	void StartBGM(UObject* WorldContextObject, USoundBase* BGM, float FadeInDuration, float OldFadeOutDuration);

	/** @brief 現在のBGM Componentを管理対象から外し、フェードアウトまたは即停止します。 */
	void ReleaseCurrentBGM(float FadeOutDuration);

	/** @brief Stage以外のLevelが初期化されたとき、Project SettingsのLevel BGMを適用します（StageはABoarGameModeが担当）。 */
	void HandleWorldActorsInitialized(const FActorsInitializedParams& Params);

	FDelegateHandle WorldInitializedHandle;

	/** @brief 位置付きSEのAttenuationを解決します（未指定ならProject Settingsの既定値）。 */
	static USoundAttenuation* ResolveWorldAttenuation(USoundAttenuation* Attenuation);

	/** @brief BGMが自然終了またはFadeOut完了したとき、保持参照を解放します。 */
	UFUNCTION()
	void HandleBGMFinished();

	/** Level遷移をまたいで維持するBGM再生Componentです。 */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BGMComponent;

	/** SE音量の変更を反映する再生中Componentと、その個別音量倍率です。 */
	TMap<TWeakObjectPtr<UAudioComponent>, float> ManagedSoundEffects;

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
