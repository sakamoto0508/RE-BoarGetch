#pragma once

#include "CoreMinimal.h"
#include "BoarAudioTypes.generated.h"

class USoundBase;
class USoundAttenuation;

/**
 * UI共通SEの論理名です。音源はProject Settings > Game > REBoarGetch Audioで設定します。
 * 画面ごとに音源参照をコピーせず、BoarAudioManagerSubsystem::PlayUISoundから鳴らします。
 */
UENUM(BlueprintType)
enum class EBoarUISound : uint8
{
	/** 選択項目の変更（Stage Selectの左右切替など）。 */
	Select,
	/** 決定が受理されたとき。 */
	Confirm,
	/** 戻る・キャンセルが成立したとき。 */
	Cancel,
	/** 未解放Stageなど、操作が拒否されたとき。 */
	Reject,
	/** 画面・メニューを開いたとき。 */
	Open,
	/** 画面・メニューを閉じたとき。 */
	Close
};

/** Lobby系Portal / Stage Entranceの共通SEです。 */
UENUM(BlueprintType)
enum class EBoarPortalSound : uint8
{
	/** Interact入力が受理され、PortalがUIを開く・起動処理へ進んだとき。 */
	InteractAccepted,
	/** Level遷移演出を開始したとき（遷移確定）。 */
	Activate,
	/** 遷移先が無効などで操作を受け付けなかったとき。 */
	Reject
};

/**
 * 環境音の種類です。Customは各Emitter / Componentに直接設定した音源だけを使います。
 * それ以外はProject Settingsの共通音源を使うため、同じ種類の音源を一か所で差し替えられます。
 */
UENUM(BlueprintType)
enum class EBoarAmbienceType : uint8
{
	Custom,
	Water,
	Forest,
	Wind,
	Windmill,
	Facility,
	Hologram,
	Ranch,
	/** Portal・Stage Entranceの待機音。 */
	PortalIdle
};

/** Playerの移動・動作SEです（BP_BoarPlayerCharacterのPlayer|Audioで設定）。未設定のSEは再生しません。 */
USTRUCT(BlueprintType)
struct REBOARGETCH_API FBoarPlayerAudioSettings
{
	GENERATED_BODY()

	/** AnimNotify_BoarFootstepから鳴らす足音です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Movement")
	TObjectPtr<USoundBase> Footstep;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Movement", meta = (ClampMin = "0.0"))
	float FootstepVolume = 0.6f;

	/** 地上からのJump開始です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Movement")
	TObjectPtr<USoundBase> Jump;

	/** 空中での二段ジャンプです。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Movement")
	TObjectPtr<USoundBase> DoubleJump;

	/** 通常の着地です。HardLandと排他で、1回の着地で片方だけ鳴ります。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Movement")
	TObjectPtr<USoundBase> Land;

	/** 高所からの強い着地です。未設定の場合はLandを鳴らします。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Movement")
	TObjectPtr<USoundBase> HardLand;

	/** この落下速度（cm/s）未満の着地は段差の降下とみなし、着地音を鳴らしません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinLandSpeed = 250.f;

	/** この落下速度（cm/s）以上の着地をHardLandとして扱います。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float HardLandSpeed = 1100.f;

	/** Dash開始です（スタミナ切れで拒否された入力では鳴りません）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Movement")
	TObjectPtr<USoundBase> DashStart;

	/** Player SEの距離減衰です。未設定ならProject SettingsのDefault World Attenuationを使います。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundAttenuation> Attenuation;
};

/**
 * Boar種別ごとのSEです（DA_BoarDataAssetの種別ごとのAudio）。
 * 状態遷移・成立イベントでだけ鳴らし、毎フレームの状態確認では鳴らしません。
 */
USTRUCT(BlueprintType)
struct REBOARGETCH_API FBoarArchetypeAudioSettings
{
	GENERATED_BODY()

	/** Playerを新たに認識した瞬間の警戒・鳴き声です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Perception")
	TObjectPtr<USoundBase> Alert;

	/** 警戒音の最短再生間隔です。見失い→再発見の繰り返しで鳴り続けないようにします。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Perception", meta = (ClampMin = "0.0", Units = "s"))
	float AlertCooldown = 4.f;

	/** 逃走状態へ入った瞬間です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Escape")
	TObjectPtr<USoundBase> EscapeStart;

	/** 逃走Stateを抜けてからこの秒数以内の再開（逃走先への到着→再選択など）は同じ逃走の継続とみなし、EscapeStartを鳴らしません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Escape", meta = (ClampMin = "0.0", Units = "s"))
	float EscapeStartMinGap = 2.0f;

	/** 逃走中の大きな切り返し（踏ん張り→停止旋回）開始です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Escape")
	TObjectPtr<USoundBase> PivotStart;

	/** 突進・檻攻撃の予兆（ため）開始です。Redでは回避判断に使う重要音です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Attack")
	TObjectPtr<USoundBase> AttackTelegraph;

	/** 予兆音の音量倍率です。BGM・環境音に埋もれる場合に上げます。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Attack", meta = (ClampMin = "0.0"))
	float AttackTelegraphVolume = 1.f;

	/** 予兆音を2D（距離減衰なし）で鳴らします。画面外からの突進も聞き逃さないようにする場合に使います。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Attack")
	bool bAttackTelegraphAs2D = false;

	/** 突進開始です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Attack")
	TObjectPtr<USoundBase> ChargeStart;

	/** 突進の終了（停止・衝突・後退開始）です。Player命中音はPlayerHitReactionの被弾音、檻命中音はCageのHit音を使います。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Attack")
	TObjectPtr<USoundBase> ChargeEnd;

	/** スタミナが0になり、疲労停止が確定した瞬間です（Blueの捕獲チャンス）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Stamina")
	TObjectPtr<USoundBase> StaminaExhausted;

	/** 疲労停止中だけBoarに追従して鳴るループ音（息切れ等）です。回復・捕獲・Stage終了で停止します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Stamina")
	TObjectPtr<USoundBase> TiredLoop;

	/** スタミナが回復し、逃走を再開できる状態に戻った瞬間です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Stamina")
	TObjectPtr<USoundBase> StaminaRecovered;

	/** AnimNotify_BoarFootstepから鳴らす足音です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Movement")
	TObjectPtr<USoundBase> Footstep;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Movement", meta = (ClampMin = "0.0"))
	float FootstepVolume = 0.5f;

	/** Boar SEの距離減衰です。未設定ならProject SettingsのDefault World Attenuationを使います。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundAttenuation> Attenuation;
};
