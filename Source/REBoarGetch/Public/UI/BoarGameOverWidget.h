#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarGameOverWidget.generated.h"

class UButton;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameOverChoice);

/** GameOver専用のRetry/Lobby選択です。既存Resultの任意入力とは分離します。 */
UCLASS(Blueprintable)
class REBOARGETCH_API UBoarGameOverWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	// 選択の意思だけをControllerへ渡し、レベル遷移や終了状態の管理は外側で行います。
	UPROPERTY(BlueprintAssignable) FOnGameOverChoice OnRetryRequested;
	UPROPERTY(BlueprintAssignable) FOnGameOverChoice OnLobbyRequested;
	UFUNCTION(BlueprintCallable) void RequestRetry();
	UFUNCTION(BlueprintCallable) void RequestLobby();
	void FocusInitialChoice();
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** 固定名BindWidgetを使わず、Class Defaultsからボタン名を指定します。 */
	UPROPERTY(EditDefaultsOnly, Category = "Widget References") FName RetryButtonWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Widget References") FName LobbyButtonWidgetName;

	// ---- 表示演出（GAME OVER見出しが少し拡大して落ち着く＋シアンの細いフレーム） ----
	/** 拡大して落ち着く見出し（既存のHeading TextBlock）。空なら演出しません。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal") FName HeadingWidgetName = TEXT("Heading");
	/** フェードインさせるパネル（既存のGameOverPanel）。空ならWidget全体をフェードします。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal") FName PanelWidgetName = TEXT("GameOverPanel");
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0")) float PanelFadeInTime = .22f;
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0.05")) float HeadingRevealTime = .6f;
	/** 見出しの開始倍率。ここから少し小さめ（HeadingSettleUndershoot）を経て1.0に落ち着きます。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "1")) float HeadingStartScale = 1.4f;
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0", ClampMax = "0.2")) float HeadingSettleUndershoot = .04f;
	/** 見出しを囲むシアンの細いフレーム（WBPのImage/Border）。見出しと同じ拡大率・不透明度で動きます。空・未配置なら枠は出しません。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal") FName HeadingFrameWidgetName = TEXT("HeadingFrame");
private:
	UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
	UPROPERTY(Transient) TObjectPtr<UButton> LobbyButton;
	/** 演出対象の見出し（BPからも参照できます）。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Reveal", meta = (AllowPrivateAccess = "true")) TObjectPtr<class UWidget> HeadingWidget;
	UPROPERTY(Transient) TObjectPtr<class UWidget> HeadingFrameWidget;
	UPROPERTY(Transient) TObjectPtr<class UWidget> PanelWidget;
	float RevealAge = 0.f;
};
