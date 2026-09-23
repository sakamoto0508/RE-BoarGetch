#include "UI/BoarEncyclopediaWidget.h"
#include "UI/BoarLoadoutEntry.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Stage/StageConfig.h"
#include "Boar/BoarBase.h"
#include "Core/BoarGameInstance.h"
#include "Save/BoarSaveGame.h"
#include "Engine/Texture2D.h"

namespace
{
	FText StageLabel(const UStageConfig* Stage)
	{
		return Stage->DisplayName.IsEmpty() ? FText::FromName(Stage->StageId) : Stage->DisplayName;
	}
	FText RegisteredText(const FText& Text)
	{
		return Text.IsEmpty() ? FText::FromString(TEXT("未登録")) : Text;
	}
}

void UBoarEncyclopediaWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bInitialFocusPending = true;
	if (!WidgetTree) return;
	List = Cast<UScrollBox>(WidgetTree->FindWidget(ListWidgetName));
	ProgressText = Cast<UTextBlock>(WidgetTree->FindWidget(ProgressWidgetName));
	TypeProgressText = Cast<UTextBlock>(WidgetTree->FindWidget(TypeProgressWidgetName));
	DetailText = Cast<UTextBlock>(WidgetTree->FindWidget(DetailWidgetName));
	Photo = Cast<UImage>(WidgetTree->FindWidget(PhotoWidgetName));
	PhotoStatus = Cast<UTextBlock>(WidgetTree->FindWidget(PhotoStatusWidgetName));
	BackButton = Cast<UButton>(WidgetTree->FindWidget(BackButtonName));
	if (BackButton)
	{
		BackButton->OnClicked.AddUniqueDynamic(this, &UBoarEncyclopediaWidget::Close);
		BackButton->OnHovered.AddUniqueDynamic(this, &UBoarEncyclopediaWidget::FocusBack);
	}
	BuildEntries();
}

void UBoarEncyclopediaWidget::BuildEntries()
{
	Definitions.Reset(); Entries.Reset(); Labels.Reset();
	if (List) List->ClearChildren();
	const UBoarGameInstance* GI = GetGameInstance<UBoarGameInstance>();
	const UBoarSaveGame* Save = GI ? GI->GetProgress() : nullptr;
	TMap<FName, int32> IdCounts;
	for (const UStageConfig* Stage : StageCatalog)
		if (Stage) for (const auto& Def : Stage->BoarSpawnDefinitions)
			if (!Def.BoarUniqueId.IsNone()) ++IdCounts.FindOrAdd(Def.BoarUniqueId);
	int32 Captured = 0;
	TMap<EBoarArchetype, FIntPoint> TypeCounts;
	for (int32 S = 0; S < StageCatalog.Num(); ++S)
	{
		const UStageConfig* Stage = StageCatalog[S];
		if (!Stage || Stage->StageId.IsNone()) continue;
		for (int32 D = 0; D < Stage->BoarSpawnDefinitions.Num(); ++D)
		{
			const auto& Def = Stage->BoarSpawnDefinitions[D];
			if (Def.BoarUniqueId.IsNone() || IdCounts.FindRef(Def.BoarUniqueId) != 1 || !Def.BoarClass) continue;
			const bool bCaptured = Save && Save->CapturedBoarUniqueIds.Contains(Def.BoarUniqueId);
			Captured += bCaptured ? 1 : 0;
			const EBoarArchetype Type = Def.BoarClass.GetDefaultObject()->GetBoarArchetype();
			FIntPoint& Counts = TypeCounts.FindOrAdd(Type, FIntPoint::ZeroValue);
			Counts.X += bCaptured ? 1 : 0; ++Counts.Y;
			const int32 Index = Definitions.Add(FIntPoint(S, D));
			Labels.Add(FText::Format(NSLOCTEXT("BoarEncyclopedia", "Row", "{0}  /  {1}"), StageLabel(Stage),
				bCaptured ? RegisteredText(Def.EncyclopediaName) : FText::FromString(TEXT("???"))));
			Entries.Add(nullptr);
			if (!List || !EntryClass) continue;
			auto* Entry = CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(), EntryClass);
			if (!Entry) continue;
			Entry->Setup(Index, Labels.Last());
			Entry->OnChosen.AddUniqueDynamic(this, &UBoarEncyclopediaWidget::SelectEntry);
			Entry->OnFocused.AddUniqueDynamic(this, &UBoarEncyclopediaWidget::SelectEntry);
			List->AddChild(Entry); Entries[Index] = Entry;
		}
	}
	if (ProgressText) ProgressText->SetText(Definitions.IsEmpty()
		? NSLOCTEXT("BoarEncyclopedia", "Empty", "図鑑データはまだ登録されていません")
		: FText::Format(NSLOCTEXT("BoarEncyclopedia", "Progress", "捕獲済み {0} / {1}"), Captured, Definitions.Num()));
	FString Summary;
	TArray<EBoarArchetype> Types; TypeCounts.GetKeys(Types); Types.Sort();
	for (EBoarArchetype Type : Types)
	{
		const FIntPoint Counts = TypeCounts[Type];
		// 未捕獲種の名前・母数はまだ公開しない。
		if (Counts.X == 0) continue;
		Summary += FString::Printf(TEXT("%s  %d / %d\n"), *StaticEnum<EBoarArchetype>()->GetDisplayNameTextByValue(static_cast<int64>(Type)).ToString(), Counts.X, Counts.Y);
	}
	if (TypeProgressText) TypeProgressText->SetText(FText::FromString(Summary));
	if (Photo) { Photo->SetBrushFromTexture(nullptr); Photo->SetVisibility(ESlateVisibility::Hidden); }
	if (PhotoStatus) PhotoStatus->SetText(FText::GetEmpty());
	if (DetailText) DetailText->SetText(FText::GetEmpty());
	if (!Definitions.IsEmpty()) SelectEntry(0);
}

