#include "UI/BoarLobbyWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "UI/BoarStageSelectPresenter.h"
#include "Player/BoarPlayerController.h"
#include "UI/BoarLoadoutEntry.h"
#include "Components/ScrollBox.h"
#include "Components/CanvasPanelSlot.h"
#include "UI/BoarStagePreviewActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"

namespace
{
	// BP側の任意名Widgetを解決します。未配置・未設定の場合はnullptrとして扱います。
	template <typename TWidget>
	TWidget* ResolveLobbyWidget(UWidgetTree* WidgetTree, const FName WidgetName)
	{
		return WidgetTree && !WidgetName.IsNone()
			? Cast<TWidget>(WidgetTree->FindWidget(WidgetName))
			: nullptr;
	}
}

void UBoarLobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ResolveWidgetReferences();
	StageList = ResolveLobbyWidget<UScrollBox>(WidgetTree, StageListWidgetName);
	StageStatus = ResolveLobbyWidget<UTextBlock>(WidgetTree, StageStatusWidgetName);
	StageProgress = ResolveLobbyWidget<UTextBlock>(WidgetTree, StageProgressWidgetName);
	PreviousButton = ResolveLobbyWidget<UButton>(WidgetTree, PreviousButtonName);
	NextButton = ResolveLobbyWidget<UButton>(WidgetTree, NextButtonName);
	if (PreviousButton) PreviousButton->OnClicked.AddUniqueDynamic(this, &UBoarLobbyWidget::PreviousStage);
	if (NextButton) NextButton->OnClicked.AddUniqueDynamic(this, &UBoarLobbyWidget::NextStage);
	EncyclopediaButton = ResolveLobbyWidget<UButton>(WidgetTree, EncyclopediaButtonName);
	if (EncyclopediaButton)
	{
		EncyclopediaButton->SetVisibility(ESlateVisibility::Collapsed);
		EncyclopediaButton->OnClicked.AddUniqueDynamic(this, &UBoarLobbyWidget::OpenEncyclopedia);
		EncyclopediaButton->OnHovered.AddUniqueDynamic(this, &UBoarLobbyWidget::FocusEncyclopedia);
	}

	if (StartStageButton)
	{
		StartStageButton->OnClicked.AddUniqueDynamic(this, &UBoarLobbyWidget::HandleStartStageClicked);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.AddUniqueDynamic(this, &UBoarLobbyWidget::HandleCancelClicked);
	}
}

void UBoarLobbyWidget::NativeDestruct()
{
	// Viewとモデル両方の通知を解除し、画面外に生成したプレビューActorも破棄します。
	ReleasePreviews();
	if (Presenter)
	{
		Presenter->OnViewChanged.RemoveDynamic(this, &UBoarLobbyWidget::RefreshSelectedStage);
		Presenter->Shutdown();
	}
	if (PreviousButton) PreviousButton->OnClicked.RemoveDynamic(this, &UBoarLobbyWidget::PreviousStage);
	if (NextButton) NextButton->OnClicked.RemoveDynamic(this, &UBoarLobbyWidget::NextStage);
	for (const auto& Entry : StageEntries) if (Entry)
	{
		Entry->OnFocused.RemoveDynamic(this, &UBoarLobbyWidget::SelectStage);
		Entry->OnChosen.RemoveDynamic(this, &UBoarLobbyWidget::ChooseStage);
	}
	if (auto* PC = Cast<ABoarPlayerController>(GetOwningPlayer())) PC->CloseEncyclopediaFrom(this);
	if (EncyclopediaButton)
	{
		EncyclopediaButton->OnClicked.RemoveDynamic(this, &UBoarLobbyWidget::OpenEncyclopedia);
		EncyclopediaButton->OnHovered.RemoveDynamic(this, &UBoarLobbyWidget::FocusEncyclopedia);
	}
	if (StartStageButton)
	{
		StartStageButton->OnClicked.RemoveDynamic(this, &UBoarLobbyWidget::HandleStartStageClicked);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.RemoveDynamic(this, &UBoarLobbyWidget::HandleCancelClicked);
	}

	Super::NativeDestruct();
}

