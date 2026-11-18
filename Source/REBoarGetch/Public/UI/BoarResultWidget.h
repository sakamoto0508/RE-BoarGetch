#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Stage/StageRunData.h"
#include "BoarResultWidget.generated.h"

class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnResultDismissRequested);

/** ステージ終了時のプレイ結果を表示する基底Widgetです。 */
UCLASS(Blueprintable)
class REBOARGETCH_API UBoarResultWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 確定済みRunの捕獲数・時間・収集結果と、ステージ設定の目標数を表示します。 */
	void InitializeResult(const FStageRunData& Run, int32 TargetCount);
	UPROPERTY(BlueprintReadOnly, Category = "REBoarGetch|Result") FStageRunData ResultData;
	/** NEW個体一覧などをReusable Widgetへ展開するための通知です。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Result") void OnRunResultUpdated();

	/** 任意入力を受け取れるようWidget自身へフォーカスを設定します。 */
	void FocusForDismissInput();

	/** リザルトを閉じてロビーへ戻る入力をPlayerControllerへ通知します。 */
	UPROPERTY(BlueprintAssignable, Category = "REBoarGetch|Result")
	FOnResultDismissRequested OnDismissRequested;

	/** 「STAGE CLEAR!」→捕獲数→特別コインの順表示がすべて終わり、終了入力を受け付ける状態ならtrueです。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Result") bool IsDismissInputAccepted() const { return bDismissInputAccepted; }
	/** 順表示がすべて完了したときにBPへ通知します。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "REBoarGetch|Result") void OnRevealFinished();

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 「STAGE CLEAR!」見出しの登場アニメ。WBPで同名のアニメを作成すると自動で結び付きます。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "REBoarGetch|Result Reveal", meta = (BindWidgetAnimOptional))
	TObjectPtr<class UWidgetAnimation> Anim_StageClear;
	/** 「ボア捕獲数 X / 目標」の登場アニメです。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "REBoarGetch|Result Reveal", meta = (BindWidgetAnimOptional))
	TObjectPtr<class UWidgetAnimation> Anim_CaptureCount;
	/** 「特別コイン +N」の登場アニメです。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "REBoarGetch|Result Reveal", meta = (BindWidgetAnimOptional))
	TObjectPtr<class UWidgetAnimation> Anim_SpecialCoin;
	UPROPERTY(BlueprintReadOnly, Category = "REBoarGetch|Result Reveal", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Text_StageClear;
	UPROPERTY(BlueprintReadOnly, Category = "REBoarGetch|Result Reveal", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Text_CaptureLine;
	UPROPERTY(BlueprintReadOnly, Category = "REBoarGetch|Result Reveal", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Text_CoinLine;
	/** 前の項目のアニメ終了から次の項目を出すまでの間隔（秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Reveal", meta = (ClampMin = "0")) float RevealStepInterval = .35f;
	/** 最後のアニメ終了から終了入力を受け付けるまでの待ち時間（秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Reveal", meta = (ClampMin = "0")) float InputUnlockDelay = .15f;
	/** アニメ未設定時に不透明度だけで表示する時間（秒）。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Reveal", meta = (ClampMin = "0.01")) float FallbackRevealDuration = .3f;
	/** {Captured} と {Target} を置換します。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Reveal") FText CaptureLineFormat = NSLOCTEXT("Result", "CaptureLine", "ボア捕獲数 {Captured} / {Target}");
	/** {Coins} を置換します。今回の挑戦で取得した特別コイン数です。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Reveal") FText CoinLineFormat = NSLOCTEXT("Result", "CoinLine", "特別コイン +{Coins}");
	/** 順表示の完了後（終了入力の受付と同時）にフェードインするWidget名。補足情報と操作ガイドに使います。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Reveal")
	TArray<FName> LateRevealWidgetNames = { TEXT("ClearTimeRow"), TEXT("NewBoarCountRow"), TEXT("Text_ContinueGuide") };
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Reveal|Audio") TObjectPtr<class USoundBase> StageClearSound;
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Reveal|Audio") TObjectPtr<class USoundBase> CaptureCountSound;
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Reveal|Audio") TObjectPtr<class USoundBase> SpecialCoinSound;
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References") FName ClearTimeTextWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References") FName NewBoarCountTextWidgetName;
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References") FName SpecialCoinCountTextWidgetName;
	/** 表示時に、Clear演出の黒フェードを戻して背後のステージをうっすら見せます（背景の暗さ・ぼかしはWBP側）。0以下なら戻しません。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Reveal", meta = (ClampMin = "0")) float WorldFadeInTime = .6f;
	/** 順表示の各ステップ（STAGE CLEAR見出し→捕獲数→特別コイン）で出すWidget名。Text_StageClear等のTextBlockが無いステップで、開始時に隠しアニメ無しならフェードします。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Reveal")
	TArray<FName> RevealStepWidgetNames = { TEXT("HeaderGroup"), TEXT("CaptureRow"), TEXT("DetailRows") };
	/** 新規登録ボアのカード（UBoarResultBoarCard）の名前。今回NEWの個体を先頭から埋め、余りは未登録スロット表示にします。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References")
	TArray<FName> BoarCardWidgetNames = { TEXT("BoarCard0"), TEXT("BoarCard1"), TEXT("BoarCard2") };
	/** 特別コインの「+」と数値。取得0枚は通常色、1枚以上は強調色にします（アンバーは報酬だけに使う）。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References") FName CoinPlusTextWidgetName = TEXT("CoinPlus");
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Format") FLinearColor CoinEarnedColor = FLinearColor(1.f, .58f, .033f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Format") FLinearColor CoinZeroColor = FLinearColor(.72f, .83f, .9f, 1.f);
	/** 今回NEWが無いときにカードの代わりに出すTextBlock（文面はNewBoarListEmptyText）。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References") FName NewBoarEmptyTextWidgetName = TEXT("NewBoarEmptyText");
	/** 終了案内：コントローラー用（PlayStation式）とキーボード・マウス用。最後に使った入力機器の方だけを表示します。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References") FName PadGuideWidgetName = TEXT("PadGuide");
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References") FName KeyboardGuideWidgetName = TEXT("KeyboardGuide");
	/** ステージ表示名（StageConfigのDisplayName）を表示するTextBlockの名前。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References") FName StageNameTextWidgetName = TEXT("StageNameText");
	/** カード番号の書式。{Number}をStageConfig内の個体順（001〜）に置換します。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Format") FText BoarCardNumberFormat = NSLOCTEXT("Result", "CardNumber", "No.{Number}");
	/** 今回NEWになったBoarの名前一覧を表示するTextBlockの名前。確定済みRunのNewBoarUniqueIdsだけを表示し、Widgetでは再判定しません。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Widget References") FName NewBoarListTextWidgetName;
	/** NEW一覧の1行。{Name}を図鑑名（StageConfigのEncyclopediaName。未設定ならBoarUniqueId）に置換します。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Format") FText NewBoarEntryFormat = NSLOCTEXT("Result", "NewBoarEntry", "NEW  {Name}");
	/** 今回NEWの個体がいないときのNEW一覧の文面。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Format") FText NewBoarListEmptyText = NSLOCTEXT("Result", "NewBoarEmpty", "今回の新規登録はありません");
	/** クリアタイムの書式。{Minutes}（2桁）、{Seconds}（2桁）、{Hundredths}（2桁）を置換します。 */
	UPROPERTY(EditDefaultsOnly, Category = "REBoarGetch|Result Format") FText ClearTimeFormat = NSLOCTEXT("Result", "ClearTime", "{Minutes}:{Seconds}.{Hundredths}");
	/** 終了入力の受付開始と同時にループ再生する案内の点滅アニメ（任意）。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "REBoarGetch|Result Reveal", meta = (BindWidgetAnimOptional))
	TObjectPtr<class UWidgetAnimation> Anim_ContinueGuide;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** WBP内で現在の捕獲数を表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Result Widget References")
	FName CapturedCountTextWidgetName;

	/** WBP内で目標捕獲数を表示するTextBlockの名前を指定します。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|Result Widget References")
	FName TargetCountTextWidgetName;

private:
	void ResolveWidgetReferences();
	FText FormatClearTime(float Seconds) const;
	FText BuildNewBoarList(const FStageRunData& Run) const;
	/** Runと同じStageIdのStageConfig（無ければnull）。 */
	const class UStageConfig* FindRunStage(const FStageRunData& Run) const;
	void FillBoarCards(const FStageRunData& Run) const;
	/** 入力機器に合わせて終了案内を切り替えます（同じ機器なら何もしません）。 */
	void SetGuideDevice(bool bGamepad);
	int32 GuideDevice = INDEX_NONE;
	/** ステップIの表示対象（Text_*があればそれ、無ければRevealStepWidgetNames[I]）。 */
	UWidget* GetRevealStepWidget(int32 Index) const;
	FReply RequestDismiss();
	void StartReveal();
	void StartRevealStep(int32 Index);
	bool bDismissInputAccepted = false;
	bool bRevealRunning = false;
	float LateRevealAge = -1.f;
	float RevealAge = 0.f;
	int32 NextRevealStep = 0;
	float RevealStepStart[3] = { 0.f, 0.f, 0.f };
	float RevealStepDuration[3] = { 0.f, 0.f, 0.f };
	float RevealUnlockTime = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CapturedCountText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TargetCountText;

};
