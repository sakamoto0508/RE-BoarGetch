#include "UI/BoarGameOverWidget.h"
#include "REBoarGetch.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

// RetryとLobbyの選択を通知へ接続します。BPに必要なボタンがない場合は警告します。
void UBoarGameOverWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RetryButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(RetryButtonWidgetName)) : nullptr;
	LobbyButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(LobbyButtonWidgetName)) : nullptr;

	if (RetryButton)
		RetryButton->OnClicked.AddUniqueDynamic(this, &UBoarGameOverWidget::RequestRetry);

	if (LobbyButton)
		LobbyButton->OnClicked.AddUniqueDynamic(this, &UBoarGameOverWidget::RequestLobby);

	if (!RetryButton || !LobbyButton)
	{
		UE_LOG(LogREBoarGetch, Warning, TEXT("[GameOver] Retry／LobbyのWidget参照を設定してください。"));
	}

	// 選択表示はボタン背面の枠Image・アイコン・ラベルを切り替えて行います（初期選択はRetry）。
	ResolveChoice(Choices[0], RetryButton, RetryFrameWidgetName, RetryIconWidgetName, RetryLabelWidgetName);
	ResolveChoice(Choices[1], LobbyButton, LobbyFrameWidgetName, LobbyIconWidgetName, LobbyLabelWidgetName);
	PulseAge = 0.f;
	ApplySelection(0);

	// 見出しは少し拡大した状態から落ち着き、パネルは短くフェードインします（操作・フォーカスは最初から有効）。
	auto Find = [this](FName Name) { return (WidgetTree && !Name.IsNone()) ? WidgetTree->FindWidget(Name) : nullptr; };
	HeadingWidget = Find(HeadingWidgetName);
	PanelWidget = Find(PanelWidgetName);
	HeadingFrameWidget = Find(HeadingFrameWidgetName);
	ChoicesWidget = Find(ChoicesWidgetName);
	RevealAge = 0.f;

	for (UWidget* Widget : {HeadingWidget.Get(), HeadingFrameWidget.Get()})
	{
		if (!Widget)
			continue;

		Widget->SetRenderTransformPivot(FVector2D(.5f, .5f));
		Widget->SetRenderScale(FVector2D(HeadingStartScale));
		Widget->SetRenderOpacity(0.f);
	}

	if (ChoicesWidget)
		ChoicesWidget->SetRenderOpacity(0.f);

	if (UWidget* Fade = PanelWidget ? PanelWidget.Get() : this) Fade->SetRenderOpacity(
		PanelFadeInTime > 0.f ? 0.f : 1.f);
}

void UBoarGameOverWidget::ResolveChoice(FChoiceVisual& Out, UButton* Button, FName Frame, FName Icon, FName Label) const
{
	auto Find = [this](FName Name) { return (WidgetTree && !Name.IsNone()) ? WidgetTree->FindWidget(Name) : nullptr; };
	Out.Button = Button;
	Out.Frame = Cast<UImage>(Find(Frame));
	Out.Icon = Cast<UImage>(Find(Icon));
	Out.Label = Cast<UTextBlock>(Find(Label));

	for (UWidget* Widget : {static_cast<UWidget*>(Button), static_cast<UWidget*>(Out.Frame.Get())})
		if (Widget)
			Widget->SetRenderTransformPivot(FVector2D(.5f, .5f));
}

// 選択中の項目だけアンバーの発光枠・少し拡大、それ以外はシアン輪郭の通常状態にします。
void UBoarGameOverWidget::ApplySelection(int32 Index)
{
	SelectedChoice = Index;

	for (int32 i = 0; i < UE_ARRAY_COUNT(Choices); ++i)
	{
		const FChoiceVisual& C = Choices[i];
		const bool bSelected = i == Index;

		if (UImage* Frame = C.Frame.Get())
		{
			if (UTexture2D* Texture = bSelected ? SelectedFrameTexture : NormalFrameTexture)
				Frame->SetBrushFromTexture(Texture, false);

			Frame->SetRenderOpacity(1.f);
			Frame->SetRenderScale(FVector2D(bSelected ? SelectedScale : 1.f));
		}

		if (UButton* Button = C.Button.Get())
			Button->SetRenderScale(FVector2D(bSelected ? SelectedScale : 1.f));

		if (UImage* Icon = C.Icon.Get())
			Icon->SetColorAndOpacity(bSelected ? SelectedIconColor : NormalIconColor);

		if (UTextBlock* Label = C.Label.Get()) Label->SetColorAndOpacity(
			FSlateColor(bSelected ? SelectedLabelColor : NormalLabelColor));
	}
}

