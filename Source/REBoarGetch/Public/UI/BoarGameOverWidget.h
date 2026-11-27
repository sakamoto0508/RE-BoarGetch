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
	/** リトライを選んだことをControllerへ通知する。 */
	UPROPERTY(BlueprintAssignable)
	FOnGameOverChoice OnRetryRequested;

	/** ロビーへ戻る選択をControllerへ通知する。 */
	UPROPERTY(BlueprintAssignable)
	FOnGameOverChoice OnLobbyRequested;

	UFUNCTION(BlueprintCallable)
	void RequestRetry();

	UFUNCTION(BlueprintCallable)
	void RequestLobby();

	void FocusInitialChoice();
	/** Retry/Lobbyボタンのうち見つからないもの（名前未設定・不在・Button以外）を返します。WidgetTreeはCreateWidget内で構築済みなので、表示前に検証できます。 */
	TArray<FString> GetMissingRequiredWidgets() const;
	/** 現在選択中（フォーカス／ホバー）の項目。0=Retry、1=Lobby。 */
	UFUNCTION(BlueprintPure, Category = "Selection")
	int32 GetSelectedChoice() const { return SelectedChoice; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	/** 決定キー（PlayStation式の○＝右ボタン）。選択中の項目を実行します。Enter・クリックはSlate標準のまま使えます。 */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TArray<FKey> ConfirmKeys = { EKeys::Gamepad_FaceButton_Right };

	/** この画面では使わないキー（PlayStation式の×＝下ボタン）。Slate標準の決定として扱われないよう消費します。 */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TArray<FKey> IgnoredKeys = { EKeys::Gamepad_FaceButton_Bottom };

	/** 固定名BindWidgetを使わず、Class Defaultsからボタン名を指定します。 */
	UPROPERTY(EditDefaultsOnly, Category = "Widget References")
	FName RetryButtonWidgetName;

	/** WBPでロビーに戻るボタンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Widget References")
	FName LobbyButtonWidgetName;

	// ---- 選択表示（ボタン本体は透明。背面の枠Image・アイコン・ラベルを選択状態で切り替えます） ----
	/** 各ボタンの枠Image／アイコンImage／ラベルTextBlockの名前。見つからない要素は切り替えません。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection")
	FName RetryFrameWidgetName = TEXT("RetryFrameImage");

	/** WBPでロビーに戻る選択肢の枠を探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection")
	FName LobbyFrameWidgetName = TEXT("LobbyFrameImage");

	/** WBPでリトライのアイコンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection")
	FName RetryIconWidgetName = TEXT("RetryIcon");

	/** WBPでロビーに戻るアイコンを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection")
	FName LobbyIconWidgetName = TEXT("LobbyIcon");

	/** WBPでリトライのラベルを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection")
	FName RetryLabelWidgetName = TEXT("RetryLabel");

	/** WBPでロビーに戻るラベルを探すWidget名。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection")
	FName LobbyLabelWidgetName = TEXT("LobbyLabel");

	/** 通常（シアン輪郭）／選択中（アンバー発光）の枠テクスチャ。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection")
	TObjectPtr<UTexture2D> NormalFrameTexture;

	/** 選択中の項目を囲む枠画像。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection")
	TObjectPtr<UTexture2D> SelectedFrameTexture;

	/** 通常時の選択肢アイコンの色。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection")
	FLinearColor NormalIconColor = FLinearColor(.55f, .88f, 1.f, 1.f);

	/** 選択中のアイコンの色。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection")
	FLinearColor SelectedIconColor = FLinearColor(1.f, .72f, .05f, 1.f);

	/** 通常時の選択肢ラベルの色。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection")
	FLinearColor NormalLabelColor = FLinearColor(.78f, .93f, 1.f, 1.f);

	/** 選択中のラベルの色。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection")
	FLinearColor SelectedLabelColor = FLinearColor(1.f, .95f, .78f, 1.f);

	/** 選択中の項目の拡大率と、枠の発光の明滅（不透明度の下限・周期秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection", meta = (ClampMin = "1"))
	float SelectedScale = 1.03f;

	/** 選択枠が脈動するときの最小不透明度（0～1）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection", meta = (ClampMin = "0", ClampMax = "1"))
	float SelectedPulseMinOpacity = .8f;

	/** 選択中を示す枠の脈動周期（秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Selection", meta = (ClampMin = "0.1"))
	float SelectedPulsePeriod = 1.4f;

	// ---- 表示演出（GAME OVER見出しが少し拡大して落ち着く） ----
	/** 拡大して落ち着く見出し（TextBlockでもImageでも可）。空なら演出しません。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal")
	FName HeadingWidgetName = TEXT("Heading");

	/** フェードインさせるパネル。空ならWidget全体をフェードします。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal")
	FName PanelWidgetName = TEXT("GameOverPanel");

	/** GameOverパネルのフェードイン時間（秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0"))
	float PanelFadeInTime = .22f;

	/** 見出しが表示されるまでのアニメーション時間（秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0.05"))
	float HeadingRevealTime = .6f;

	/** 見出しの開始倍率。ここから少し小さめ（HeadingSettleUndershoot）を経て1.0に落ち着きます。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "1"))
	float HeadingStartScale = 1.4f;

	/** 見出しを停止位置へ戻す前に加える行き過ぎ量。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0", ClampMax = "0.2"))
	float HeadingSettleUndershoot = .04f;

	/** 見出しを囲むフレーム（任意）。見出しと同じ拡大率・不透明度で動きます。空・未配置なら何もしません。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal")
	FName HeadingFrameWidgetName = TEXT("HeadingFrame");

	/** ボタン群（任意）。見出しより少し遅れて下からフェードインします。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal")
	FName ChoicesWidgetName = TEXT("ChoiceGroup");

	/** 選択肢の表示を始めるまでの待機時間（秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0"))
	float ChoicesDelay = .25f;

	/** 選択肢が現れるアニメーションの時間（秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "Reveal", meta = (ClampMin = "0.05"))
	float ChoicesRevealTime = .3f;

private:
	/** 選択肢1件の表示部品とホバー状態を保持する。 */
	struct FChoiceVisual
	{
		/** 選択肢を決定するボタン。 */
		TWeakObjectPtr<UButton> Button;
		/** 選択状態を示す枠画像。 */
		TWeakObjectPtr<UImage> Frame;
		/** 選択肢の動作を示すアイコン。 */
		TWeakObjectPtr<UImage> Icon;
		/** 選択肢の内容を示すテキスト。 */
		TWeakObjectPtr<UTextBlock> Label;
		/** ホバー開始を検出するために保持する前回の状態。 */
		bool bWasHovered = false;
	};
	void ResolveChoice(FChoiceVisual& Out, UButton* Button, FName Frame, FName Icon, FName Label) const;
	void ApplySelection(int32 Index);
	void UpdateSelection();

	/** 同じステージへの再挑戦を決定するボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> RetryButton;

	/** ロビーへ戻る操作を決定するボタン。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> LobbyButton;

	/** 演出対象の見出し（BPからも参照できます）。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Reveal", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UWidget> HeadingWidget;

	/** GameOver見出しのアニメーション対象となる枠。 */
	UPROPERTY(Transient)
	TObjectPtr<class UWidget> HeadingFrameWidget;

	/** GameOver画面のフェード対象となるパネル。 */
	UPROPERTY(Transient)
	TObjectPtr<class UWidget> PanelWidget;

	/** リトライとロビーの選択肢をまとめる領域。 */
	UPROPERTY(Transient)
	TObjectPtr<class UWidget> ChoicesWidget;

	/** 各選択肢の表示部品と状態の一覧。 */
	FChoiceVisual Choices[2];
	/** 強調表示中の選択肢の添字。 */
	int32 SelectedChoice = 0;
	/** GameOverパネルの表示開始からの実時間（秒）。 */
	float RevealAge = 0.f;
	/** 選択枠の脈動に使う実時間（秒）。 */
	float PulseAge = 0.f;
};