void UBoarLobbyWidget::ShowStageSelection(UStageConfig* StageConfig)
{
	// 既存の単一ステージ向けBP呼び出しも、共通のカタログ表示へ接続します。
	ShowStageCatalog({StageConfig}, StageConfig);
}

void UBoarLobbyWidget::ShowStageCatalog(const TArray<UStageConfig*>& Stages, UStageConfig* FallbackStage)
{
	for (const auto& Entry : StageEntries) if (Entry)
	{
		Entry->OnFocused.RemoveDynamic(this, &UBoarLobbyWidget::SelectStage);
		Entry->OnChosen.RemoveDynamic(this, &UBoarLobbyWidget::ChooseStage);
	}
	// カタログを差し替える前に旧Entryの通知を外し、一覧を作り直します。
	StageEntries.Reset();
	if (StageList) StageList->ClearChildren();
	if (!Presenter) Presenter = NewObject<UBoarStageSelectPresenter>(this);
	Presenter->Initialize(GetGameInstance(), Stages, FallbackStage);
	Presenter->OnViewChanged.AddUniqueDynamic(this, &UBoarLobbyWidget::RefreshSelectedStage);
	for (int32 I = 0; I < Presenter->GetStageCount(); ++I)
	{
		UBoarLoadoutEntry* Entry = !bUseCarousel && StageList && StageEntryClass ? CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(), StageEntryClass) : nullptr;
		// カルーセルではEntryを作らず、添字だけPresenterのカタログと揃えておきます。
		StageEntries.Add(Entry);
		if (!Entry) continue;
		Entry->OnFocused.AddUniqueDynamic(this, &UBoarLobbyWidget::SelectStage);
		Entry->OnChosen.AddUniqueDynamic(this, &UBoarLobbyWidget::ChooseStage);
		StageList->AddChild(Entry);
	}
	SetVisibility(ESlateVisibility::Visible);
	RefreshSelectedStage();
	bInitialFocusPending = true;
}

