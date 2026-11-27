#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Audio/BoarAudioTypes.h"
#include "BoarAudioSettings.generated.h"

class USoundBase;
class USoundAttenuation;
class UWorld;

/** Stage以外のLevel（Lobby・Gadget Lab・Archive等）のBGM設定です。Stage BGMはStageConfigで設定します。 */
USTRUCT()
struct REBOARGETCH_API FBoarLevelBGMEntry
{
	GENERATED_BODY()

	/** BGM設定を適用するLevel。 */
	UPROPERTY(Config, EditAnywhere, Category = "Level BGM")
	TSoftObjectPtr<UWorld> Level;

	/** このLevelのBGM。前のLevelと同じ曲なら頭から再生し直さず継続します。 */
	UPROPERTY(Config, EditAnywhere, Category = "Level BGM")
	TSoftObjectPtr<USoundBase> BGM;

	/** BGMが未設定のとき、前のLevelのBGMを止めずに継続します（Gadget Lab・Archive等）。falseなら止めます。 */
	UPROPERTY(Config, EditAnywhere, Category = "Level BGM")
	bool bKeepCurrentBGMWhenUnset = false;

	/** Level BGMのフェードイン時間（秒）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Level BGM", meta = (ClampMin = "0.0", Units = "s"))
	float FadeInSeconds = 1.0f;

	/** 前のLevelから続く別の曲をフェードアウトする時間です。 */
	UPROPERTY(Config, EditAnywhere, Category = "Level BGM", meta = (ClampMin = "0.0", Units = "s"))
	float PreviousBGMFadeOutSeconds = 1.0f;
};

/** 音源ごとの音量（ミックス）。音源アセットを変えずに、どの再生箇所でもこの倍率を掛けます。 */
USTRUCT()
struct REBOARGETCH_API FBoarSoundVolumeEntry
{
	GENERATED_BODY()

	/** この音量設定を適用するサウンド。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mix")
	TSoftObjectPtr<USoundBase> Sound;

	/** 1.0で音源そのままの音量。BGM・SEどちらにも使えます。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mix", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float Volume = 1.0f;

	/** 用途と調整理由のメモ（再生には使いません）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mix")
	FString Note;
};

// 複数の画面・Actorが共有するSEと距離減衰の設定です（Project Settings > Game > REBoarGetch Audio。DefaultGame.iniに保存）。
// Stage BGMはStageConfig、それ以外のLevel（Lobby等）のBGMはここのLevel BGMs、Player・Boar・Cage・Gadget固有のSEは各BP / DataAssetで設定します。
// 未設定の項目は再生をスキップします。
/** 各画面とActorで共有するサウンド、音量、距離減衰を設定する。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "REBoarGetch Audio"))
class REBOARGETCH_API UBoarAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// ---- Level BGM（Stage以外） ----
	/** Stage以外のLevelごとのBGMです。Level初期化時に適用し、UI（Stage Select等）の開閉では変えません。 */
	UPROPERTY(Config, EditAnywhere, Category = "Level BGM", meta = (TitleProperty = "Level"))
	TArray<FBoarLevelBGMEntry> LevelBGMs;

	/** LevelBGMsに無いStage以外のLevel（Title等）へ入ったとき、前のBGMを止めます。 */
	UPROPERTY(Config, EditAnywhere, Category = "Level BGM")
	bool bStopBGMInUnlistedLevels = true;

	/** BGM設定のないLevelへ遷移するときのフェードアウト時間（秒）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Level BGM", meta = (ClampMin = "0.0", Units = "s"))
	float UnlistedLevelFadeOutSeconds = 1.0f;

	/** Package名（/Game/Level/L_Lobby等）に一致する設定を返します。 */
	const FBoarLevelBGMEntry* FindLevelBGM(const FString& LevelPackageName) const;

	// ---- UI共通 ----
	/** UI項目へ移動したときのSE。 */
	UPROPERTY(Config, EditAnywhere, Category = "UI")
	TSoftObjectPtr<USoundBase> UISelect;

