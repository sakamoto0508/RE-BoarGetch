#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoarPresentationDirector.generated.h"

class ABoarPlayerCharacter;
class ABoarPlayerController;
class USoundBase;

/**
 * Stage終了演出（Stage Clear / Game Over）の演出側の基底Actorです。
 * 演出の順序・タイミング・使用Asset（Montage / SE / VFX等）は派生Blueprintの OnPresentationStarted で組み立て、
 * 完了時に FinishPresentation を呼びます。開始・タイムアウト・中断・GameModeへの完了通知は所有Component（C++）が管理し、
 * このActorはGameplayの判定や保存を行いません。Transientで生成され、Level遷移・中断時のEndPlayで変更した見た目を元に戻します。
 * BP内のDelay / Timelineはヒットストップ（Global Time Dilation）の影響を受けない実時間で進みます。
 */
UCLASS(Abstract, Blueprintable, NotPlaceable)
class REBOARGETCH_API ABoarPresentationDirector : public AActor
{
	GENERATED_BODY()
public:
	ABoarPresentationDirector();

	/** 所有Componentから呼ばれます。BPの OnPresentationStarted を発行します。 */
	void BeginPresentation(ABoarPlayerController* InController, FSimpleDelegate InOnFinished);

	/** 演出完了をC++の進行制御へ報告します（2回目以降は無視）。 */
	UFUNCTION(BlueprintCallable, Category = "Presentation")
	void FinishPresentation();

	UFUNCTION(BlueprintPure, Category = "Presentation") bool IsPresentationFinished() const { return bFinished; }
	UFUNCTION(BlueprintPure, Category = "Presentation") ABoarPlayerController* GetPresentationController() const;
	UFUNCTION(BlueprintPure, Category = "Presentation") ABoarPlayerCharacter* GetPresentationPlayer() const;
	/** 演出開始からの実時間（秒）。 */
	UFUNCTION(BlueprintPure, Category = "Presentation") float GetPresentationAge() const { return Age; }

	// ---- Audio（BoarAudioManagerSubsystem経由。音量設定は変更しません） ----
	UFUNCTION(BlueprintCallable, Category = "Presentation|Audio", meta = (AdvancedDisplay = "Pitch"))
	void PlayPresentationSound(USoundBase* Sound, float Pitch = 1.f);
	/** ジングル等をBGMチャンネルで1回再生します（次のBGMで置き換わります）。 */
	UFUNCTION(BlueprintCallable, Category = "Presentation|Audio")
	void PlayPresentationBGM(USoundBase* Music, float FadeInTime = 0.f);
	UFUNCTION(BlueprintCallable, Category = "Presentation|Audio")
	void StopStageBGM(float FadeOutTime = .5f);

	/** 通常HUDの表示（演出中だけ隠す・フェードする）。 */
	UFUNCTION(BlueprintCallable, Category = "Presentation|UI")
	void SetPlayerHUDHidden(bool bHideHUD);

protected:
	/** 演出開始。ここで演出の順序とタイミングを組み立て、最後に FinishPresentation を呼びます。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Presentation")
	void OnPresentationStarted();

	virtual void Tick(float DeltaSeconds) override;
	/** 実時間の経過（秒）。派生クラスの毎フレーム処理に使います。 */
	virtual void TickPresentation(float RealDeltaSeconds) {}

private:
	TWeakObjectPtr<ABoarPlayerController> Controller;
	FSimpleDelegate OnFinished;
	bool bFinished = false;
	float Age = 0.f;
	double LastRealTime = 0.0;
};
