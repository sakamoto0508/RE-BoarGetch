#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Audio/BoarAudioTypes.h"
#include "BoarLoopingSoundComponent.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USoundBase;

/**
 * 設備の待機音・環境音を、この位置から鳴らすComponentです（BoarAudioManagerSubsystem経由でSE音量設定に従います）。
 * - Loop：BeginPlayから継続再生し、EndPlay（Level遷移を含む）で停止します。ゲームPause中は一時停止します。
 * - Intermittent：鳥・虫などをランダム間隔（ゲーム時間）で一回ずつ鳴らします。
 * 音源はSoundを直接指定するか、AmbienceTypeでProject Settings（REBoarGetch Audio）の共通音源を使います。
 * 音源が無い場合は何もしません。距離減衰は未指定ならDefault Ambience Attenuationを使います。
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class REBOARGETCH_API UBoarLoopingSoundComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UBoarLoopingSoundComponent();

	/** Customの場合はSoundだけを使います。それ以外はSound未設定時にProject Settingsの共通音源を使います。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	EBoarAmbienceType AmbienceType = EBoarAmbienceType::Custom;

	/** 個別の音源です。設定するとAmbienceTypeの共通音源より優先します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> Sound;

	/** 未設定ならProject SettingsのDefault Ambience Attenuationを使います。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundAttenuation> Attenuation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.0f;

	/** BeginPlayで自動開始します。falseの場合はStartSoundで開始します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	bool bStartOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0", Units = "s"))
	float FadeInDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0", Units = "s"))
	float FadeOutDuration = 0.5f;

	/** trueでループではなくランダム間隔の一回再生にします（森の鳥・虫など）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Intermittent")
	bool bIntermittent = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Intermittent", meta = (EditCondition = "bIntermittent", ClampMin = "0.1", Units = "s"))
	float MinInterval = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Intermittent", meta = (EditCondition = "bIntermittent", ClampMin = "0.1", Units = "s"))
	float MaxInterval = 10.0f;

	/** 一回再生の位置をこの半径（cm）内で水平にばらつかせます。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Intermittent", meta = (EditCondition = "bIntermittent", ClampMin = "0.0", Units = "cm"))
	float RandomRadius = 0.0f;

	UFUNCTION(BlueprintCallable, Category = "Audio")
	void StartSound();

	UFUNCTION(BlueprintCallable, Category = "Audio")
	void StopSound();

	UFUNCTION(BlueprintPure, Category = "Audio")
	bool IsSoundActive() const { return bActive; }

	/** Sound → AmbienceTypeの共通音源の順に解決した音源を返します。 */
	USoundBase* ResolveSound() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ScheduleNextOneShot();
	void PlayOneShot();
	USoundAttenuation* ResolveAttenuation() const;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> LoopComponent;

	FTimerHandle IntermittentTimer;
	bool bActive = false;
};
