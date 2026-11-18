#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarGameOverWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UTexture2D;
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
	/** Retry/Lobbyボタンのうち見つからないもの（名前未設定・不在・Button以外）を返します。WidgetTreeはCreateWidget内で構築済みなので、表示前に検証できます。 */
	TArray<FString> GetMissingRequiredWidgets() const;
	/** 現在選択中（フォーカス／ホバー）の項目。0=Retry、1=Lobby。 */
	UFUNCTION(BlueprintPure, Category = "Selection") int32 GetSelectedChoice() const { return SelectedChoice; }
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** 固定名BindWidgetを使わず、Class Defaultsからボタン名を指定します。 */
	UPROPERTY(EditDefaultsOnly, Category = "Widget References") FName RetryButtonWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "Widget References") FName LobbyButtonWidgetName;

	// ---- 選択表示（ボタン本体は透明。背面の枠Image・アイコン・ラベルを選択状態で切り替えます） ----
	/** 各ボタンの枠Image／アイコンImage／ラベルTextBlockの名前。見つからない要素は切り替えません。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection") FName RetryFrameWidgetName = TEXT("RetryFrameImage");
	UPROPERTY(EditDefaultsOnly, Category = "Selection") FName LobbyFrameWidgetName = TEXT("LobbyFrameImage");
	UPROPERTY(EditDefaultsOnly, Category = "Selection") FName RetryIconWidgetName = TEXT("RetryIcon");
	UPROPERTY(EditDefaultsOnly, Category = "Selection") FName LobbyIconWidgetName = TEXT("LobbyIcon");
	UPROPERTY(EditDefaultsOnly, Category = "Selection") FName RetryLabelWidgetName = TEXT("RetryLabel");
	UPROPERTY(EditDefaultsOnly, Category = "Selection") FName LobbyLabelWidgetName = TEXT("LobbyLabel");
	/** 通常（シアン輪郭）／選択中（アンバー発光）の枠テクスチャ。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection") TObjectPtr<UTexture2D> NormalFrameTexture;
	UPROPERTY(EditDefaultsOnly, Category = "Selection") TObjectPtr<UTexture2D> SelectedFrameTexture;
	UPROPERTY(EditDefaultsOnly, Category = "Selection") FLinearColor NormalIconColor = FLinearColor(.55f, .88f, 1.f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "Selection") FLinearColor SelectedIconColor = FLinearColor(1.f, .72f, .05f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "Selection") FLinearColor NormalLabelColor = FLinearColor(.78f, .93f, 1.f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "Selection") FLinearColor SelectedLabelColor = FLinearColor(1.f, .95f, .78f, 1.f);
	/** 選択中の項目の拡大率と、枠の発光の明滅（不透明度の下限・周期秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection", meta = (ClampMin = "1")) float SelectedScale = 1.03f;
	UPROPERTY(EditDefaultsOnly, Category = "Selection", meta = (ClampMin = "0", ClampMax = "1")) float SelectedPulseMinOpacity = .8f;
	UPROPERTY(EditDefaultsOnly, Category = "Selection", meta = (ClampMin = "0.1")) float SelectedPulsePeriod = 1.4f;

	// ---- 表示演出（GAME OVER見出しが少し拡大して落ち着く） ----
	/** 拡大して落ち着く見出し（TextBlockでもImageでも可）。空なら演出しません。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal") FName HeadingWidgetName = TEXT("Heading");
	/** フェードインさせるパネル。空ならWidget全体をフェードします。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal") FName PanelWidgetName = TEXT("GameOverPanel");
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0")) float PanelFadeInTime = .22f;
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0.05")) float HeadingRevealTime = .6f;
	/** 見出しの開始倍率。ここから少し小さめ（HeadingSettleUndershoot）を経て1.0に落ち着きます。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "1")) float HeadingStartScale = 1.4f;
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0", ClampMax = "0.2")) float HeadingSettleUndershoot = .04f;
	/** 見出しを囲むフレーム（任意）。見出しと同じ拡大率・不透明度で動きます。空・未配置なら何もしません。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal") FName HeadingFrameWidgetName = TEXT("HeadingFrame");
	/** ボタン群（任意）。見出しより少し遅れて下からフェードインします。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal") FName ChoicesWidgetName = TEXT("ChoiceGroup");
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0")) float ChoicesDelay = .25f;
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0.05")) float ChoicesRevealTime = .3f;
private:
	struct FChoiceVisual
	{
		TWeakObjectPtr<UButton> Button;
		TWeakObjectPtr<UImage> Frame;
		TWeakObjectPtr<UImage> Icon;
		TWeakObjectPtr<UTextBlock> Label;
		bool bWasHovered = false;
	};
	void ResolveChoice(FChoiceVisual& Out, UButton* Button, FName Frame, FName Icon, FName Label) const;
	void ApplySelection(int32 Index);
	void UpdateSelection();

	UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
	UPROPERTY(Transient) TObjectPtr<UButton> LobbyButton;
	/** 演出対象の見出し（BPからも参照できます）。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Reveal", meta = (AllowPrivateAccess = "true")) TObjectPtr<class UWidget> HeadingWidget;
	UPROPERTY(Transient) TObjectPtr<class UWidget> HeadingFrameWidget;
	UPROPERTY(Transient) TObjectPtr<class UWidget> PanelWidget;
	UPROPERTY(Transient) TObjectPtr<class UWidget> ChoicesWidget;
	FChoiceVisual Choices[2];
	int32 SelectedChoice = 0;
	float RevealAge = 0.f;
	float PulseAge = 0.f;
};
