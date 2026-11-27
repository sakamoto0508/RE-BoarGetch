#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ACFCameraTypes.h"
#include "BoarGameplayCameraComponent.generated.h"

class APawn;
class APlayerController;
class UACFCameraPreset;
class UACFCameraSubsystem;

/**
 * 通常TPSカメラをAction Camera Framework（ACF）で動かすための、ローカルプレイヤー単位のRequest所有者です。
 * ABoarPlayerControllerに付き、ControllerのBeginPlay〜EndPlayの間だけ通常カメラのRequestを1つだけ保持します。
 *
 * - ACFが無効、Preset未設定、Pawn未Possessのときは何もせず、CharacterのSpringArm / FollowCameraがそのまま映ります（Fallback）。
 * - Possessが変わると追従対象だけを差し替えます（Requestは作り直さない）。
 * - EndPlay（OpenLevel / Retry / PIE終了）でカット解除します。ACFのSubsystemはLocalPlayerと共にLevelをまたいで残るため、
 *   ここで解除しないと古いRequestが次のLevelへ残ります。Instigator = Controllerも設定し、EndPlayを経ない破棄でも自動解除されます。
 * - 共有Preset（DataAsset）は読むだけで、実行中に書き換えません。
 *
 * 比較用の切替は Console Variable `Boar.Camera.ACF`（-1 = Componentの既定値 / 0 = SpringArm / 1 = ACF）です。
 * Level遷移後も同じ値が使われます。`Boar.Camera.Dump` で現在のカメラとRequestの状態をLogへ出力します。
 *
 * Damping検証用（Shipping以外）。どれもアセットを書き換えず、-1でアセットの値へ戻ります。
 * - `Boar.Camera.Test.PivotDamping <秒>`：ACF既存のRuntime Tuning（Follow Damping）で通常カメラのPivot Dampingを変更
 * - `Boar.Camera.Test.RotationDamping <秒>` / `Boar.Camera.Test.ReturnDamping <秒>`：Presetの一時複製（Transient）の値を変えて差し替え
 * - `Boar.Camera.Test.Reset`：上の3つをすべて-1へ戻す
 * - `Boar.Camera.Trace.Start [ラベル]` / `Boar.Camera.Trace.Stop`：毎フレームのPivot／回転の誤差をCSV（Saved/BoarCameraTrace）へ記録し、停止時に要約をLogへ出力
 */
UCLASS(ClassGroup=(Camera), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UBoarGameplayCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBoarGameplayCameraComponent();

	/** 現在ACFの通常カメラを使う設定か（Console Variableの上書きを含む）を返します。 */
	UFUNCTION(BlueprintPure, Category = "Camera|ACF")
	bool IsACFCameraEnabled() const;

	/** 通常カメラのRequestが有効か（ACFが実際に通常カメラを出しているか）を返します。 */
	UFUNCTION(BlueprintPure, Category = "Camera|ACF")
	bool HasActiveACFRequest() const;

	UFUNCTION(BlueprintPure, Category = "Camera|ACF")
	FACFCameraRequestHandle GetACFRequestHandle() const { return RequestHandle; }

	/** 設定（有効／無効、Preset、Possess中のPawn）に合わせてRequestを発行・更新・解除します。何度呼んでも二重Requestになりません。 */
	void RefreshCameraRequest();

	/** 現在の状態（Request、ACF出力、ACF／SpringArmそれぞれのカメラ姿勢）を1行ずつLogへ出します。 */
	void DumpCameraState() const;

	/** Damping検証用の毎フレーム記録を開始／停止します（Shipping以外）。 */
	void StartTrace(const FString& Label);
	void StopTrace();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Console Variableが-1のときに使う既定値です。falseなら従来のSpringArmカメラだけを使います。 */
	UPROPERTY(EditDefaultsOnly, Category = "Camera|ACF")
	bool bUseACFCameraByDefault = true;

	/** 通常TPSカメラのPreset（Orbital Follow + Collision）。未設定ならSpringArmカメラへFallbackします。 */
	UPROPERTY(EditDefaultsOnly, Category = "Camera|ACF")
	TObjectPtr<UACFCameraPreset> NormalCameraPreset;

	/** ACFのRequest Priorityです。Lock-On等の後続カメラはこれより高いPriorityで重ねます。 */
	UPROPERTY(EditDefaultsOnly, Category = "Camera|ACF")
	int32 RequestPriority = 0;

	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	APlayerController* GetOwningController() const;
	UACFCameraSubsystem* GetCameraSubsystem() const;
	void ReleaseRequest(bool bCut);
	void IssueRequest(UACFCameraSubsystem& Cameras, APawn& Pawn, UACFCameraPreset* Preset);
	/** 通常はNormalCameraPreset。Damping検証値が設定されていれば、その一時複製（Transient、保存されない）を返します。 */
	UACFCameraPreset* GetEffectivePreset();
	/** Pivot Damping検証値をACFのRuntime Tuningで適用／解除します。 */
	void ApplyPivotTuning();
	void HandleWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	FACFCameraRequestHandle RequestHandle;
	/** 現在のRequestに渡したPreset（通常Presetか検証用の一時複製）。差し替えの検出に使います。 */
	UPROPERTY(Transient)
	TObjectPtr<UACFCameraPreset> RequestedPreset;
	/** Rotation／Return Damping検証用の一時複製です。アセットとしては保存されません。 */
	UPROPERTY(Transient)
	TObjectPtr<UACFCameraPreset> TestPreset;
	float TestPresetRotationDamping = -1.f;
	float TestPresetReturnDamping = -1.f;

	/** 毎フレーム記録（Shipping以外）の状態です。 */
	struct FTraceState;
	TSharedPtr<FTraceState> Trace;
	/** 現在のRequestが追従しているPawnです。Possess変更の検出に使います。 */
	TWeakObjectPtr<APawn> FollowedPawn;
	bool bReportedMissingPreset = false;
	bool bBegunPlay = false;
};
