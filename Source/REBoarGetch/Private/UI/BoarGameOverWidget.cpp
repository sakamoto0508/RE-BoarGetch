#include "UI/BoarGameOverWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"

// RetryとLobbyの選択を通知へ接続します。BPに必要なボタンがない場合は警告します。
void UBoarGameOverWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RetryButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(RetryButtonWidgetName)) : nullptr;
	LobbyButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(LobbyButtonWidgetName)) : nullptr;
	if (RetryButton) RetryButton->OnClicked.AddUniqueDynamic(this, &UBoarGameOverWidget::RequestRetry);
	if (LobbyButton) LobbyButton->OnClicked.AddUniqueDynamic(this, &UBoarGameOverWidget::RequestLobby);
	if (!RetryButton || !LobbyButton) UE_LOG(LogTemp, Warning,
	                                         TEXT("[GameOver] Configure Retry/Lobby widget references."));
	// 見出しは少し拡大した状態から落ち着き、パネルは短くフェードインします（操作・フォーカスは最初から有効）。
	HeadingWidget = (WidgetTree && !HeadingWidgetName.IsNone()) ? WidgetTree->FindWidget(HeadingWidgetName) : nullptr;
	PanelWidget = (WidgetTree && !PanelWidgetName.IsNone()) ? WidgetTree->FindWidget(PanelWidgetName) : nullptr;
	// 見出しの枠はWBPのWidgetで描き、C++は見出しと同じ拡大率・不透明度を適用するだけです。
	HeadingFrameWidget = (WidgetTree && !HeadingFrameWidgetName.IsNone())
		                     ? WidgetTree->FindWidget(HeadingFrameWidgetName)
		                     : nullptr;
	RevealAge = 0.f;
	for (UWidget* Widget : {HeadingWidget.Get(), HeadingFrameWidget.Get()})
	{
		if (!Widget) continue;
		Widget->SetRenderTransformPivot(FVector2D(.5f, .5f));
		Widget->SetRenderScale(FVector2D(HeadingStartScale));
		Widget->SetRenderOpacity(0.f);
	}
	if (UWidget* Fade = PanelWidget ? PanelWidget.Get() : this) Fade->SetRenderOpacity(
		PanelFadeInTime > 0.f ? 0.f : 1.f);
}

void UBoarGameOverWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (RevealAge > PanelFadeInTime + HeadingRevealTime) return;
	RevealAge += FMath::Min(InDeltaTime, .1f);
	if (UWidget* Fade = PanelWidget ? PanelWidget.Get() : this)
		Fade->SetRenderOpacity(PanelFadeInTime > 0.f ? FMath::Clamp(RevealAge / PanelFadeInTime, 0.f, 1.f) : 1.f);
	if (!HeadingWidget) return;
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

// 再表示や画面破棄で古いボタン通知が残らないよう解除します。
void UBoarGameOverWidget::NativeDestruct()
{
	if (RetryButton) RetryButton->OnClicked.RemoveDynamic(this, &UBoarGameOverWidget::RequestRetry);
	if (LobbyButton) LobbyButton->OnClicked.RemoveDynamic(this, &UBoarGameOverWidget::RequestLobby);
	Super::NativeDestruct();
}

void UBoarGameOverWidget::FocusInitialChoice()
{
	if (RetryButton) RetryButton->SetUserFocus(GetOwningPlayer());
}

// 再挑戦の意思だけを通知し、レベル再読み込みは呼び出し元に任せます。
void UBoarGameOverWidget::RequestRetry() { OnRetryRequested.Broadcast(); }
void UBoarGameOverWidget::RequestLobby() { OnLobbyRequested.Broadcast(); }