void UBoarEncyclopediaWidget::SelectEntry(int32 Index)
{
	if (!Definitions.IsValidIndex(Index)) return;
	const FIntPoint Location = Definitions[Index];
	const UStageConfig* Stage = StageCatalog[Location.X];
	const auto& Def = Stage->BoarSpawnDefinitions[Location.Y];
	const UBoarGameInstance* GI = GetGameInstance<UBoarGameInstance>();
	const UBoarSaveGame* Save = GI ? GI->GetProgress() : nullptr;
	const bool bCaptured = Save && Save->CapturedBoarUniqueIds.Contains(Def.BoarUniqueId);
	for (int32 I = 0; I < Entries.Num(); ++I) if (Entries[I]) Entries[I]->Setup(I, Labels[I], I == Index);
	if (Entries.IsValidIndex(Index) && Entries[Index] && List) List->ScrollWidgetIntoView(Entries[Index], true);
	FText Detail;
	if (!bCaptured)
	{
		Detail = FText::Format(NSLOCTEXT("BoarEncyclopedia", "Hidden", "出現Stage：{0}\n\n名前：???\n種類：???\n\n説明：???\n特徴：???"), StageLabel(Stage));
	}
	else
	{
		const FText Type = StaticEnum<EBoarArchetype>()->GetDisplayNameTextByValue(static_cast<int64>(Def.BoarClass.GetDefaultObject()->GetBoarArchetype()));
		Detail = FText::Format(NSLOCTEXT("BoarEncyclopedia", "Details", "{0}\n種類：{1} / 捕獲済み\n初捕獲Stage：{2}\n\n{3}\n\n特徴：{4}"),
			RegisteredText(Def.EncyclopediaName), Type, StageLabel(Stage), RegisteredText(Def.EncyclopediaDescription), RegisteredText(Def.EncyclopediaTraits));
	}
	if (DetailText) DetailText->SetText(Detail);
	UTexture2D* Texture = bCaptured ? Def.EncyclopediaPhoto.LoadSynchronous() : nullptr;
	if (Photo)
	{
		Photo->SetBrushFromTexture(Texture);
		Photo->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (PhotoStatus) PhotoStatus->SetText(Texture ? FText::GetEmpty() : FText::FromString(bCaptured ? TEXT("写真未登録") : TEXT("???")));
}

void UBoarEncyclopediaWidget::NativeDestruct()
{
	for (const auto& Entry : Entries)
	{
		if (!Entry) continue;
		Entry->OnChosen.RemoveDynamic(this, &UBoarEncyclopediaWidget::SelectEntry);
		Entry->OnFocused.RemoveDynamic(this, &UBoarEncyclopediaWidget::SelectEntry);
	}
	if (BackButton)
	{
		BackButton->OnClicked.RemoveDynamic(this, &UBoarEncyclopediaWidget::Close);
		BackButton->OnHovered.RemoveDynamic(this, &UBoarEncyclopediaWidget::FocusBack);
	}
	Super::NativeDestruct();
}
void UBoarEncyclopediaWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (!bInitialFocusPending) return;
	bInitialFocusPending = false; FocusInitialChoice();
}
void UBoarEncyclopediaWidget::FocusInitialChoice() { if (!Entries.IsEmpty() && Entries[0]) Entries[0]->FocusEntry(); else FocusBack(); }
void UBoarEncyclopediaWidget::FocusBack() { if (BackButton) BackButton->SetKeyboardFocus(); }
void UBoarEncyclopediaWidget::Close() { OnClosed.Broadcast(); }
