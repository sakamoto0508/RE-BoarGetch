#include "UI/BoarResultWidget.h"

#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Input/Reply.h"
#include "Animation/WidgetAnimation.h"
#include "Audio/BoarAudioManagerSubsystem.h"

namespace
{
	template <typename WidgetType>
	WidgetType* ResolveNamedWidget(UWidgetTree* WidgetTree, const FName WidgetName)
	{
		if (!WidgetTree || WidgetName.IsNone())
		{
			return nullptr;
		}

		return Cast<WidgetType>(WidgetTree->FindWidget(WidgetName));
	}
}

void UBoarResultWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// WBP未設定・生成失敗時だけ使用する最小表示。既存WBPのTreeは変更しません。
	if (GetClass() != StaticClass() || !WidgetTree || WidgetTree->RootWidget) return;
	auto* Panel = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Panel;
	auto* Guide = WidgetTree->ConstructWidget<UTextBlock>();
	Guide->SetText(FText::FromString(TEXT("Stage Clear - Press any button to return to Lobby")));
	Panel->AddChildToVerticalBox(Guide);
	CapturedCountTextWidgetName = TEXT("FallbackCapturedCount");
	TargetCountTextWidgetName = TEXT("FallbackTargetCount");
	Panel->AddChildToVerticalBox(WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), CapturedCountTextWidgetName));
	Panel->AddChildToVerticalBox(WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TargetCountTextWidgetName));
}

void UBoarResultWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ResolveWidgetReferences();
	SetIsFocusable(true);
}

void UBoarResultWidget::ResolveWidgetReferences()
{
	CapturedCountText = ResolveNamedWidget<UTextBlock>(WidgetTree, CapturedCountTextWidgetName);
	TargetCountText = ResolveNamedWidget<UTextBlock>(WidgetTree, TargetCountTextWidgetName);

	if (!CapturedCountText)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: CapturedCountTextWidgetName is unset or does not reference a TextBlock."), *GetName());
	}

	if (!TargetCountText)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: TargetCountTextWidgetName is unset or does not reference a TextBlock."), *GetName());
	}
}

void UBoarResultWidget::FocusForDismissInput()
{
	SetUserFocus(GetOwningPlayer());
}

// 確定済みのRunだけを表示へ取り込みます。表示用コピーから進行中の結果や保存データを変更しません。
void UBoarResultWidget::InitializeResult(const FStageRunData& Run, int32 TargetCount)
{
	if (!Run.bResultFrozen) return;
	ResultData = Run;
	// 目標数はRunDataに含まれないStage設定値。現在収容数は確定Runだけを参照します。
	if (TargetCountText) TargetCountText->SetText(FText::AsNumber(TargetCount));
	if (CapturedCountText) CapturedCountText->SetText(FText::AsNumber(Run.CurrentHousedCount));
	if (UTextBlock* Text = ResolveNamedWidget<UTextBlock>(WidgetTree, ClearTimeTextWidgetName))
		Text->SetText(FText::AsTimespan(FTimespan::FromSeconds(Run.ClearTimeSeconds)));
	if (UTextBlock* Text = ResolveNamedWidget<UTextBlock>(WidgetTree, NewBoarCountTextWidgetName))
		Text->SetText(FText::AsNumber(Run.NewBoarUniqueIds.Num()));
	if (UTextBlock* Text = ResolveNamedWidget<UTextBlock>(WidgetTree, SpecialCoinCountTextWidgetName))
		Text->SetText(FText::AsNumber(Run.SpecialCoinIds.Num()));
	// 確定Runの値だけで文面を作ります（仮の数値は使いません）。
	FFormatNamedArguments Args;
	Args.Add(TEXT("Captured"), FText::AsNumber(Run.CurrentHousedCount));
	Args.Add(TEXT("Target"), FText::AsNumber(TargetCount));
	Args.Add(TEXT("Coins"), FText::AsNumber(Run.SpecialCoinIds.Num()));
	if (Text_CaptureLine) Text_CaptureLine->SetText(FText::Format(CaptureLineFormat, Args));
	if (Text_CoinLine) Text_CoinLine->SetText(FText::Format(CoinLineFormat, Args));
	OnRunResultUpdated();
	StartReveal();
}

