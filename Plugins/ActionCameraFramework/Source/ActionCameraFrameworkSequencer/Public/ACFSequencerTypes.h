#pragma once

#include "CoreMinimal.h"
#include "ACFCameraTypes.h"
#include "ACFSequencerTypes.generated.h"

class AActor;
class ALevelSequenceActor;
class APlayerController;

/** あるローカルプレイヤーの画面を、Level Sequence の Camera Cut がどの段階で握っているか。 */
UENUM(BlueprintType)
enum class EACFSequencerCameraPhase : uint8
{
	/** Sequencer は画面を握っていない（ゲームプレイカメラ / ACF が映っている）。 */
	None,
	/** PCM の ViewTarget Blend で、ゲームプレイカメラ → Sequencer のカメラへ移行中（Camera Cut の Ease In）。 */
	BlendingIn,
	/** Sequencer のカメラが映っている（Sequencer のカメラ同士の切替・ブレンドを含む）。 */
	InControl,
	/** PCM の ViewTarget Blend で、Sequencer のカメラ → ゲームプレイカメラへ戻っている（Camera Cut の Ease Out）。 */
	BlendingOut,
};

/** Sequencer から戻るとき、ControlRotation（と Orbital Follow の向き）をどうするか。 */
UENUM(BlueprintType)
enum class EACFSequencerControlRotationPolicy : uint8
{
	/** 何もしない（既定）。ゲームプレイカメラは Sequencer 開始前の向きを保ったまま戻る。 */
	Keep,
	/**
	 * Sequencer の最後のカメラの向き（Roll は 0、Pitch は PCM の制限内）を ControlRotation に入れ、
	 * Orbital Follow（Input Source = Camera Input）の向きも合わせる。演出の最後の画角からそのまま操作に戻れる。
	 */
	MatchSequencerCamera,
};

/** Sequencer からゲームプレイカメラへ、どう戻ったか。 */
UENUM(BlueprintType)
enum class EACFSequencerReturnKind : uint8
{
	/** Sequencer 自身のブレンド（Camera Cut の Ease Out = PCM の ViewTarget Blend）。ACF は追加のブレンドをしない。 */
	SequencerBlend,
	/** Sequencer はカットで戻ったので、ACF が「Sequencer の最終姿勢 → ACF カメラ」を Return Blend で補間した。 */
	AcfReturnBlend,
	/** カットで戻った（Return Blend が Cut、ACF の要求が無い、自動処理が無効、PC / PCM の作り直しなど）。 */
	Cut,
};

/**
 * Sequencer と ACF の連携の設定。既定値は Project Settings（Action Camera Framework - Sequencer）、
 * プレイヤー単位・Level Sequence Actor 単位で上書きできる（優先: Sequence Actor > プレイヤー > Project Settings）。
 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORKSEQUENCER_API FACFSequencerCameraOptions
{
	GENERATED_BODY()

	FACFSequencerCameraOptions();

	/** OFF にすると、この対象については ACF は何もしない（ACF は Sequencer のカメラの上にそのまま出力され、復帰処理も行わない）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequencer")
	bool bEnabled = true;

	/**
	 * Sequencer が**カットで**ゲームプレイカメラへ戻ったときに、ACF が補うブレンド（Sequencer の最終姿勢 → ACF カメラ）。
	 * Sequencer が自分でブレンドして戻る場合（Camera Cut の Ease Out）は使わない（二重ブレンドにしない）。
	 * Cut にすると、Sequencer のカットをそのまま尊重する。既定 EaseInOut 0.5 秒。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequencer")
	FACFCameraBlendSettings ReturnBlend;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequencer")
	EACFSequencerControlRotationPolicy ControlRotationPolicy = EACFSequencerControlRotationPolicy::Keep;

	/**
	 * Camera Cut が無くても、この Level Sequence の再生中（一時停止を含む）は ACF の出力を抑制する。
	 * プレイヤー自身の Camera Component を Sequencer のトラックで直接動かす演出向け。既定 OFF。
	 * Level Sequence Actor 単位の設定（Set ACF Sequencer Options For Sequence）でだけ意味を持つ。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequencer")
	bool bSuppressOutputWhilePlaying = false;

	/** bSuppressOutputWhilePlaying の抑制に入る / 戻るときの Weight のフェード秒数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequencer", meta = (ClampMin = "0.0", EditCondition = "bSuppressOutputWhilePlaying"))
	float OutputBlendTime = 0.5f;
};

/** あるローカルプレイヤーについての、Sequencer とカメラの現在の関係（デバッグ / UI / ゲームロジック用）。 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORKSEQUENCER_API FACFSequencerCameraState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sequencer")
	EACFSequencerCameraPhase Phase = EACFSequencerCameraPhase::None;

	/** Phase != None。 */
	UPROPERTY(BlueprintReadOnly, Category = "Sequencer")
	bool bSequencerControlsCamera = false;

	/** このプレイヤーを対象に再生中（一時停止を含む）の Level Sequence の数。Camera Cut の有無は問わない。 */
	UPROPERTY(BlueprintReadOnly, Category = "Sequencer")
	int32 NumActiveSequences = 0;

	/** 画面を握っている（または最後に握っていた）Level Sequence Actor。 */
	UPROPERTY(BlueprintReadOnly, Category = "Sequencer")
	TObjectPtr<ALevelSequenceActor> SequenceActor = nullptr;

	/** Sequencer が ViewTarget にしているカメラ。 */
	UPROPERTY(BlueprintReadOnly, Category = "Sequencer")
	TObjectPtr<AActor> SequencerViewTarget = nullptr;

	/** 戻り先のゲームプレイの ViewTarget（通常は Pawn）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Sequencer")
	TObjectPtr<AActor> GameplayViewTarget = nullptr;

	/** このモジュールが ACF の出力を抑制しているか（bSuppressOutputWhilePlaying の Sequence が再生中）。Camera Cut では抑制は使わない。 */
	UPROPERTY(BlueprintReadOnly, Category = "Sequencer")
	bool bSuppressingOutput = false;

	/** ACF の結果が画面に出ていないか（Sequencer のカメラだけが映っている間。ACF は裏で評価を続けている）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Sequencer")
	bool bOutputBypassed = false;
};
