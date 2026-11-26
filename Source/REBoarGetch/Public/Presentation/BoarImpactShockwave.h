#pragma once

#include "CoreMinimal.h"

class APlayerCameraManager;
class UNiagaraComponent;
class UNiagaraSystem;
class UWorld;

/**
 * 突進の命中位置に出す衝撃波（NS_PlayerHitImpact）の再生パラメーターです。
 * プレイヤー被弾（UPlayerHitReactionComponent）と檻への攻撃命中（UCageVisualComponent）で同じSystemを共有し、
 * 大きさ・色・本数などだけを呼び出し側で変えます。負の値の項目はNiagara側のUser Parameter既定値のままにします。
 */
struct FBoarImpactShockwaveParams
{
	/** User.Scale：全体の大きさ。 */
	float Scale = 1.0f;

	/** User.Color：衝撃リング・放射線の色。 */
	FLinearColor Color = FLinearColor(1.0f, 0.93f, 0.62f, 1.0f);

	/** リング面の法線を突進方向からカメラへ傾ける割合（0 = 突進方向に完全に垂直）。 */
	float RingCameraBias = 0.35f;

	/** trueならUser.SparkColorをSparkColorで上書きします。 */
	bool bOverrideSparkColor = false;
	FLinearColor SparkColor = FLinearColor(1.0f, 0.66f, 0.26f, 1.0f);

	/** User.StreakCount / User.ParticleCount（負ならNiagara既定値）。 */
	int32 StreakCount = -1;
	int32 ParticleCount = -1;

	/** User.MaxRadius（cm）/ User.Lifetime（秒）（負ならNiagara既定値）。 */
	float MaxRadius = -1.0f;
	float Lifetime = -1.0f;
};

namespace BoarImpactShockwave
{
	/**
	 * 衝撃波をLocationに1回だけ再生します（AutoRelease）。Directionは押し込む向き（水平）、Cameraは向きの補正に使います（nullなら補正なし）。
	 * User Parameterを入れてからActivateするため、初回Spawnで向き・色が確定します。失敗時はnullptrを返します。
	 */
	REBOARGETCH_API UNiagaraComponent* Spawn(UWorld* World, UNiagaraSystem* System, const FVector& Location, const FVector& Direction,
		const APlayerCameraManager* Camera, const FBoarImpactShockwaveParams& Params);
}