void UBoarResultWidget::StartReveal()
{
	UWidgetAnimation* Anims[3] = { Anim_StageClear, Anim_CaptureCount, Anim_SpecialCoin };
	UTextBlock* Texts[3] = { Text_StageClear, Text_CaptureLine, Text_CoinLine };
	// 各項目は前の項目のアニメ終了から一定間隔後に開始します。項目が無いステップは時間を使いません。
	float Time = 0.f;
	RevealUnlockTime = 0.f;
	for (int32 I = 0; I < 3; ++I)
	{
		const bool bHasStep = Anims[I] || Texts[I];
		RevealStepDuration[I] = Anims[I] ? Anims[I]->GetEndTime() : (Texts[I] ? FallbackRevealDuration : 0.f);
		RevealStepStart[I] = Time;
		if (Texts[I]) Texts[I]->SetRenderOpacity(0.f);
		if (bHasStep)
		{
			RevealUnlockTime = Time + RevealStepDuration[I];
			Time = RevealUnlockTime + RevealStepInterval;
		}
	}
	RevealUnlockTime += InputUnlockDelay;
	for (const FName Name : LateRevealWidgetNames)
		if (UWidget* Late = ResolveNamedWidget<UWidget>(WidgetTree, Name)) Late->SetRenderOpacity(0.f);
	LateRevealAge = -1.f;
	RevealAge = 0.f;
	NextRevealStep = 0;
	bDismissInputAccepted = false;
	bRevealRunning = true;
}

void UBoarResultWidget::StartRevealStep(int32 Index)
{
	UWidgetAnimation* Anims[3] = { Anim_StageClear, Anim_CaptureCount, Anim_SpecialCoin };
	UTextBlock* Texts[3] = { Text_StageClear, Text_CaptureLine, Text_CoinLine };
	USoundBase* Sounds[3] = { StageClearSound, CaptureCountSound, SpecialCoinSound };
	if (!Anims[Index] && !Texts[Index]) return;
	if (Anims[Index]) PlayAnimation(Anims[Index]);
	if (Sounds[Index] && GetGameInstance())
		if (auto* Audio = GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>()) Audio->PlaySoundEffect2D(this, Sounds[Index]);
}

void UBoarResultWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (LateRevealAge >= 0.f)
	{
		LateRevealAge += InDeltaTime;
		const float Alpha = FMath::Clamp(LateRevealAge / FallbackRevealDuration, 0.f, 1.f);
		for (const FName Name : LateRevealWidgetNames)
			if (UWidget* Late = ResolveNamedWidget<UWidget>(WidgetTree, Name)) Late->SetRenderOpacity(Alpha);
		if (Alpha >= 1.f) LateRevealAge = -1.f;
	}
	if (!bRevealRunning) return;
	RevealAge += InDeltaTime;
	UTextBlock* Texts[3] = { Text_StageClear, Text_CaptureLine, Text_CoinLine };
	UWidgetAnimation* Anims[3] = { Anim_StageClear, Anim_CaptureCount, Anim_SpecialCoin };
	while (NextRevealStep < 3 && RevealAge >= RevealStepStart[NextRevealStep]) StartRevealStep(NextRevealStep++);
	// UMGアニメが無い項目だけ、不透明度でフェードインします。
	for (int32 I = 0; I < 3; ++I)
		if (Texts[I] && !Anims[I] && I < NextRevealStep)
			Texts[I]->SetRenderOpacity(FMath::Clamp((RevealAge - RevealStepStart[I]) / FallbackRevealDuration, 0.f, 1.f));
	if (NextRevealStep >= 3 && RevealAge >= RevealUnlockTime)
	{
		bRevealRunning = false;
		bDismissInputAccepted = true;
		LateRevealAge = 0.f;
		OnRevealFinished();
	}
}

FReply UBoarResultWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	(void)InGeometry;
	(void)InKeyEvent;
	return RequestDismiss();
}

FReply UBoarResultWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	(void)InGeometry;
	(void)InMouseEvent;
	return RequestDismiss();
}

// 任意キー・マウス入力を共通の終了要求へまとめ、実際の復帰処理は呼び出し元に委ねます。
FReply UBoarResultWidget::RequestDismiss()
{
	// 順表示の途中は入力を消費し、Lobbyへは進めません。
	if (!bDismissInputAccepted) return FReply::Handled();
	OnDismissRequested.Broadcast();
	return FReply::Handled();
}