// ホバーした項目へフォーカスを移し、フォーカスのある項目を選択表示にします。
// どれにもフォーカスが無い（背景クリック等）ときは選択中の項目へ戻し、Gamepad/キーボード操作を途切れさせません。
void UBoarGameOverWidget::UpdateSelection()
{
	int32 Focused = INDEX_NONE;

	for (int32 i = 0; i < UE_ARRAY_COUNT(Choices); ++i)
	{
		UButton* Button = Choices[i].Button.Get();
		if (!Button)
			continue;
		// ホバー開始時だけ移すので、マウスを置いたままでもGamepad/キーで選択を変えられます。
		const bool bHovered = Button->IsHovered();
		if (bHovered && !Choices[i].bWasHovered)
			Button->SetUserFocus(GetOwningPlayer());

		Choices[i].bWasHovered = bHovered;

		if (Button->HasAnyUserFocus())
			Focused = i;
	}

	if (Focused != INDEX_NONE)
	{
		if (Focused != SelectedChoice)
		{
			PulseAge = 0.f;
			ApplySelection(Focused);
		}
	}
	else if (UButton* Button = Choices[SelectedChoice].Button.Get())
		Button->SetUserFocus(GetOwningPlayer());
}

void UBoarGameOverWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const float Dt = FMath::Min(InDeltaTime, .1f);
	UpdateSelection();

	// 選択枠の発光をゆっくり明滅させます。
	PulseAge += Dt;

	if (UImage* Frame = Choices[SelectedChoice].Frame.Get())
	{
		const float Wave = .5f + .5f * FMath::Cos(PulseAge * 2.f * PI / SelectedPulsePeriod);
		Frame->SetRenderOpacity(FMath::Lerp(SelectedPulseMinOpacity, 1.f, Wave));
	}

	const float RevealEnd = FMath::Max(PanelFadeInTime + HeadingRevealTime, ChoicesDelay + ChoicesRevealTime);
	if (RevealAge > RevealEnd)
		return;

	RevealAge += Dt;

	if (UWidget* Fade = PanelWidget ? PanelWidget.Get() : this)
		Fade->SetRenderOpacity(PanelFadeInTime > 0.f ? FMath::Clamp(RevealAge / PanelFadeInTime, 0.f, 1.f) : 1.f);

	if (ChoicesWidget)
	{
		const float C = FMath::Clamp((RevealAge - ChoicesDelay) / ChoicesRevealTime, 0.f, 1.f);
		ChoicesWidget->SetRenderOpacity(C);
		ChoicesWidget->SetRenderTranslation(FVector2D(0.f, FMath::InterpEaseOut(24.f, 0.f, C, 2.f)));
	}

	if (!HeadingWidget)
		return;
	// 0～65%で開始倍率→わずかに小さく、残りで1.0へ戻る「拡大して落ち着く」動きです。
	const float T = FMath::Clamp(RevealAge / HeadingRevealTime, 0.f, 1.f);
	const float Low = 1.f - HeadingSettleUndershoot;
	const float Scale = T < .65f
			? FMath::InterpEaseOut(HeadingStartScale, Low, T / .65f, 2.f)
			: FMath::InterpEaseInOut(Low, 1.f, (T - .65f) / .35f, 2.f);

	HeadingWidget->SetRenderScale(FVector2D(Scale));
	HeadingWidget->SetRenderOpacity(FMath::Clamp(T / .3f, 0.f, 1.f));

	if (HeadingFrameWidget)
	{
		HeadingFrameWidget->SetRenderScale(FVector2D(Scale));
		HeadingFrameWidget->SetRenderOpacity(HeadingWidget->GetRenderOpacity());
	}
}

FReply UBoarGameOverWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ConfirmKeys.Contains(InKeyEvent.GetKey()))
	{
		if (!InKeyEvent.IsRepeat())
			SelectedChoice == 1 ? RequestLobby() : RequestRetry();

		return FReply::Handled();
	}

	if (IgnoredKeys.Contains(InKeyEvent.GetKey()))
		return FReply::Handled();

	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 再表示や画面破棄で古いボタン通知が残らないよう解除します。
void UBoarGameOverWidget::NativeDestruct()
{
	if (RetryButton)
		RetryButton->OnClicked.RemoveDynamic(this, &UBoarGameOverWidget::RequestRetry);

	if (LobbyButton)
		LobbyButton->OnClicked.RemoveDynamic(this, &UBoarGameOverWidget::RequestLobby);

	Super::NativeDestruct();
}

TArray<FString> UBoarGameOverWidget::GetMissingRequiredWidgets() const
{
	TArray<FString> Missing;
	auto Check = [&](const TCHAR* Property, FName Name)
	{
		if (Name.IsNone())
			Missing.Add(FString::Printf(TEXT("%s (unset)"), Property));
		else if (!WidgetTree || !Cast<UButton>(WidgetTree->FindWidget(Name)))
			Missing.Add(FString::Printf(TEXT("%s='%s' (no Button with this name)"), Property, *Name.ToString()));
	};
	Check(TEXT("RetryButtonWidgetName"), RetryButtonWidgetName);
	Check(TEXT("LobbyButtonWidgetName"), LobbyButtonWidgetName);
	return Missing;
}

void UBoarGameOverWidget::FocusInitialChoice()
{
	ApplySelection(0);

	if (RetryButton)
		RetryButton->SetUserFocus(GetOwningPlayer());
}

// 再挑戦の意思だけを通知し、レベル再読み込みは呼び出し元に任せます。
void UBoarGameOverWidget::RequestRetry() { OnRetryRequested.Broadcast(); }
void UBoarGameOverWidget::RequestLobby() { OnLobbyRequested.Broadcast(); }
