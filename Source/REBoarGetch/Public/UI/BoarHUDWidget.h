#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarHUDWidget.generated.h"

class UTextBlock;
class UImage;
class UProgressBar;
class UWidget;
class AGadgetBase;

/** ゲームプレイ中にHPや捕獲数を常時表示するHUDの基底Widgetです。 */
UCLASS(Abstract, Blueprintable)
class REBOARGETCH_API UBoarHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 現在の捕獲数とステージ目標数を表示へ反映します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|HUD")
	void UpdateCaptureCount(int32 CurrentCount, int32 TargetCount);

	/** 現在HPと最大HPを表示へ反映します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|HUD")
	void UpdateHealth(float CurrentHealth, float MaxHealth);

	/**
	 * ダッシュスタミナを表示へ反映します。値の計算はCharacter側が行い、ここでは割合・色だけを決めます。
	 * ゲージの位置（プレイヤー横への追従）と表示・非表示のフェードはNativeTickが担当します。
	 * @param bDashLocked スタミナ切れでDash禁止中なら枯渇色で点滅表示します。
	 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|HUD")
	void UpdateStamina(float CurrentStamina, float MaxStamina, bool bDashLocked);

	/** 4つの装備内容と現在選択中の枠を表示へ反映します。 */
	void UpdateGadgetSlots(const TArray<TSubclassOf<AGadgetBase>>& GadgetSlots, int32 SelectedSlotIndex);

protected:
	virtual void NativeConstruct() override;
	/** スタミナゲージをプレイヤーの画面上の位置へ追従させ、透明度を更新します。スタミナの計算は行いません。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** WBP内で現在捕獲数を表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	FName CapturedCountTextWidgetName;

	/** WBP内で目標捕獲数を表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	FName TargetCountTextWidgetName;

	/** WBP内で現在HPを表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	FName CurrentHealthTextWidgetName;

	/** WBP内で最大HPを表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	FName MaxHealthTextWidgetName;

	/** WBP内でスタミナゲージを表示するProgressBarの名前です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	FName StaminaBarWidgetName = TEXT("StaminaBar");

	/** 通常時のゲージ色です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Stamina")
	FLinearColor StaminaNormalColor = FLinearColor(0.30f, 0.85f, 1.0f, 1.0f);

	/** 残量が少ないときのゲージ色です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Stamina")
	FLinearColor StaminaLowColor = FLinearColor(1.0f, 0.55f, 0.025f, 1.0f);

	/** スタミナ切れでDash禁止中のゲージ色です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Stamina")
	FLinearColor StaminaExhaustedColor = FLinearColor(1.0f, 0.18f, 0.12f, 1.0f);

	/** この割合未満を「残量が少ない」表示にします。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Stamina", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StaminaLowRatio = 0.25f;

	/**
	 * ゲージを表示する位置の、プレイヤー中心からのオフセット(cm)です。X=カメラの右方向（負の値で左側）、Y=上方向。
	 * ワールド空間で指定するため、カメラ距離が変わってもキャラクターとの見た目の位置関係が保たれます。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Stamina")
	FVector2D StaminaGaugeWorldOffset = FVector2D(-55.0f, 15.0f);

	/** 満タンになってからゲージを隠し始めるまでの秒数です。0未満にすると常に表示します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Stamina")
	float StaminaHideDelayWhenFull = 1.0f;

	/** 表示・非表示のフェード速度(透明度/秒)です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Stamina", meta = (ClampMin = "0.1"))
	float StaminaFadeSpeed = 5.0f;

	/** スタミナ切れ中の点滅の速さ(回/秒)です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Stamina", meta = (ClampMin = "0.0"))
	float StaminaExhaustedBlinkRate = 3.0f;

	/** Slot 1～4のガジェット画像用Image名です。要素順がスロット番号に対応します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	TArray<FName> GadgetIconWidgetNames;

	/** Slot 1～4の選択アウトライン用Widget名です。ImageやBorderなど任意のWidgetを指定できます。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|HUD Widget References")
	TArray<FName> GadgetSelectionWidgetNames;

private:
	// 名前設定を実Widgetへ解決します。未配置の表示部品は更新時にスキップします。
	void ResolveWidgetReferences();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CapturedCountText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TargetCountText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CurrentHealthText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MaxHealthText;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> StaminaBarWidget;

	/** 最後に通知されたスタミナ表示状態です。NativeTickのフェード判定に使います。 */
	bool bStaminaFull = true;
	bool bStaminaLocked = false;
	/** 満タンになってからの経過秒数です。生成直後は満タン扱いで非表示から始めます。 */
	float StaminaFullElapsed = 1000.0f;
	/** 現在のゲージ透明度（フェード用）です。 */
	float StaminaGaugeOpacity = 0.0f;
	/** 枯渇点滅の位相です。 */
	float StaminaBlinkTime = 0.0f;

	/** プレイヤー横の画面位置を求め、ゲージのCanvasSlotへ反映します。投影できない場合はfalseです。 */
	bool UpdateStaminaGaugePosition();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> GadgetIconWidgets;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> GadgetSelectionWidgets;
};
