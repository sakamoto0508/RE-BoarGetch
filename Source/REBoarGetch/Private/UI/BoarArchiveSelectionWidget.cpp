#include "UI/BoarArchiveSelectionWidget.h"
#include "UI/BoarArchiveCard.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Core/BoarGameInstance.h"
#include "Save/BoarSaveGame.h"
#include "Stage/StageConfig.h"
#include "Boar/BoarBase.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"

namespace
{
 const FLinearColor Cyan(0.12f,0.74f,0.93f), Amber(1,0.68f,0.08f), Navy(0.006f,0.018f,0.042f);
 const FBoarSpawnDefinition* Definition(const TArray<TObjectPtr<UStageConfig>>& Stages,FName Id,const UStageConfig*& FoundStage)
 {
  FoundStage=nullptr; if(Id.IsNone())return nullptr;
  for(const auto& S:Stages)if(S)for(const auto& D:S->BoarSpawnDefinitions)if(D.BoarUniqueId==Id){FoundStage=S;return &D;}
  return nullptr;
 }
}
UBoarArchiveSelectionWidget::UBoarArchiveSelectionWidget(const FObjectInitializer& O):Super(O){SetIsFocusable(true);}
TSharedRef<SWidget> UBoarArchiveSelectionWidget::RebuildWidget()
{
 // SlateがRootWidgetを取り込む前に構築します。NativeConstructでは構築済みのルートを差し替えません。
 auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Root;
 auto* Backdrop=WidgetTree->ConstructWidget<UBorder>();Backdrop->SetBrushColor(FLinearColor(0.002f,0.006f,0.015f,0.75f));
 auto* BG=Root->AddChildToCanvas(Backdrop);BG->SetAnchors(FAnchors(0,0,1,1));BG->SetOffsets(FMargin(0));
 auto* Scale=WidgetTree->ConstructWidget<UScaleBox>();Scale->SetStretch(EStretch::ScaleToFit);
 auto* View=Root->AddChildToCanvas(Scale);View->SetAnchors(FAnchors(0.04f,0.06f,0.96f,0.94f));View->SetOffsets(FMargin(0));
 auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(1700);Size->SetHeightOverride(900);Scale->SetContent(Size);
 auto* Rim=WidgetTree->ConstructWidget<UBorder>();Rim->SetBrushColor(Cyan);Rim->SetPadding(FMargin(3));Size->SetContent(Rim);
 auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(Navy);Frame->SetPadding(FMargin(36));Rim->SetContent(Frame);
 auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Frame->SetContent(Column);
 auto Text=[&](const TCHAR* Value,int32 Size,FLinearColor Color){auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(Value));T->SetColorAndOpacity(FSlateColor(Color));auto F=T->GetFont();F.Size=Size;T->SetFont(F);T->SetAutoWrapText(true);return T;};
 Column->AddChildToVerticalBox(Text(TEXT("展示ボア選択"),38,FLinearColor::White));
 Column->AddChildToVerticalBox(Text(TEXT("アーカイブに展示するボアを選択してください"),20,Cyan))->SetPadding(FMargin(0,6,0,26));
 auto* Main=WidgetTree->ConstructWidget<UHorizontalBox>();Column->AddChildToVerticalBox(Main)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 auto* Left=WidgetTree->ConstructWidget<UVerticalBox>();auto* L=Main->AddChildToHorizontalBox(Left);FSlateChildSize LeftFill(ESlateSizeRule::Fill);LeftFill.Value=0.44f;L->SetSize(LeftFill);L->SetPadding(FMargin(0,0,32,0));
 Left->AddChildToVerticalBox(Text(TEXT("DISPLAY SLOTS  /  展示枠"),23,Cyan))->SetPadding(FMargin(0,0,0,18));
 SlotsPanel=WidgetTree->ConstructWidget<UHorizontalBox>();Left->AddChildToVerticalBox(SlotsPanel);
 if(IsDesignTime())for(int32 I=0;I<3;++I)
 {
  // デザイナー用の空枠表示です。捕獲履歴や保存済み展示には登録しません。
  auto* PreviewSize=WidgetTree->ConstructWidget<USizeBox>();PreviewSize->SetWidthOverride(206);PreviewSize->SetHeightOverride(242);
  auto* PreviewRim=WidgetTree->ConstructWidget<UBorder>();PreviewRim->SetBrushColor(I==0?Amber:Cyan);PreviewRim->SetPadding(FMargin(4));PreviewSize->SetContent(PreviewRim);
  auto* PreviewBody=WidgetTree->ConstructWidget<UBorder>();PreviewBody->SetBrushColor(Navy);PreviewBody->SetPadding(FMargin(16));PreviewRim->SetContent(PreviewBody);
  auto* PreviewColumn=WidgetTree->ConstructWidget<UVerticalBox>();PreviewBody->SetContent(PreviewColumn);
  PreviewColumn->AddChildToVerticalBox(Text(TEXT("EMPTY"),26,Cyan))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
  PreviewColumn->AddChildToVerticalBox(Text(*FString::Printf(TEXT("展示%d"),I+1),22,FLinearColor::White));
  SlotsPanel->AddChildToHorizontalBox(PreviewSize)->SetPadding(FMargin(0,0,8,0));
 }
 Left->AddChildToVerticalBox(Text(TEXT("1  展示枠を選ぶ\n2  捕獲済みボアを選ぶ\n3  展示に設定"),23,FLinearColor(0.75f,0.88f,0.95f)))->SetPadding(FMargin(0,36,0,20));
 Left->AddChildToVerticalBox(Text(TEXT("変更はその場で展示へ反映・保存されます。\n別の枠に展示中のボアを選ぶと入れ替わります。"),19,Cyan));
 auto* Right=WidgetTree->ConstructWidget<UVerticalBox>();FSlateChildSize RightFill(ESlateSizeRule::Fill);RightFill.Value=0.56f;Main->AddChildToHorizontalBox(Right)->SetSize(RightFill);
 Right->AddChildToVerticalBox(Text(TEXT("CAPTURED BOARS  /  捕獲済み"),23,Cyan))->SetPadding(FMargin(0,0,0,14));
 GridScroll=WidgetTree->ConstructWidget<UScrollBox>();GridScroll->SetClipping(EWidgetClipping::ClipToBounds);Right->AddChildToVerticalBox(GridScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 Grid=WidgetTree->ConstructWidget<UUniformGridPanel>();Grid->SetSlotPadding(FMargin(8));GridScroll->AddChild(Grid);
 auto* Detail=WidgetTree->ConstructWidget<UBorder>();Detail->SetBrushColor(FLinearColor(0.014f,0.047f,0.085f));Detail->SetPadding(FMargin(16));Right->AddChildToVerticalBox(Detail)->SetPadding(FMargin(0,16,0,0));
 auto* DetailRow=WidgetTree->ConstructWidget<UHorizontalBox>();Detail->SetContent(DetailRow);
 auto* PhotoSize=WidgetTree->ConstructWidget<USizeBox>();PhotoSize->SetWidthOverride(136);PhotoSize->SetHeightOverride(136);DetailPhoto=WidgetTree->ConstructWidget<UImage>();DetailPhoto->SetVisibility(ESlateVisibility::Hidden);PhotoSize->SetContent(DetailPhoto);DetailRow->AddChildToHorizontalBox(PhotoSize)->SetPadding(FMargin(0,0,18,0));
 DetailText=Text(TEXT("ボアを選択してください"),20,FLinearColor::White);DetailRow->AddChildToHorizontalBox(DetailText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 Status=Text(TEXT("捕獲済みのボアがいません"),19,Cyan);Column->AddChildToVerticalBox(Status)->SetPadding(FMargin(0,18,0,14));
 auto* Footer=WidgetTree->ConstructWidget<UHorizontalBox>();Column->AddChildToVerticalBox(Footer);
 auto Button=[&](const TCHAR* Label,FLinearColor Color){auto* B=WidgetTree->ConstructWidget<UButton>();FButtonStyle Style=B->GetStyle();Style.Normal.TintColor=FSlateColor(FLinearColor(0.035f,0.11f,0.18f));Style.Hovered.TintColor=FSlateColor(FLinearColor(0.11f,0.21f,0.28f));Style.Pressed.TintColor=FSlateColor(FLinearColor(0.2f,0.25f,0.18f));B->SetStyle(Style);B->SetContent(Text(Label,21,Color));Footer->AddChildToHorizontalBox(B)->SetPadding(FMargin(0,0,20,0));return B;};
 SetButton=Button(TEXT("展示に設定  [E / East]"),Amber);SetButton->OnClicked.AddUniqueDynamic(this,&UBoarArchiveSelectionWidget::Confirm);
 Button(TEXT("外す  [Delete / West]"),Cyan)->OnClicked.AddUniqueDynamic(this,&UBoarArchiveSelectionWidget::RemoveBoar);
 BackButton=Button(TEXT("戻る  [Esc / South]"),FLinearColor::White);BackButton->OnClicked.AddUniqueDynamic(this,&UBoarArchiveSelectionWidget::Cancel);
 Column->AddChildToVerticalBox(Text(TEXT("枠切替  1 / 2 / 3・L1 / R1     一覧移動  矢印 / WASD・D-Pad / Stick"),17,Cyan))->SetPadding(FMargin(0,16,0,0));
 return Super::RebuildWidget();
}
// 保存済みの展示枠を表示用に取り込み、最近の捕獲順を優先して候補を並べます。
void UBoarArchiveSelectionWidget::NativeConstruct()
{
 Super::NativeConstruct();auto* GI=GetGameInstance<UBoarGameInstance>();
 PendingIds=GI?GI->GetArchiveDisplayIds():TArray<FName>();PendingIds.SetNum(3);ActiveSlot=0;FocusedCandidate=INDEX_NONE;
 Candidates.Reset();SlotRows.Reset();CandidateRows.Reset();SlotsPanel->ClearChildren();Grid->ClearChildren();
 if(const auto* Save=GI?GI->GetProgress():nullptr)
 {
  for(int32 I=Save->CapturedBoarHistory.Num()-1;I>=0;--I){const FName Id=Save->CapturedBoarHistory[I];if(!Id.IsNone()&&Save->CapturedBoarUniqueIds.Contains(Id))Candidates.AddUnique(Id);}
  // 古いセーブで捕獲順のないIDは名前順で補います。表示順のために履歴を書き換えません。
  auto Remaining=Save->CapturedBoarUniqueIds.Array();Remaining.Sort([](FName A,FName B){return A.LexicalLess(B);});
  for(FName Id:Remaining)if(!Id.IsNone())Candidates.AddUnique(Id);
 }
 const TSubclassOf<UBoarLoadoutEntry> CardClass=EntryClass?EntryClass:TSubclassOf<UBoarLoadoutEntry>(UBoarArchiveCard::StaticClass());
 for(int32 I=0;I<3;++I)
 {
  auto* C=CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(),CardClass);if(!C)continue;
  C->OnChosen.AddUniqueDynamic(this,&UBoarArchiveSelectionWidget::SelectSlot);SlotsPanel->AddChildToHorizontalBox(C)->SetPadding(FMargin(0,0,8,0));SlotRows.Add(C);
 }
 for(int32 I=0;I<Candidates.Num();++I)
 {
  auto* C=CreateWidget<UBoarLoadoutEntry>(GetOwningPlayer(),CardClass);if(!C)continue;
  C->Setup(I,LabelFor(Candidates[I]));C->OnChosen.AddUniqueDynamic(this,&UBoarArchiveSelectionWidget::ChooseBoar);C->OnFocused.AddUniqueDynamic(this,&UBoarArchiveSelectionWidget::ChooseBoar);
  Grid->AddChildToUniformGrid(C,I/3,I%3);CandidateRows.Add(C);
 }
 Status->SetText(FText::FromString(Candidates.IsEmpty()?TEXT("捕獲済みのボアがいません"):TEXT("ボアを選び、展示に設定してください。")));
 RefreshSlotRows();RefreshDetail();bInitialFocusPending=true;
}
FText UBoarArchiveSelectionWidget::LabelFor(FName Id) const
{
 if(Id.IsNone())return FText::FromString(TEXT("EMPTY"));
 const UStageConfig* S;const auto* D=Definition(StageCatalog,Id,S);return D&&!D->EncyclopediaName.IsEmpty()?D->EncyclopediaName:FText::FromName(Id);
}
void UBoarArchiveSelectionWidget::UpdatePortrait(UBoarLoadoutEntry* Entry,FName Id,const FText& Badge,bool bActive)
{
 if(auto* Card=Cast<UBoarArchiveCard>(Entry))
 {
  const UStageConfig* S;const auto* D=Definition(StageCatalog,Id,S);auto* Texture=D?D->EncyclopediaPhoto.LoadSynchronous():nullptr;
  Card->SetPortrait(Texture,FText::FromString(Id.IsNone()?TEXT("EMPTY"):TEXT("写真未登録")),Badge,bActive);
 }
}
void UBoarArchiveSelectionWidget::RefreshSlotRows()
{
 for(int32 I=0;I<SlotRows.Num();++I)
 {
  SlotRows[I]->Setup(I,LabelFor(PendingIds[I]),ActiveSlot==I);UpdatePortrait(SlotRows[I],PendingIds[I],FText::Format(FText::FromString(TEXT("展示{0}")),I+1),ActiveSlot==I);
 }
 for(int32 I=0;I<CandidateRows.Num();++I)
 {
  const int32 DisplayIndex=PendingIds.Find(Candidates[I]);FText Badge=DisplayIndex==INDEX_NONE?FText::GetEmpty():FText::Format(FText::FromString(TEXT("DISPLAY {0}")),DisplayIndex+1);
  UpdatePortrait(CandidateRows[I],Candidates[I],Badge,false);
 }
 SetButton->SetIsEnabled(Candidates.IsValidIndex(FocusedCandidate));
}
void UBoarArchiveSelectionWidget::RefreshDetail()
{
 const FName Id=Candidates.IsValidIndex(FocusedCandidate)?Candidates[FocusedCandidate]:NAME_None;const UStageConfig* S;const auto* D=Definition(StageCatalog,Id,S);
 FString Value=Id.IsNone()?TEXT("ボアを選択してください"):LabelFor(Id).ToString();
 if(D)
 {
  const FText Type=D->BoarClass?StaticEnum<EBoarArchetype>()->GetDisplayNameTextByValue(static_cast<int64>(D->BoarClass.GetDefaultObject()->GetBoarArchetype())):FText::FromString(TEXT("未登録"));
  Value+=FString::Printf(TEXT("\n種類：%s\n初捕獲Stage：%s"),*Type.ToString(),*(S->DisplayName.IsEmpty()?S->StageId.ToString():S->DisplayName.ToString()));
  if(!D->EncyclopediaDescription.IsEmpty())Value+=TEXT("\n")+D->EncyclopediaDescription.ToString().Left(140);
 }
 else if(!Id.IsNone())Value+=TEXT("\n個体情報未登録");
 auto* Texture=D?D->EncyclopediaPhoto.LoadSynchronous():nullptr;DetailPhoto->SetBrushFromTexture(Texture);DetailPhoto->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
 DetailText->SetText(FText::FromString(Value));SetButton->SetIsEnabled(!Id.IsNone());
}
void UBoarArchiveSelectionWidget::SelectSlot(int32 Index){if(PendingIds.IsValidIndex(Index)){ActiveSlot=Index;RefreshSlotRows();}}
void UBoarArchiveSelectionWidget::ChooseBoar(int32 Index){if(Candidates.IsValidIndex(Index)){FocusedCandidate=Index;RefreshDetail();}}
// 重複展示の調整と保存はGameInstanceへ依頼します。成功後に保存側の確定値を読み直します。
void UBoarArchiveSelectionWidget::AssignBoar(FName Id)
{
 if(!Id.IsNone() && PendingIds[ActiveSlot]==Id)
 {
  Status->SetColorAndOpacity(FSlateColor(Cyan));Status->SetText(FText::FromString(TEXT("この枠には選択中のボアが展示されています。")));return;
 }
 auto* GI=GetGameInstance<UBoarGameInstance>();
 if(!GI||!GI->AssignArchiveDisplay(ActiveSlot,Id)){Status->SetColorAndOpacity(FSlateColor(FLinearColor(1,0.24f,0.2f)));Status->SetText(FText::FromString(TEXT("保存できませんでした。展示は変更されていません。")));return;}
 PendingIds=GI->GetArchiveDisplayIds();RefreshSlotRows();Status->SetColorAndOpacity(FSlateColor(Cyan));Status->SetText(FText::FromString(TEXT("展示を更新・保存しました。")));
}
void UBoarArchiveSelectionWidget::Confirm(){if(Candidates.IsValidIndex(FocusedCandidate))AssignBoar(Candidates[FocusedCandidate]);}
void UBoarArchiveSelectionWidget::RemoveBoar(){AssignBoar(NAME_None);}
void UBoarArchiveSelectionWidget::Cancel(){OnClosed.Broadcast();}
void UBoarArchiveSelectionWidget::FocusInitialChoice(){if(!CandidateRows.IsEmpty())CandidateRows[0]->FocusEntry();else if(BackButton)BackButton->SetKeyboardFocus();}
void UBoarArchiveSelectionWidget::NativeTick(const FGeometry& G,float D){Super::NativeTick(G,D);if(bInitialFocusPending){bInitialFocusPending=false;FocusInitialChoice();}}
// 3列グリッドの候補へフォーカスを移し、選択中のカードが見えるようにスクロールします。
void UBoarArchiveSelectionWidget::MoveGridFocus(int32 Offset)
{
 if(CandidateRows.IsEmpty())return;int32 Index=FMath::Clamp((FocusedCandidate==INDEX_NONE?0:FocusedCandidate)+Offset,0,CandidateRows.Num()-1);CandidateRows[Index]->FocusEntry();GridScroll->ScrollWidgetIntoView(CandidateRows[Index],true,EDescendantScrollDestination::IntoView);
}
FReply UBoarArchiveSelectionWidget::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
 const FKey K=E.GetKey();
 if(K==EKeys::Escape||K==EKeys::Gamepad_FaceButton_Bottom){Cancel();return FReply::Handled();}
 if(K==EKeys::E||K==EKeys::Gamepad_FaceButton_Right){if(!E.IsRepeat())Confirm();return FReply::Handled();}
 if(K==EKeys::Delete||K==EKeys::Gamepad_FaceButton_Left){if(!E.IsRepeat())RemoveBoar();return FReply::Handled();}
 if(K==EKeys::One||K==EKeys::Two||K==EKeys::Three){SelectSlot(K==EKeys::One?0:K==EKeys::Two?1:2);return FReply::Handled();}
 if(K==EKeys::Gamepad_LeftShoulder||K==EKeys::Gamepad_RightShoulder){SelectSlot((ActiveSlot+(K==EKeys::Gamepad_RightShoulder?1:2))%3);return FReply::Handled();}
 int32 Move=0;
 if(K==EKeys::Right||K==EKeys::D||K==EKeys::Gamepad_DPad_Right)Move=1;
 if(K==EKeys::Left||K==EKeys::A||K==EKeys::Gamepad_DPad_Left)Move=-1;
 if(K==EKeys::Down||K==EKeys::S||K==EKeys::Gamepad_DPad_Down)Move=3;
 if(K==EKeys::Up||K==EKeys::W||K==EKeys::Gamepad_DPad_Up)Move=-3;
 if(Move){MoveGridFocus(Move);return FReply::Handled();}
 return Super::NativeOnPreviewKeyDown(G,E);
}
// スティックの閾値と実時間の待ち間隔で移動を制限し、フレームごとの過剰なフォーカス移動を抑えます。
FReply UBoarArchiveSelectionWidget::NativeOnAnalogValueChanged(const FGeometry& G,const FAnalogInputEvent& E)
{
 if(E.GetKey()==EKeys::Gamepad_LeftX||E.GetKey()==EKeys::Gamepad_LeftY)
 {
  if(FMath::Abs(E.GetAnalogValue())>0.6f&&FPlatformTime::Seconds()>=NextAnalogNavigation){const int32 Sign=E.GetAnalogValue()>0?1:-1;MoveGridFocus(E.GetKey()==EKeys::Gamepad_LeftX?Sign:-Sign*3);NextAnalogNavigation=FPlatformTime::Seconds()+0.18;}
  return FReply::Handled();
 }
 return Super::NativeOnAnalogValueChanged(G,E);
}
FReply UBoarArchiveSelectionWidget::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E){return Super::NativeOnKeyDown(G,E);}
