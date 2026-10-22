#include "UI/BoarGameOverWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Rendering/DrawElements.h"

void UBoarGameOverWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 終了後はActor停止を維持し、Retry/Lobbyを選べる最小UIを用意します。
	if (GetClass() != StaticClass() || !WidgetTree || WidgetTree->RootWidget) return;
	auto* Panel = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Panel;
	auto* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(FText::FromString(TEXT("Game Over")));
	Panel->AddChildToVerticalBox(Title);
	RetryButtonWidgetName = TEXT("FallbackRetry");
	LobbyButtonWidgetName = TEXT("FallbackLobby");
	for (const FName Name : {RetryButtonWidgetName, LobbyButtonWidgetName})
	{
		auto* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Name == RetryButtonWidgetName ? TEXT("Retry") : TEXT("Lobby")));
		Button->AddChild(Label);
		Panel->AddChildToVerticalBox(Button);
	}
}

// RetryとLobbyの選択を通知へ接続します。BPに必要なボタンがない場合は警告します。
void UBoarGameOverWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RetryButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(RetryButtonWidgetName)) : nullptr;
	LobbyButton = WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(LobbyButtonWidgetName)) : nullptr;
	if (RetryButton) RetryButton->OnClicked.AddUniqueDynamic(this, &UBoarGameOverWidget::RequestRetry);
	if (LobbyButton) LobbyButton->OnClicked.AddUniqueDynamic(this, &UBoarGameOverWidget::RequestLobby);
	if (!RetryButton || !LobbyButton) UE_LOG(LogTemp, Warning, TEXT("[GameOver] Configure Retry/Lobby widget references."));
	// 見出しは少し拡大した状態から落ち着き、パネルは短くフェードインします（操作・フォーカスは最初から有効）。
	HeadingWidget = (WidgetTree && !HeadingWidgetName.IsNone()) ? WidgetTree->FindWidget(HeadingWidgetName) : nullptr;
	PanelWidget = (WidgetTree && !PanelWidgetName.IsNone()) ? WidgetTree->FindWidget(PanelWidgetName) : nullptr;
	RevealAge = 0.f;
	if (HeadingWidget)
	{
		HeadingWidget->SetRenderTransformPivot(FVector2D(.5f, .5f));
		HeadingWidget->SetRenderScale(FVector2D(HeadingStartScale));
		HeadingWidget->SetRenderOpacity(0.f);
	}
	if (UWidget* Fade = PanelWidget ? PanelWidget.Get() : this) Fade->SetRenderOpacity(PanelFadeInTime > 0.f ? 0.f : 1.f);
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
	const float Scale = T < .65f ? FMath::InterpEaseOut(HeadingStartScale, Low, T / .65f, 2.f) : FMath::InterpEaseInOut(Low, 1.f, (T - .65f) / .35f, 2.f);
	HeadingWidget->SetRenderScale(FVector2D(Scale));
	HeadingWidget->SetRenderOpacity(FMath::Clamp(T / .3f, 0.f, 1.f));
}

int32 UBoarGameOverWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!HeadingWidget) return Layer;
	// 見出しの実表示位置（拡大中を含む）を囲むシアンの細いフレームと、角の短いアクセントを描きます。
	const FGeometry& Heading = HeadingWidget->GetCachedGeometry();
	const FVector2D Size = FVector2D(Heading.GetLocalSize());
	if (Size.X < 1.f || Size.Y < 1.f) return Layer;
	// Slotより小さい文字そのもの（Desired Size、上寄せ・左右中央）を囲みます。
	const FVector2D Text = FVector2D(FMath::Min(HeadingWidget->GetDesiredSize().X, Size.X), FMath::Min(HeadingWidget->GetDesiredSize().Y, Size.Y));
	const float Scale = HeadingWidget->GetRenderTransform().Scale.X;
	const FVector2D Half = (Text * .5f) * Scale + FramePadding;
	const FVector2D Center = FVector2D(AllottedGeometry.AbsoluteToLocal(Heading.LocalToAbsolute(FVector2D(Size.X * .5f, Text.Y * .5f))));
	const float Alpha = FMath::Clamp(RevealAge / FMath::Max(.01f, HeadingRevealTime * .5f), 0.f, 1.f) * HeadingWidget->GetRenderOpacity();
	if (Alpha <= 0.f) return Layer;
	FLinearColor Color = FrameColor;
	Color.A *= Alpha;
	const FVector2D Min = Center - Half, Max = Center + Half;
	const FVector2f A(Min), B(Max);
	TArray<FVector2f> Rect = { A, FVector2f(B.X, A.Y), B, FVector2f(A.X, B.Y), A };
	FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(), Rect, ESlateDrawEffect::None, Color, true, FrameThickness);
	const float L = 14.f;
	FLinearColor Accent = Color;
	Accent.A = FMath::Min(1.f, Color.A * 1.2f);
	for (int32 X = 0; X < 2; ++X)
		for (int32 Y = 0; Y < 2; ++Y)
		{
			const FVector2f P(X ? B.X : A.X, Y ? B.Y : A.Y);
			TArray<FVector2f> Corner = { P + FVector2f(X ? -L : L, 0.f), P, P + FVector2f(0.f, Y ? -L : L) };
			FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(), Corner, ESlateDrawEffect::None, Accent, true, FrameThickness + 1.5f);
		}
	return Layer + 1;
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
