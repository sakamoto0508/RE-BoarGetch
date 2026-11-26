#include "UI/BoarResultWidget.h"
#include "REBoarGetch.h"

#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Input/Reply.h"
#include "Animation/WidgetAnimation.h"
#include "Audio/BoarAudioManagerSubsystem.h"
#include "Core/BoarGameMode.h"
#include "Engine/World.h"
#include "Stage/StageConfig.h"
#include "UI/BoarResultBoarCard.h"
#include "Engine/Texture2D.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Player/BoarPlayerController.h"

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

void UBoarResultWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ResolveWidgetReferences();
	SetIsFocusable(true);

	// 子の装飾は全てクリック透過なので、Widget自身を全面ヒット対象にしてPCのクリックでも終了入力を受け取ります。
	SetVisibility(ESlateVisibility::Visible);
}

void UBoarResultWidget::ResolveWidgetReferences()
{
	CapturedCountText = ResolveNamedWidget<UTextBlock>(WidgetTree, CapturedCountTextWidgetName);
	TargetCountText = ResolveNamedWidget<UTextBlock>(WidgetTree, TargetCountTextWidgetName);

	if (!CapturedCountText)
	{
		UE_LOG(LogREBoarGetch, Warning, TEXT("%s: CapturedCountTextWidgetNameが未設定か、TextBlockを指していません。"),
			*GetName());
	}

	if (!TargetCountText)
	{
		UE_LOG(LogREBoarGetch, Warning, TEXT("%s: TargetCountTextWidgetNameが未設定か、TextBlockを指していません。"),
			*GetName());
	}
}

void UBoarResultWidget::FocusForDismissInput()
{
	SetUserFocus(GetOwningPlayer());
}

// 確定済みのRunだけを表示へ取り込みます。表示用コピーから進行中の結果や保存データを変更しません。
void UBoarResultWidget::InitializeResult(const FStageRunData& Run, int32 TargetCount)
{
	if (!Run.bResultFrozen)
		return;

	ResultData = Run;

	// 目標数はRunDataに含まれないStage設定値。現在収容数は確定Runだけを参照します。
	if (TargetCountText)
		TargetCountText->SetText(FText::AsNumber(TargetCount));

	if (CapturedCountText)
		CapturedCountText->SetText(FText::AsNumber(Run.CurrentHousedCount));

	if (UTextBlock* Text = ResolveNamedWidget<UTextBlock>(WidgetTree, ClearTimeTextWidgetName))
		Text->SetText(FormatClearTime(Run.ClearTimeSeconds));

	if (UTextBlock* Text = ResolveNamedWidget<UTextBlock>(WidgetTree, NewBoarCountTextWidgetName))
		Text->SetText(FText::AsNumber(Run.NewBoarUniqueIds.Num()));

	if (UTextBlock* Text = ResolveNamedWidget<UTextBlock>(WidgetTree, NewBoarListTextWidgetName))
		Text->SetText(BuildNewBoarList(Run));

	if (UTextBlock* Text = ResolveNamedWidget<UTextBlock>(WidgetTree, SpecialCoinCountTextWidgetName))
		Text->SetText(FText::AsNumber(Run.SpecialCoinIds.Num()));
	// 確定Runの値だけで文面を作ります（仮の数値は使いません）。
	FFormatNamedArguments Args;
	Args.Add(TEXT("Captured"), FText::AsNumber(Run.CurrentHousedCount));
	Args.Add(TEXT("Target"), FText::AsNumber(TargetCount));
	Args.Add(TEXT("Coins"), FText::AsNumber(Run.SpecialCoinIds.Num()));

	if (Text_CaptureLine)
		Text_CaptureLine->SetText(FText::Format(CaptureLineFormat, Args));

	if (Text_CoinLine)
		Text_CoinLine->SetText(FText::Format(CoinLineFormat, Args));

	if (UTextBlock* Text = ResolveNamedWidget<UTextBlock>(WidgetTree, StageNameTextWidgetName))
		if (const UStageConfig* Stage = FindRunStage(Run))
			Text->SetText(Stage->DisplayName);

	FillBoarCards(Run);

	// 報酬が無い（0枚）ときはアンバーで強調しません。
	const FSlateColor CoinColor(Run.SpecialCoinIds.IsEmpty() ? CoinZeroColor : CoinEarnedColor);

	for (FName Name : {SpecialCoinCountTextWidgetName, CoinPlusTextWidgetName})
		if (UTextBlock* Text = ResolveNamedWidget<UTextBlock>(WidgetTree, Name))
			Text->SetColorAndOpacity(CoinColor);

	const ABoarPlayerController* BoarPC = Cast<ABoarPlayerController>(GetOwningPlayer());
	GuideDevice = INDEX_NONE;
	SetGuideDevice(BoarPC && BoarPC->WasLastInputGamepad());

	// Clear演出は最後に黒へフェードして終わるので、Result表示に合わせて背後のステージを戻します。
	if (WorldFadeInTime > 0.f)
		if (APlayerController* PC = GetOwningPlayer(); PC && PC->PlayerCameraManager && PC->PlayerCameraManager->FadeAmount > 0.f)
			PC->PlayerCameraManager->StartCameraFade(PC->PlayerCameraManager->FadeAmount, 0.f, WorldFadeInTime, FLinearColor::Black, false, false);

	OnRunResultUpdated();
	StartReveal();
}