void UBoarLobbyWidget::RefreshSelectedStage()
{
	if (!Presenter) return;
	// Viewは整形済みのデータを描画します。SaveGameや解放条件を直接参照しません。
	const auto Data = Presenter->GetSelectedStage();
	for (int32 I = 0; I < Presenter->GetStageCount(); ++I)
	{
		if (!StageEntries.IsValidIndex(I) || !StageEntries[I]) continue;
		const auto Entry = Presenter->GetStage(I);
		StageEntries[I]->Setup(I, Entry.EntryLabel, Entry.bSelected);
		StageEntries[I]->SetAvailable(Entry.bUnlocked);
		StageEntries[I]->SetRenderOpacity(Entry.bUnlocked ? 1.0f : 0.45f);
	}
	if (PreviousButton) PreviousButton->SetIsEnabled(Presenter->CanStep(-1, bUseCarousel));
	if (NextButton) NextButton->SetIsEnabled(Presenter->CanStep(1, bUseCarousel));
	if (StageNameText) StageNameText->SetText(Data.DisplayName);
	if (StageDescriptionText) StageDescriptionText->SetText(Data.Description);
	if (TargetCaptureCountText) TargetCaptureCountText->SetText(Data.bValid ? FText::AsNumber(Data.TargetCaptureCount) : FText::GetEmpty());
	if (StageThumbnailImage)
	{
		UTexture2D* Thumbnail = Data.Thumbnail.LoadSynchronous();
		StageThumbnailImage->SetBrushFromTexture(Thumbnail, true);
		StageThumbnailImage->SetVisibility(Thumbnail ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (StartStageButton) StartStageButton->SetIsEnabled(Data.bCanStart);
	if (StageStatus) StageStatus->SetText(Data.Status);
	if (StageProgress) StageProgress->SetText(Data.Progress);
	if (bUseCarousel) RefreshCarousel();
}

void UBoarLobbyWidget::HideStageSelection()
{
	ReleasePreviews();
	if (Presenter) Presenter->Shutdown();
	bInitialFocusPending = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UBoarLobbyWidget::ResolveWidgetReferences()
{
	StageNameText = ResolveLobbyWidget<UTextBlock>(WidgetTree, StageNameTextWidgetName);
	StageDescriptionText = ResolveLobbyWidget<UTextBlock>(WidgetTree, StageDescriptionTextWidgetName);
	TargetCaptureCountText = ResolveLobbyWidget<UTextBlock>(WidgetTree, TargetCaptureCountTextWidgetName);
	StageThumbnailImage = ResolveLobbyWidget<UImage>(WidgetTree, StageThumbnailImageWidgetName);
	StartStageButton = ResolveLobbyWidget<UButton>(WidgetTree, StartStageButtonWidgetName);
	CancelButton = ResolveLobbyWidget<UButton>(WidgetTree, CancelButtonWidgetName);
}

void UBoarLobbyWidget::HandleStartStageClicked()
{
	// Presenterの開始判定を通った定義だけを通知し、保存とレベル遷移はStageEntranceに委ねます。
	if (UStageConfig* Stage = Presenter ? Presenter->RequestStart() : nullptr)
	{
		OnStageStartRequested.Broadcast(Stage);
	}
}

void UBoarLobbyWidget::HandleCancelClicked()
{
	HideStageSelection();
	OnStageSelectionClosed.Broadcast();
}

void UBoarLobbyWidget::OpenEncyclopedia()
{
	if (auto* PC = Cast<ABoarPlayerController>(GetOwningPlayer())) PC->OpenEncyclopedia(this, EncyclopediaButton);
}
void UBoarLobbyWidget::FocusEncyclopedia() { if (EncyclopediaButton) EncyclopediaButton->SetKeyboardFocus(); }

void UBoarLobbyWidget::SelectStage(int32 Index)
{
	if (!Presenter || !Presenter->TrySelect(Index)) return;
	RefreshSelectedStage();
	if (StageList && StageEntries.IsValidIndex(Index) && StageEntries[Index]) StageList->ScrollWidgetIntoView(StageEntries[Index], true);
}
void UBoarLobbyWidget::ChooseStage(int32 Index)
{
	SelectStage(Index);
	if (Presenter && Presenter->GetSelectedIndex() == Index) HandleStartStageClicked();
}
void UBoarLobbyWidget::StepStage(int32 Direction)
{
	if (!Presenter) return;
	const bool bHadPending = Presenter->IsTransitionPending();
	const bool bStepped = Presenter->RequestStep(Direction, bUseCarousel);
	if (bUseCarousel)
	{
		// 連続入力で前の移動先が確定した場合、中央カードを更新してから次をアニメーションします。
		if (bHadPending) RefreshSelectedStage();
		if (!bStepped) return;
		SlideDirection = Direction; CarouselElapsed = 0;
		if (StartStageButton) StartStageButton->SetIsEnabled(false);
		return;
	}
	if (bStepped)
	{
		SelectStage(Presenter->GetSelectedIndex());
		FocusSelection();
	}
}
void UBoarLobbyWidget::PreviousStage() { StepStage(-1); }
void UBoarLobbyWidget::NextStage() { StepStage(1); }
void UBoarLobbyWidget::FocusSelection()
{
	const int32 SelectedStageIndex = Presenter ? Presenter->GetSelectedIndex() : INDEX_NONE;
	if (StageEntries.IsValidIndex(SelectedStageIndex) && StageEntries[SelectedStageIndex]) StageEntries[SelectedStageIndex]->FocusEntry();
	else if (StartStageButton && StartStageButton->GetIsEnabled()) StartStageButton->SetKeyboardFocus();
	else if (CancelButton) CancelButton->SetKeyboardFocus();
}
void UBoarLobbyWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (bUseCarousel && Presenter && Presenter->IsTransitionPending()) AnimateCarousel(DeltaTime);
	// 表示の次のTickでフォーカスを設定し、生成直後のWidgetへの設定を避けます。
	if (bInitialFocusPending && IsVisible()) { bInitialFocusPending = false; FocusSelection(); }
}
FReply UBoarLobbyWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	// 押しっぱなしによる連続移動は抑え、対応キーは子Widgetへ伝播させずここで消費します。
	if (PreviousStageKeys.Contains(Event.GetKey())) { if (!Event.IsRepeat()) PreviousStage(); return FReply::Handled(); }
	if (NextStageKeys.Contains(Event.GetKey())) { if (!Event.IsRepeat()) NextStage(); return FReply::Handled(); }
	if (CancelKeys.Contains(Event.GetKey())) { if (!Event.IsRepeat()) HandleCancelClicked(); return FReply::Handled(); }
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
void UBoarLobbyWidget::ShowTravelError(const FText& Message) { if (StageStatus) StageStatus->SetText(Message); }

void UBoarLobbyWidget::RefreshCarousel()
{
	// 中央と左右のカードに必要なミニチュアだけを残し、範囲外のActorとMaterialを解放します。
	for (auto It = PreviewActors.CreateIterator(); It; ++It)
	{
		if (!Presenter->IsPreviewVisible(It.Key()))
		{
			if (IsValid(It.Value())) It.Value()->Destroy();
			PreviewBrushes.Remove(It.Key()); It.RemoveCurrent();
		}
	}
	const TCHAR* Names[] = {TEXT("Previous"), TEXT("Current"), TEXT("Next")};
	for (int32 I = 0; I < 3; ++I)
	{
		const FString Prefix = FString(TEXT("Carousel")) + Names[I];
		auto* Card = WidgetTree->FindWidget(FName(*Prefix));
		const auto Stage = Presenter->GetCarouselStage(I - 1);
		if (!Card) continue;
		Card->SetVisibility(Stage.bValid ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		Card->SetRenderTranslation(FVector2D::ZeroVector);
		Card->SetRenderScale(FVector2D(I == 1 ? 1.f : .72f));
		Card->SetRenderOpacity(I == 1 ? 1.f : I == 0 ? .58f : .38f);
		const bool bUnlocked = Stage.bUnlocked;
		if (auto* Label = ResolveLobbyWidget<UTextBlock>(WidgetTree, FName(*(Prefix + TEXT("Label")))))
			Label->SetText(!Stage.bValid ? FText::GetEmpty() : !bUnlocked ? FText::FromString(TEXT("???")) : I == 2 ? FText::FromName(Stage.StageId) : Stage.DisplayName);
		if (auto* Badge = ResolveLobbyWidget<UTextBlock>(WidgetTree, FName(*(Prefix + TEXT("Badge")))))
			Badge->SetText(Stage.Badge);
		if (auto* Lock = WidgetTree->FindWidget(FName(*(Prefix + TEXT("Lock"))))) Lock->SetVisibility(Stage.bValid && !bUnlocked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		UTexture2D* Thumbnail = Stage.Thumbnail.LoadSynchronous();
		UMaterialInstanceDynamic* Diorama = Stage.bValid ? GetDioramaBrush(Stage) : nullptr;
		if (Diorama) Diorama->SetScalarParameterValue(TEXT("Saturation"), !bUnlocked || I == 2 ? .18f : I == 0 ? .5f : 1.f);
		if (auto* Preview = ResolveLobbyWidget<UImage>(WidgetTree, FName(*(Prefix + TEXT("Image")))))
		{
			if (Diorama) Preview->SetBrushFromMaterial(Diorama);
			else Preview->SetBrushFromTexture(Thumbnail, false);
			Preview->SetVisibility(Diorama || Thumbnail ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			// 次のカードとロック中のカードは輪郭を残しつつ、明るい細部を抑えて表示します。
			Preview->SetColorAndOpacity(!bUnlocked || I == 2 ? FLinearColor(.5f,.62f,.78f,1) : I == 0 ? FLinearColor(.75f,.83f,.9f,1) : FLinearColor::White);
		}
		if (auto* Empty = WidgetTree->FindWidget(FName(*(Prefix + TEXT("Empty"))))) Empty->SetVisibility(Stage.bValid && !Thumbnail && !Diorama ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	const auto Selected = Presenter->GetSelectedStage();
	if (auto* Number = ResolveLobbyWidget<UTextBlock>(WidgetTree, TEXT("Text_StageNumber"))) Number->SetText(Selected.bValid ? FText::FromName(Selected.StageId) : FText::GetEmpty());
	if (!Selected.bUnlocked)
	{
		// ロック中の中央カードは閲覧できますが、詳細名・説明・進捗は伏せます。
		if (StageNameText) StageNameText->SetText(FText::FromString(TEXT("???")));
		if (StageDescriptionText) StageDescriptionText->SetText(FText::GetEmpty());
		if (TargetCaptureCountText) TargetCaptureCountText->SetText(FText::FromString(TEXT("—")));
		if (StageProgress) StageProgress->SetText(FText::GetEmpty());
		if (StageStatus) StageStatus->SetText(FText::FromString(TEXT("LOCKED")));
	}
}

UMaterialInstanceDynamic* UBoarLobbyWidget::GetDioramaBrush(const FBoarStageSelectViewData& Stage)
{
	if (!Stage.bValid || Stage.PreviewActorClass.IsNull() || !PreviewMaterial || !GetWorld()) return nullptr;
	// 再描画のたびにActorやRenderTargetを作り直さないよう、StageId単位で再利用します。
	if (auto* Existing = PreviewBrushes.Find(Stage.StageId)) return Existing->Get();
	UClass* Class = Stage.PreviewActorClass.LoadSynchronous();
	if (!Class) return nullptr;
	// ミニチュア撮影用の一時Actorを画面外に生成します。閉じる際はReleasePreviewsで破棄します。
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Actor = GetWorld()->SpawnActor<ABoarStagePreviewActor>(Class, FVector(0,0,-100000), FRotator::ZeroRotator, Params);
	if (!Actor) return nullptr;
	UTextureRenderTarget2D* Target = Actor->CreatePreview();
	if (!Target) { Actor->Destroy(); return nullptr; }
	auto* Brush = UMaterialInstanceDynamic::Create(PreviewMaterial, this);
	Brush->SetTextureParameterValue(TEXT("PreviewTexture"), Target);
	PreviewActors.Add(Stage.StageId, Actor); PreviewBrushes.Add(Stage.StageId, Brush);
	return Brush;
}

void UBoarLobbyWidget::ReleasePreviews()
{
	for (auto& Pair : PreviewActors) if (IsValid(Pair.Value)) Pair.Value->Destroy();
	PreviewActors.Empty(); PreviewBrushes.Empty();
}

void UBoarLobbyWidget::AnimateCarousel(float DeltaTime)
{
	CarouselElapsed += DeltaTime;
	const float T = FMath::Clamp(CarouselElapsed / FMath::Max(CarouselDuration, .05f), 0.f, 1.f);
	// Smoothstepで移動・拡縮・透明度の始点と終点をなめらかにつなぎます。
	const float A = T*T*(3.f-2.f*T);
	const TCHAR* Names[] = {TEXT("CarouselPrevious"), TEXT("CarouselCurrent"), TEXT("CarouselNext")};
	for (int32 I = 0; I < 3; ++I) if (auto* Card = WidgetTree->FindWidget(Names[I]))
	{
		Card->SetRenderTranslation(FVector2D(-SlideDirection * 470.f * A, 0));
		const float From = I == 1 ? 1.f : .72f;
		const float To = I - SlideDirection == 1 ? 1.f : .72f;
		Card->SetRenderScale(FVector2D(FMath::Lerp(From, To, A)));
		Card->SetRenderOpacity(FMath::Lerp(I == 1 ? 1.f : I == 0 ? .58f : .38f, I - SlideDirection == 1 ? 1.f : .38f, A));
	}
	if (T >= 1.f)
	{
		// 見た目の移動が完了してから選択を確定し、開始可否とフォーカスを更新します。
		Presenter->CommitPendingSelection();
		RefreshSelectedStage();
		FocusSelection();
	}
}
