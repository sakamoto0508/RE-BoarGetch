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

	UPROPERTY(Config, EditAnywhere, Category = "Level BGM")
	TSoftObjectPtr<UWorld> Level;

	/** このLevelのBGM。前のLevelと同じ曲なら頭から再生し直さず継続します。 */
	UPROPERTY(Config, EditAnywhere, Category = "Level BGM")
	TSoftObjectPtr<USoundBase> BGM;

	/** BGMが未設定のとき、前のLevelのBGMを止めずに継続します（Gadget Lab・Archive等）。falseなら止めます。 */
	UPROPERTY(Config, EditAnywhere, Category = "Level BGM")
	bool bKeepCurrentBGMWhenUnset = false;

	UPROPERTY(Config, EditAnywhere, Category = "Level BGM", meta = (ClampMin = "0.0", Units = "s"))
	float FadeInSeconds = 1.0f;

	/** 前のLevelから続く別の曲をフェードアウトする時間です。 */
	UPROPERTY(Config, EditAnywhere, Category = "Level BGM", meta = (ClampMin = "0.0", Units = "s"))
	float PreviousBGMFadeOutSeconds = 1.0f;
};

/**
 * 複数の画面・Actorが共有するSEと距離減衰の設定です（Project Settings > Game > REBoarGetch Audio。DefaultGame.iniに保存）。
 * Stage BGMはStageConfig、それ以外のLevel（Lobby等）のBGMはここのLevel BGMs、Player・Boar・Cage・Gadget固有のSEは各BP / DataAssetで設定します。
 * 未設定の項目は再生をスキップします。
 */
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

	UPROPERTY(Config, EditAnywhere, Category = "Level BGM", meta = (ClampMin = "0.0", Units = "s"))
	float UnlistedLevelFadeOutSeconds = 1.0f;

	/** Package名（/Game/Level/L_Lobby等）に一致する設定を返します。 */
	const FBoarLevelBGMEntry* FindLevelBGM(const FString& LevelPackageName) const;

	// ---- UI共通 ----
	UPROPERTY(Config, EditAnywhere, Category = "UI")
	TSoftObjectPtr<USoundBase> UISelect;

	UPROPERTY(Config, EditAnywhere, Category = "UI")
	TSoftObjectPtr<USoundBase> UIConfirm;

	UPROPERTY(Config, EditAnywhere, Category = "UI")
	TSoftObjectPtr<USoundBase> UICancel;

	UPROPERTY(Config, EditAnywhere, Category = "UI")
	TSoftObjectPtr<USoundBase> UIReject;

	UPROPERTY(Config, EditAnywhere, Category = "UI")
	TSoftObjectPtr<USoundBase> UIOpen;

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
	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceWater;

	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceForest;

	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceWind;

	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceWindmill;

	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceFacility;

	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<USoundBase> AmbienceHologram;

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
};