FText UBoarResultWidget::FormatClearTime(float Seconds) const
{
	const int32 TotalHundredths = FMath::Max(0, FMath::FloorToInt(Seconds * 100.f));
	auto Two = [](int32 V) { return FText::FromString(FString::Printf(TEXT("%02d"), V)); };
	FFormatNamedArguments Args;
	Args.Add(TEXT("Minutes"), Two(TotalHundredths / 6000));
	Args.Add(TEXT("Seconds"), Two(TotalHundredths / 100 % 60));
	Args.Add(TEXT("Hundredths"), Two(TotalHundredths % 100));
	return FText::Format(ClearTimeFormat, Args);
}

const UStageConfig* UBoarResultWidget::FindRunStage(const FStageRunData& Run) const
{
	const ABoarGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr;
	const UStageConfig* Stage = Mode ? Mode->GetStageConfig() : nullptr;
	return Stage && Stage->StageId == Run.StageId ? Stage : nullptr;
}

// NEWの判定はしない。確定済みRunのNewBoarUniqueIdsを先頭からカードへ並べ、余りは未登録スロットにする。
// 名前は図鑑名（未設定ならID）、写真は図鑑写真（未設定ならカード側のプレースホルダー）を使う。
void UBoarResultWidget::FillBoarCards(const FStageRunData& Run) const
{
	const UStageConfig* Stage = FindRunStage(Run);

	// 今回NEWが無ければ空き枠を並べず、「今回の新規登録はありません」を出します。
	const bool bAnyNew = !Run.NewBoarUniqueIds.IsEmpty();

	if (UTextBlock* Empty = ResolveNamedWidget<UTextBlock>(WidgetTree, NewBoarEmptyTextWidgetName))
	{
		Empty->SetText(NewBoarListEmptyText);
		Empty->SetVisibility(bAnyNew ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	for (const FName Name : BoarCardWidgetNames)
		if (UWidget* Card = ResolveNamedWidget<UWidget>(WidgetTree, Name))
			Card->SetVisibility(bAnyNew ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);

	for (int32 I = 0; I < BoarCardWidgetNames.Num(); ++I)
	{
		UBoarResultBoarCard* Card = ResolveNamedWidget<UBoarResultBoarCard>(WidgetTree, BoarCardWidgetNames[I]);
		if (!Card)
			continue;
		if (!Run.NewBoarUniqueIds.IsValidIndex(I))
		{
			Card->SetEmptySlot();
			continue;
		}

		const FName Id = Run.NewBoarUniqueIds[I];
		const int32 DefIndex = Stage
				? Stage->BoarSpawnDefinitions.IndexOfByPredicate(
					[Id](const FBoarSpawnDefinition& D) { return D.BoarUniqueId == Id; })
				: INDEX_NONE;

		const FBoarSpawnDefinition* Def = DefIndex != INDEX_NONE ? &Stage->BoarSpawnDefinitions[DefIndex] : nullptr;
		FFormatNamedArguments Args;
		Args.Add(TEXT("Number"), FText::FromString(DefIndex != INDEX_NONE ? FString::Printf(TEXT("%03d"), DefIndex + 1) : TEXT("---")));
		Card->SetBoarEntry(Def ? Def->EncyclopediaPhoto.LoadSynchronous() : nullptr, FText::Format(BoarCardNumberFormat, Args),
			Def && !Def->EncyclopediaName.IsEmpty() ? Def->EncyclopediaName : FText::FromName(Id),
			Stage ? Stage->DisplayName : FText::GetEmpty(), true);
	}
}

UWidget* UBoarResultWidget::GetRevealStepWidget(int32 Index) const
{
	UTextBlock* Texts[3] = {Text_StageClear, Text_CaptureLine, Text_CoinLine};

	if (Texts[Index])
		return Texts[Index];

	return RevealStepWidgetNames.IsValidIndex(Index) ? ResolveNamedWidget<UWidget>(WidgetTree, RevealStepWidgetNames[Index]) : nullptr;
}

// NEWの判定はしない。確定済みRunのNewBoarUniqueIdsを、表示名へ置き換えて並べるだけ。
FText UBoarResultWidget::BuildNewBoarList(const FStageRunData& Run) const
{
	if (Run.NewBoarUniqueIds.IsEmpty())
		return NewBoarListEmptyText;

	const UStageConfig* Stage = FindRunStage(Run);
	TArray<FText> Lines;

	for (const FName Id : Run.NewBoarUniqueIds)
	{
		FText Name = FText::FromName(Id);

		if (Stage)
			if (const FBoarSpawnDefinition* Def = Stage->BoarSpawnDefinitions.FindByPredicate(
				[Id](const FBoarSpawnDefinition& D) { return D.BoarUniqueId == Id; }); Def && !Def->EncyclopediaName.IsEmpty())
				Name = Def->EncyclopediaName;

		FFormatNamedArguments Args;
		Args.Add(TEXT("Name"), Name);
		Lines.Add(FText::Format(NewBoarEntryFormat, Args));
	}

	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

void UBoarResultWidget::StartReveal()
{
	UWidgetAnimation* Anims[3] = {Anim_StageClear, Anim_CaptureCount, Anim_SpecialCoin};
	UWidget* Texts[3] = {GetRevealStepWidget(0), GetRevealStepWidget(1), GetRevealStepWidget(2)};

	// 各項目は前の項目のアニメ終了から一定間隔後に開始します。項目が無いステップは時間を使いません。
	float Time = 0.f;
	RevealUnlockTime = 0.f;

	for (int32 I = 0; I < 3; ++I)
	{
		const bool bHasStep = Anims[I] || Texts[I];
		RevealStepDuration[I] = Anims[I] ? Anims[I]->GetEndTime() : (Texts[I] ? FallbackRevealDuration : 0.f);
		RevealStepStart[I] = Time;

		if (Texts[I])
			Texts[I]->SetRenderOpacity(0.f);

		if (bHasStep)
		{
			RevealUnlockTime = Time + RevealStepDuration[I];
			Time = RevealUnlockTime + RevealStepInterval;
		}
	}

	RevealUnlockTime += InputUnlockDelay;

	for (const FName Name : LateRevealWidgetNames)
		if (UWidget* Late = ResolveNamedWidget<UWidget>(WidgetTree, Name))
			Late->SetRenderOpacity(0.f);

	LateRevealAge = -1.f;
	RevealAge = 0.f;
	NextRevealStep = 0;
	bDismissInputAccepted = false;
	bRevealRunning = true;
}

void UBoarResultWidget::StartRevealStep(int32 Index)
{
	UWidgetAnimation* Anims[3] = {Anim_StageClear, Anim_CaptureCount, Anim_SpecialCoin};
	UWidget* Texts[3] = {GetRevealStepWidget(0), GetRevealStepWidget(1), GetRevealStepWidget(2)};
	USoundBase* Sounds[3] = {StageClearSound, CaptureCountSound, SpecialCoinSound};

	if (!Anims[Index] && !Texts[Index])
		return;
	if (Anims[Index])
		PlayAnimation(Anims[Index]);

	if (Sounds[Index] && GetGameInstance())
		if (auto* Audio = GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>()) Audio->PlaySoundEffect2D(
			this, Sounds[Index]);
}

void UBoarResultWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (LateRevealAge >= 0.f)
	{
		LateRevealAge += InDeltaTime;
		const float Alpha = FMath::Clamp(LateRevealAge / FallbackRevealDuration, 0.f, 1.f);

		for (const FName Name : LateRevealWidgetNames)
			if (UWidget* Late = ResolveNamedWidget<UWidget>(WidgetTree, Name))
				Late->SetRenderOpacity(Alpha);

		if (Alpha >= 1.f)
			LateRevealAge = -1.f;
	}

	if (!bRevealRunning)
		return;

	RevealAge += InDeltaTime;
	UWidget* Texts[3] = {GetRevealStepWidget(0), GetRevealStepWidget(1), GetRevealStepWidget(2)};
	UWidgetAnimation* Anims[3] = {Anim_StageClear, Anim_CaptureCount, Anim_SpecialCoin};

	while (NextRevealStep < 3 && RevealAge >= RevealStepStart[NextRevealStep])
		StartRevealStep(NextRevealStep++);
	// UMGアニメが無い項目だけ、不透明度でフェードインします。
	for (int32 I = 0; I < 3; ++I)
		if (Texts[I] && !Anims[I] && I < NextRevealStep)
			Texts[I]->SetRenderOpacity(
				FMath::Clamp((RevealAge - RevealStepStart[I]) / FallbackRevealDuration, 0.f, 1.f));
	if (NextRevealStep >= 3 && RevealAge >= RevealUnlockTime)
	{
		bRevealRunning = false;
		bDismissInputAccepted = true;
		LateRevealAge = 0.f;

		if (Anim_ContinueGuide)
			PlayAnimation(Anim_ContinueGuide, 0.f, 0);

		OnRevealFinished();
	}
}

void UBoarResultWidget::SetGuideDevice(bool bGamepad)
{
	if (GuideDevice == int32(bGamepad))
		return;

	GuideDevice = int32(bGamepad);

	// 保存失敗中は「任意の入力で戻る」の案内を出さない（警告の文面に操作を書いている）。
	if (bSaveFailed)
	{
		if (auto* BoarPC = Cast<ABoarPlayerController>(GetOwningPlayer()))
			BoarPC->NotifyInputDevice(bGamepad);
		return;
	}

	if (UWidget* Pad = ResolveNamedWidget<UWidget>(WidgetTree, PadGuideWidgetName))
		Pad->SetVisibility(bGamepad ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	if (UWidget* Keyboard = ResolveNamedWidget<UWidget>(WidgetTree, KeyboardGuideWidgetName))
		Keyboard->SetVisibility(bGamepad ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

	if (auto* BoarPC = Cast<ABoarPlayerController>(GetOwningPlayer()))
		BoarPC->NotifyInputDevice(bGamepad);
}

FReply UBoarResultWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	(void)InGeometry;
	SetGuideDevice(InKeyEvent.GetKey().IsGamepadKey());
	if (bSaveFailed)
		return HandleSaveFailureKey(InKeyEvent.GetKey());
	return RequestDismiss();
}

FReply UBoarResultWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!InMouseEvent.GetCursorDelta().IsNearlyZero())
		SetGuideDevice(false);

	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UBoarResultWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	(void)InGeometry;
	(void)InMouseEvent;
	SetGuideDevice(false);
	if (bSaveFailed)
		return HandleSaveFailureKey(EKeys::LeftMouseButton);
	return RequestDismiss();
}

// 任意キー・マウス入力を共通の終了要求へまとめ、実際の復帰処理は呼び出し元に委ねます。
FReply UBoarResultWidget::RequestDismiss()
{
	// 順表示の途中は入力を消費し、Lobbyへは進めません。
	if (!bDismissInputAccepted)
		return FReply::Handled();

	OnDismissRequested.Broadcast();
	return FReply::Handled();
}

void UBoarResultWidget::SetSaveFailed(bool bFailed)
{
	const bool bWasFailed = bSaveFailed;
	bSaveFailed = bFailed;
	bConfirmingDiscard = false;
	bSaveRetryFailed = false;
	UpdateSaveWarning();

	// 保存できたら、隠していた「任意の入力で戻る」の案内を元へ戻す。
	if (bWasFailed && !bFailed)
	{
		for (const FName Name : SaveFailureHiddenWidgetNames)
			if (UWidget* Hidden = ResolveNamedWidget<UWidget>(WidgetTree, Name))
				Hidden->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		const bool bGamepad = GuideDevice == 1;
		GuideDevice = INDEX_NONE;
		SetGuideDevice(bGamepad);
	}
}

void UBoarResultWidget::NotifySaveRetryFailed()
{
	if (!bSaveFailed)
		return;

	bSaveRetryFailed = true;
	UpdateSaveWarning();
}

bool UBoarResultWidget::ProcessSaveFailureInput(bool bDecide)
{
	if (!bSaveFailed)
		return false;

	if (!bConfirmingDiscard)
	{
		if (bDecide)
		{
			// 成功すればPlayerController経由でSetSaveFailed(false)、失敗すればNotifySaveRetryFailedが呼ばれる。
			OnSaveRetryRequested.Broadcast();
			return true;
		}

		bConfirmingDiscard = true;
		UpdateSaveWarning();
		return true;
	}

	if (bDecide)
	{
		// 保存せずLobbyへ戻る。保存に失敗した結果は進行データへ反映されていない（GameInstanceは成功時だけ正本を更新する）。
		OnDismissRequested.Broadcast();
		return true;
	}

	bConfirmingDiscard = false;
	UpdateSaveWarning();
	return true;
}

FReply UBoarResultWidget::HandleSaveFailureKey(const FKey& Key)
{
	// 順表示の途中は通常の終了入力と同じく受け付けない。
	if (!bDismissInputAccepted)
		return FReply::Handled();

	static const FKey DecideKeys[] = { EKeys::Gamepad_FaceButton_Right, EKeys::Enter, EKeys::SpaceBar, EKeys::LeftMouseButton };
	static const FKey CancelKeys[] = { EKeys::Gamepad_FaceButton_Bottom, EKeys::Escape, EKeys::BackSpace };

	for (const FKey& Decide : DecideKeys)
		if (Key == Decide)
		{
			ProcessSaveFailureInput(true);
			return FReply::Handled();
		}

	for (const FKey& Cancel : CancelKeys)
		if (Key == Cancel)
		{
			ProcessSaveFailureInput(false);
			return FReply::Handled();
		}

	// 誤操作で保存せず戻らないよう、ほかのキーは無視する。
	return FReply::Handled();
}

void UBoarResultWidget::UpdateSaveWarning()
{
	if (bSaveFailed)
		for (const FName Name : SaveFailureHiddenWidgetNames)
			if (UWidget* Hidden = ResolveNamedWidget<UWidget>(WidgetTree, Name))
				Hidden->SetVisibility(ESlateVisibility::Collapsed);

	if (Text_SaveWarning)
	{
		Text_SaveWarning->SetText(bConfirmingDiscard ? DiscardConfirmText : bSaveRetryFailed ? SaveRetryFailedText : SaveFailedText);
		Text_SaveWarning->SetVisibility(bSaveFailed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	else if (bSaveFailed && !bReportedMissingSaveWarning)
	{
		// 代替の表示は作らない。操作（決定で再試行・取消で破棄の確認）は文面がなくても受け付ける。
		bReportedMissingSaveWarning = true;
		UE_LOG(LogREBoarGetch, Error, TEXT("[Result] %s: Text_SaveWarning（TextBlock）がないため、保存失敗の警告を表示できません。"), *GetNameSafe(GetClass()));
	}

	OnSaveStateChanged(bSaveFailed, bConfirmingDiscard);
}