	/** UI項目を決定したときのSE。 */
	UPROPERTY(Config, EditAnywhere, Category = "UI")
	TSoftObjectPtr<USoundBase> UIConfirm;

	/** UI操作をキャンセルしたときのSE。 */
	UPROPERTY(Config, EditAnywhere, Category = "UI")
	TSoftObjectPtr<USoundBase> UICancel;

	/** 無効なUI操作を通知するSE。 */
	UPROPERTY(Config, EditAnywhere, Category = "UI")
	TSoftObjectPtr<USoundBase> UIReject;

	/** UIを開くときのSE。 */
	UPROPERTY(Config, EditAnywhere, Category = "UI")
	TSoftObjectPtr<USoundBase> UIOpen;

	/** UIを閉じるときのSE。 */
	UPROPERTY(Config, EditAnywhere, Category = "UI")
	TSoftObjectPtr<USoundBase> UIClose;

	// ---- Portal / Stage Entrance ----
	/** Interact入力が受理されたとき（2D）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Portal")
	TSoftObjectPtr<USoundBase> PortalInteractAccepted;

	/** Level遷移演出の開始時（2D）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Portal")
	TSoftObjectPtr<USoundBase> PortalActivate;

	/** 遷移できない状態で操作されたとき（2D）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Portal")
	TSoftObjectPtr<USoundBase> PortalReject;

	// ---- 環境音（EBoarAmbienceType別の共通音源） ----
	/** 水辺で再生する共通環境音。 */
	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceWater;

	/** 森林で再生する共通環境音。 */
	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceForest;

	/** 風を表現する共通環境音。 */
	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceWind;

	/** 風車の周囲で再生する共通環境音。 */
	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceWindmill;

	/** 施設内で再生する共通環境音。 */
	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceFacility;

	/** ホログラム設備で再生する共通環境音。 */
	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceHologram;

	/** 放牧施設で再生する共通環境音。 */
	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceRanch;

	/** Portal / Stage Entranceの待機ループです。 */
	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbiencePortalIdle;

	// ---- 距離減衰 ----
	/** Player・Boar・Cage等、位置付きSEの既定の距離減衰です。各設定で個別に指定した場合はそちらを優先します。 */
	UPROPERTY(Config, EditAnywhere, Category = "Attenuation")
	TSoftObjectPtr<USoundAttenuation> DefaultWorldAttenuation;

	/** 環境音・待機音の既定の距離減衰です。 */
	UPROPERTY(Config, EditAnywhere, Category = "Attenuation")
	TSoftObjectPtr<USoundAttenuation> DefaultAmbienceAttenuation;

	USoundBase* GetUISound(EBoarUISound Sound) const;
	USoundBase* GetPortalSound(EBoarPortalSound Sound) const;
	USoundBase* GetAmbienceSound(EBoarAmbienceType Type) const;
	USoundAttenuation* GetDefaultWorldAttenuation() const { return DefaultWorldAttenuation.LoadSynchronous(); }
	USoundAttenuation* GetDefaultAmbienceAttenuation() const { return DefaultAmbienceAttenuation.LoadSynchronous(); }

	// ---- Mix（音源ごとの音量） ----
	/** 音源ごとの音量倍率です。InportAssets等のGit管理外の音源も、アセットを変えずにここで音量を揃えます。一覧に無い音源は1.0です。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mix", meta = (TitleProperty = "Sound"))
	TArray<FBoarSoundVolumeEntry> SoundVolumes;

	/** SoundVolumesに登録した倍率を返します（未登録は1.0）。 */
	float GetSoundVolume(const USoundBase* Sound) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** サウンド参照をキーに保持する音量設定のキャッシュ。 */
	mutable TMap<FSoftObjectPath, float> SoundVolumeCache;
	/** サウンド別音量のキャッシュを構築済みか。 */
	mutable bool bSoundVolumeCacheBuilt = false;
};
