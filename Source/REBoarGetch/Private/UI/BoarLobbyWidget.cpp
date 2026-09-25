#include "UI/BoarLobbyWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Stage/StageConfig.h"
#include "Player/BoarPlayerController.h"
#include "UI/BoarLoadoutEntry.h"
#include "Components/ScrollBox.h"
#include "BoarGameInstance.h"
#include "BoarSaveGame.h"
#include "Components/CanvasPanelSlot.h"

namespace
{
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
	ShowStageCatalog({StageConfig}, StageConfig);
}

bool UBoarLobbyWidget::IsUnlocked(const UStageConfig* Stage) const
{
	const auto* GI = GetGameInstance<UBoarGameInstance>();
	return Stage && (GI ? GI->IsStageUnlocked(Stage) : Stage->UnlockCondition.RequiredClearedStageId.IsNone());
}

void UBoarLobbyWidget::ShowStageCatalog(const TArray<UStageConfig*>& Stages, UStageConfig* FallbackStage)
{
	PendingStageIndex = INDEX_NONE;
	for (const auto& Entry : StageEntries) if (Entry)
	{
		Entry->OnFocused.RemoveDynamic(this, &UBoarLobbyWidget::SelectStage);
		Entry->OnChosen.RemoveDynamic(this, &UBoarLobbyWidget::ChooseStage);
	}
	StageEntries.Reset(); AvailableStages.Reset();
	if (StageList) StageList->ClearChildren();
	TSet<FName> Seen;
	for (UStageConfig* Stage : Stages)
	{
		if (!Stage || Stage->StageId.IsNone() || Seen.Contains(Stage->StageId)) continue;
		Seen.Add(Stage->StageId); AvailableStages.Add(Stage);
	}
	const auto* GI = GetGameInstance<UBoarGameInstance>();
	const auto* Save = GI ? GI->GetProgress() : nullptr;
	SelectedStageIndex = INDEX_NONE;
	for (int32 I = 0; I < AvailableStages.Num(); ++I)
		if (Save && AvailableStages[I]->StageId == Save->LastAttemptedStageId && IsUnlocked(AvailableStages[I])) SelectedStageIndex = I;
	if (SelectedStageIndex == INDEX_NONE && IsUnlocked(FallbackStage)) SelectedStageIndex = AvailableStages.IndexOfByKey(FallbackStage);
	if (SelectedStageIndex == INDEX_NONE)
		for (int32 I = 0; I < AvailableStages.Num(); ++I) if (IsUnlocked(AvailableStages[I])) { SelectedStageIndex = I; break; }
	for (int32 I = 0; I < AvailableStages.Num(); ++I)
	{
		UBoarLoadoutEntry* Entry = !bUseCarousel && StageList && StageEntryClass ? CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(), StageEntryClass) : nullptr;
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
	SelectedStageConfig = AvailableStages.IsValidIndex(SelectedStageIndex) ? AvailableStages[SelectedStageIndex] : nullptr;
	UStageConfig* StageConfig = SelectedStageConfig;
	const auto* GI = GetGameInstance<UBoarGameInstance>();
	const auto* Save = GI ? GI->GetProgress() : nullptr;
	int32 UnlockedCount = 0;
	for (int32 I = 0; I < AvailableStages.Num(); ++I)
	{
		const UStageConfig* Stage = AvailableStages[I]; const bool bUnlocked = IsUnlocked(Stage);
		UnlockedCount += bUnlocked ? 1 : 0;
		if (!StageEntries.IsValidIndex(I) || !StageEntries[I]) continue;
		const FString State = !bUnlocked ? TEXT("LOCKED") : Save && Save->ClearedStageIds.Contains(Stage->StageId) ? TEXT("CLEAR") : TEXT("未クリア");
		const FText Label = FText::Format(NSLOCTEXT("StageSelect", "Entry", "{0}\n{1}"), Stage->DisplayName.IsEmpty() ? FText::FromName(Stage->StageId) : Stage->DisplayName, FText::FromString(State));
		StageEntries[I]->Setup(I, Label, I == SelectedStageIndex);
		StageEntries[I]->SetAvailable(bUnlocked);
		StageEntries[I]->SetRenderOpacity(bUnlocked ? 1.0f : 0.45f);
	}
	if (PreviousButton) PreviousButton->SetIsEnabled(bUseCarousel ? SelectedStageIndex > 0 : UnlockedCount > 1);
	if (NextButton) NextButton->SetIsEnabled(bUseCarousel ? SelectedStageIndex + 1 < AvailableStages.Num() : UnlockedCount > 1);

	if (StageNameText)
	{
		StageNameText->SetText(StageConfig ? StageConfig->DisplayName : FText::GetEmpty());
	}
	if (StageDescriptionText)
	{
		StageDescriptionText->SetText(StageConfig ? StageConfig->Description : FText::GetEmpty());
	}
	if (TargetCaptureCountText)
	{
		TargetCaptureCountText->SetText(StageConfig
			? FText::AsNumber(StageConfig->TargetCaptureCount)
			: FText::GetEmpty());
	}
	if (StageThumbnailImage)
	{
		UTexture2D* Thumbnail = StageConfig ? StageConfig->Thumbnail.LoadSynchronous() : nullptr;
		StageThumbnailImage->SetBrushFromTexture(Thumbnail, true);
		StageThumbnailImage->SetVisibility(
			Thumbnail ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (StartStageButton)
	{
		StartStageButton->SetIsEnabled(IsUnlocked(StageConfig) && !StageConfig->Level.IsNull());
	}
	if (StageStatus) StageStatus->SetText(!StageConfig ? NSLOCTEXT("StageSelect", "NoStage", "選択できるステージがありません")
		: StageConfig->Level.IsNull() ? NSLOCTEXT("StageSelect", "NoLevel", "出発先未登録")
		: Save && Save->ClearedStageIds.Contains(StageConfig->StageId) ? FText::FromString(TEXT("CLEAR")) : NSLOCTEXT("StageSelect", "NotClear", "未クリア"));
	if (StageProgress)
	{
		TSet<FName> Coins, Boars;
		if (StageConfig)
		{
			for (const auto& Def : StageConfig->SpecialCoinDefinitions) if (!Def.SpecialCoinId.IsNone()) Coins.Add(Def.SpecialCoinId);
			for (const auto& Def : StageConfig->BoarSpawnDefinitions) if (!Def.BoarUniqueId.IsNone()) Boars.Add(Def.BoarUniqueId);
		}
		int32 CoinsFound = 0, BoarsFound = 0;
		for (FName Id : Coins) if (Save && Save->SpecialCoinIds.Contains(Id)) ++CoinsFound;
		for (FName Id : Boars) if (Save && Save->CapturedBoarUniqueIds.Contains(Id)) ++BoarsFound;
		const FText BoarProgress = Boars.IsEmpty() ? NSLOCTEXT("StageSelect", "NoIndividuals", "個体データ未登録")
			: FText::Format(NSLOCTEXT("StageSelect", "Count", "{0} / {1}"), BoarsFound, Boars.Num());
		StageProgress->SetText(StageConfig ? FText::Format(NSLOCTEXT("StageSelect", "Progress", "特別コイン：{0} / {1}\n図鑑：{2}"), CoinsFound, Coins.Num(), BoarProgress) : FText::GetEmpty());
	}
	if (bUseCarousel) RefreshCarousel();
}

void UBoarLobbyWidget::HideStageSelection()
{
	PendingStageIndex = INDEX_NONE;
	bInitialFocusPending = false;
	SelectedStageConfig = nullptr;
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
	if (PendingStageIndex != INDEX_NONE) return;
	if (IsUnlocked(SelectedStageConfig) && !SelectedStageConfig->Level.IsNull())
	{
		OnStageStartRequested.Broadcast(SelectedStageConfig);
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
	if (!AvailableStages.IsValidIndex(Index) || !IsUnlocked(AvailableStages[Index])) return;
	SelectedStageIndex = Index; RefreshSelectedStage();
	if (StageList && StageEntries.IsValidIndex(Index) && StageEntries[Index]) StageList->ScrollWidgetIntoView(StageEntries[Index], true);
}
void UBoarLobbyWidget::ChooseStage(int32 Index) { SelectStage(Index); if (SelectedStageIndex == Index) HandleStartStageClicked(); }
void UBoarLobbyWidget::StepStage(int32 Direction)
{
	if (bUseCarousel)
	{
		// Finish the prior request before accepting another; details always match the committed center.
		if (PendingStageIndex != INDEX_NONE)
		{
			SelectedStageIndex = PendingStageIndex; PendingStageIndex = INDEX_NONE; RefreshSelectedStage();
		}
		const int32 Next = SelectedStageIndex + Direction;
		if (!AvailableStages.IsValidIndex(Next)) return;
		PendingStageIndex = Next; SlideDirection = Direction; CarouselElapsed = 0;
		if (StartStageButton) StartStageButton->SetIsEnabled(false);
		return;
	}
	const int32 Count = AvailableStages.Num();
	for (int32 Step = 1; Step <= Count; ++Step)
	{
		const int32 Index = (FMath::Max(SelectedStageIndex, 0) + Direction * Step + Count) % Count;
		if (IsUnlocked(AvailableStages[Index])) { SelectStage(Index); FocusSelection(); return; }
	}
}
void UBoarLobbyWidget::PreviousStage() { StepStage(-1); }
void UBoarLobbyWidget::NextStage() { StepStage(1); }
void UBoarLobbyWidget::FocusSelection()
{
	if (StageEntries.IsValidIndex(SelectedStageIndex) && StageEntries[SelectedStageIndex]) StageEntries[SelectedStageIndex]->FocusEntry();
	else if (StartStageButton && StartStageButton->GetIsEnabled()) StartStageButton->SetKeyboardFocus();
	else if (CancelButton) CancelButton->SetKeyboardFocus();
}
void UBoarLobbyWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (bUseCarousel && PendingStageIndex != INDEX_NONE) AnimateCarousel(DeltaTime);
	if (bInitialFocusPending && IsVisible()) { bInitialFocusPending = false; FocusSelection(); }
}
FReply UBoarLobbyWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (PreviousStageKeys.Contains(Event.GetKey())) { if (!Event.IsRepeat()) PreviousStage(); return FReply::Handled(); }
	if (NextStageKeys.Contains(Event.GetKey())) { if (!Event.IsRepeat()) NextStage(); return FReply::Handled(); }
	if (CancelKeys.Contains(Event.GetKey())) { if (!Event.IsRepeat()) HandleCancelClicked(); return FReply::Handled(); }
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
void UBoarLobbyWidget::ShowTravelError(const FText& Message) { if (StageStatus) StageStatus->SetText(Message); }

void UBoarLobbyWidget::RefreshCarousel()
{
	const auto* GI = GetGameInstance<UBoarGameInstance>();
	const auto* Save = GI ? GI->GetProgress() : nullptr;
	const TCHAR* Names[] = {TEXT("Previous"), TEXT("Current"), TEXT("Next")};
	for (int32 I = 0; I < 3; ++I)
	{
		const FString Prefix = FString(TEXT("Carousel")) + Names[I];
		auto* Card = WidgetTree->FindWidget(FName(*Prefix));
		const int32 Index = SelectedStageIndex + I - 1;
		const UStageConfig* Stage = AvailableStages.IsValidIndex(Index) ? AvailableStages[Index].Get() : nullptr;
		if (!Card) continue;
		Card->SetVisibility(Stage ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		Card->SetRenderTranslation(FVector2D::ZeroVector);
		Card->SetRenderScale(FVector2D(I == 1 ? 1.f : .72f));
		Card->SetRenderOpacity(I == 1 ? 1.f : I == 0 ? .58f : .38f);
		const bool bUnlocked = IsUnlocked(Stage);
		if (auto* Label = ResolveLobbyWidget<UTextBlock>(WidgetTree, FName(*(Prefix + TEXT("Label")))))
			Label->SetText(!Stage ? FText::GetEmpty() : !bUnlocked ? FText::FromString(TEXT("???")) : I == 2 ? FText::FromName(Stage->StageId) : Stage->DisplayName);
		if (auto* Badge = ResolveLobbyWidget<UTextBlock>(WidgetTree, FName(*(Prefix + TEXT("Badge")))))
			Badge->SetText(!Stage ? FText::GetEmpty() : !bUnlocked ? FText::FromString(TEXT("LOCKED")) : Save && Save->ClearedStageIds.Contains(Stage->StageId) ? FText::FromString(TEXT("CLEAR")) : FText::GetEmpty());
		if (auto* Lock = WidgetTree->FindWidget(FName(*(Prefix + TEXT("Lock"))))) Lock->SetVisibility(Stage && !bUnlocked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		UTexture2D* Thumbnail = Stage ? Stage->Thumbnail.LoadSynchronous() : nullptr;
		if (auto* Preview = ResolveLobbyWidget<UImage>(WidgetTree, FName(*(Prefix + TEXT("Image")))))
		{
			Preview->SetBrushFromTexture(Thumbnail, false);
			Preview->SetVisibility(Thumbnail ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			// Preserve recognizable outlines without exposing bright detail on the next/locked preview.
			Preview->SetColorAndOpacity(!bUnlocked || I == 2 ? FLinearColor(.12f,.2f,.27f,1) : I == 0 ? FLinearColor(.45f,.55f,.6f,1) : FLinearColor::White);
		}
		if (auto* Empty = WidgetTree->FindWidget(FName(*(Prefix + TEXT("Empty"))))) Empty->SetVisibility(Stage && !Thumbnail ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (auto* Number = ResolveLobbyWidget<UTextBlock>(WidgetTree, TEXT("Text_StageNumber"))) Number->SetText(SelectedStageConfig ? FText::FromName(SelectedStageConfig->StageId) : FText::GetEmpty());
	if (!IsUnlocked(SelectedStageConfig))
	{
		if (StageNameText) StageNameText->SetText(FText::FromString(TEXT("???")));
		if (StageDescriptionText) StageDescriptionText->SetText(FText::GetEmpty());
		if (TargetCaptureCountText) TargetCaptureCountText->SetText(FText::FromString(TEXT("—")));
		if (StageProgress) StageProgress->SetText(FText::GetEmpty());
		if (StageStatus) StageStatus->SetText(FText::FromString(TEXT("LOCKED")));
	}
}

void UBoarLobbyWidget::AnimateCarousel(float DeltaTime)
{
	CarouselElapsed += DeltaTime;
	const float T = FMath::Clamp(CarouselElapsed / FMath::Max(CarouselDuration, .05f), 0.f, 1.f);
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
		SelectedStageIndex = PendingStageIndex; PendingStageIndex = INDEX_NONE;
		RefreshSelectedStage();
		FocusSelection();
	}
}
