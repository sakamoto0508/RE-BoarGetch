#include "UI/BoarArchiveSelectionWidget.h"
#include "UI/BoarLoadoutEntry.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Core/BoarGameInstance.h"
#include "Save/BoarSaveGame.h"
#include "Stage/StageConfig.h"
#include "InputCoreTypes.h"

void UBoarArchiveSelectionWidget::NativeConstruct()
{
    Super::NativeConstruct();
    auto* GI=GetGameInstance<UBoarGameInstance>();
    PendingIds=GI?GI->GetArchiveDisplayIds():TArray<FName>();PendingIds.SetNum(3);
    Candidates.Reset();Candidates.Add(NAME_None);
    if(const auto* Save=GI?GI->GetProgress():nullptr)
    {
        auto Captured=Save->CapturedBoarUniqueIds.Array();
        Captured.Sort([](FName A,FName B){return A.LexicalLess(B);});
        for(FName Id:Captured)if(!Id.IsNone())Candidates.Add(Id);
    }
    SlotRows.Reset();CandidateRows.Reset();
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Root;
    auto* Backdrop=WidgetTree->ConstructWidget<UBorder>();Backdrop->SetBrushColor(FLinearColor(0.003f,0.008f,0.02f,0.68f));
    auto* BG=Root->AddChildToCanvas(Backdrop);BG->SetAnchors(FAnchors(0,0,1,1));BG->SetOffsets(FMargin(0));
    auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor(0.008f,0.035f,0.075f,0.98f));Frame->SetPadding(FMargin(32));
    auto* Panel=Root->AddChildToCanvas(Frame);Panel->SetAnchors(FAnchors(0.5f,0.5f));Panel->SetAlignment(FVector2D(0.5f,0.5f));Panel->SetSize(FVector2D(1120,720));
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Frame->SetContent(Column);
    auto Text=[&](const FString& Value,int32 Size,FLinearColor Color)
    {
        auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(Value));T->SetColorAndOpacity(FSlateColor(Color));
        auto Font=T->GetFont();Font.Size=Size;T->SetFont(Font);return T;
    };
    Column->AddChildToVerticalBox(Text(TEXT("BOAR DISPLAY / 展示選択"),30,FLinearColor(0.22f,0.88f,1)))->SetPadding(FMargin(0,0,0,16));
    Column->AddChildToVerticalBox(Text(TEXT("展示枠を選び、捕獲済みBoarを割り当ててください。空欄も保存できます。"),19,FLinearColor::White))->SetPadding(FMargin(0,0,0,20));
    auto* Rows=WidgetTree->ConstructWidget<UHorizontalBox>();Column->AddChildToVerticalBox(Rows)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* Slots=WidgetTree->ConstructWidget<UVerticalBox>();auto* Left=Rows->AddChildToHorizontalBox(Slots);Left->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Left->SetPadding(FMargin(0,0,24,0));
    Slots->AddChildToVerticalBox(Text(TEXT("DISPLAY SLOT"),19,FLinearColor(0.25f,0.9f,1)))->SetPadding(FMargin(0,0,0,16));
    auto* Right=WidgetTree->ConstructWidget<UVerticalBox>();Rows->AddChildToHorizontalBox(Right)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Right->AddChildToVerticalBox(Text(TEXT("CAPTURED BOARS / 捕獲済み"),19,FLinearColor(0.25f,0.9f,1)))->SetPadding(FMargin(0,0,0,16));
    auto* List=WidgetTree->ConstructWidget<UScrollBox>();Right->AddChildToVerticalBox(List)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    if(EntryClass)
    {
        for(int32 I=0;I<3;++I)
        {
            auto* E=CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(),EntryClass);if(!E)continue;
            E->OnChosen.AddUniqueDynamic(this,&UBoarArchiveSelectionWidget::SelectSlot);
            Slots->AddChildToVerticalBox(E)->SetPadding(FMargin(0,0,0,14));SlotRows.Add(E);
        }
        for(int32 I=0;I<Candidates.Num();++I)
        {
            auto* E=CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(),EntryClass);if(!E)continue;
            E->Setup(I,LabelFor(Candidates[I]));E->OnChosen.AddUniqueDynamic(this,&UBoarArchiveSelectionWidget::ChooseBoar);
            List->AddChild(E);CandidateRows.Add(E);
        }
    }
    Status=Text(Candidates.Num()==1?TEXT("捕獲済みBoarはまだありません。展示枠は空のままです。"):TEXT("確定すると、この3枠をすぐに保存します。"),18,FLinearColor(0.9f,0.94f,1));
    Status->SetAutoWrapText(true);Column->AddChildToVerticalBox(Status)->SetPadding(FMargin(0,18,0,16));
    auto* Footer=WidgetTree->ConstructWidget<UHorizontalBox>();Column->AddChildToVerticalBox(Footer);
    auto* SaveButton=WidgetTree->ConstructWidget<UButton>();SaveButton->SetContent(Text(TEXT("保存して閉じる"),22,FLinearColor(0.98f,0.8f,0.2f)));
    SaveButton->OnClicked.AddUniqueDynamic(this,&UBoarArchiveSelectionWidget::Confirm);Footer->AddChildToHorizontalBox(SaveButton)->SetPadding(FMargin(0,0,24,0));
    auto* Back=WidgetTree->ConstructWidget<UButton>();Back->SetContent(Text(TEXT("戻る / 変更を破棄"),22,FLinearColor(0.5f,0.9f,1)));
    Back->OnClicked.AddUniqueDynamic(this,&UBoarArchiveSelectionWidget::Cancel);Footer->AddChildToHorizontalBox(Back);
    RefreshSlotRows();
}
FText UBoarArchiveSelectionWidget::LabelFor(FName Id) const
{
    if(Id.IsNone())return FText::FromString(TEXT("空欄"));
    for(const auto& S:StageCatalog)if(S)for(const auto& D:S->BoarSpawnDefinitions)
        if(D.BoarUniqueId==Id && !D.EncyclopediaName.IsEmpty())return FText::Format(FText::FromString(TEXT("{0} [{1}]")),D.EncyclopediaName,FText::FromName(Id));
    return FText::FromName(Id);
}
void UBoarArchiveSelectionWidget::RefreshSlotRows()
{
    for(int32 I=0;I<SlotRows.Num();++I)SlotRows[I]->Setup(I,FText::Format(FText::FromString(TEXT("Slot {0}  /  {1}")),I,LabelFor(PendingIds[I])),ActiveSlot==I);
}
void UBoarArchiveSelectionWidget::SelectSlot(int32 Index)
{
    if(!PendingIds.IsValidIndex(Index))return;
    ActiveSlot=Index;RefreshSlotRows();if(!CandidateRows.IsEmpty())CandidateRows[0]->FocusEntry();
}
void UBoarArchiveSelectionWidget::ChooseBoar(int32 Index)
{
    if(!Candidates.IsValidIndex(Index))return;
    PendingIds[ActiveSlot]=Candidates[Index];RefreshSlotRows();if(SlotRows.IsValidIndex(ActiveSlot))SlotRows[ActiveSlot]->FocusEntry();
}
void UBoarArchiveSelectionWidget::Confirm()
{
    auto* GI=GetGameInstance<UBoarGameInstance>();
    if(GI && GI->SaveArchiveDisplays(PendingIds))OnClosed.Broadcast();
    else if(Status)Status->SetText(FText::FromString(TEXT("保存できませんでした。変更は確定していません。")));
}
void UBoarArchiveSelectionWidget::Cancel(){OnClosed.Broadcast();}
void UBoarArchiveSelectionWidget::FocusInitialChoice(){if(!SlotRows.IsEmpty())SlotRows[0]->FocusEntry();}
FReply UBoarArchiveSelectionWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right){Cancel();return FReply::Handled();}
    return Super::NativeOnKeyDown(Geometry,Event);
}
