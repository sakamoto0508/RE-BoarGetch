#include "UI/BoarUIAuthoringLibrary.h"

#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_CallFunction.h"
#include "MovieScene.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "HighResScreenshot.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Stage/SpecialCoin.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Misc/FileHelper.h"
#include "UObject/Package.h"

namespace
{
	void DescribeWidget(UWidget* Widget, int32 Depth, FString& Out)
	{
		if (!Widget) return;
		Out += FString::ChrN(Depth * 2, TEXT(' ')) + FString::Printf(TEXT("%s [%s] var=%d vis=%d"), *Widget->GetName(),
		                                                             *Widget->GetClass()->GetName(),
		                                                             Widget->bIsVariable ? 1 : 0,
		                                                             int32(Widget->GetVisibility()));
		if (const UTextBlock* Text = Cast<UTextBlock>(Widget))
			Out += FString::Printf(
				TEXT(" text=\"%s\" size=%.0f font=%s color=%s"), *Text->GetText().ToString(), Text->GetFont().Size,
				*GetNameSafe(Text->GetFont().FontObject), *Text->GetColorAndOpacity().GetSpecifiedColor().ToString());
		if (Widget->Slot) Out += FString::Printf(TEXT(" slot=%s"), *Widget->Slot->GetClass()->GetName());
		if (const UCanvasPanelSlot* Canvas = Cast<UCanvasPanelSlot>(Widget->Slot))
			Out += FString::Printf(
				TEXT(" pos=%s size=%s auto=%d anchors=%s align=%s"), *Canvas->GetPosition().ToString(),
				*Canvas->GetSize().ToString(),
				Canvas->GetAutoSize() ? 1 : 0, *Canvas->GetAnchors().Minimum.ToString(),
				*Canvas->GetAlignment().ToString());
		Out += TEXT("\n");
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
			for (int32 I = 0; I < Panel->GetChildrenCount(); ++I) DescribeWidget(Panel->GetChildAt(I), Depth + 1, Out);
	}

	FFrameNumber ToFrame(const UMovieScene* Scene, float Seconds)
	{
		return (Seconds * Scene->GetTickResolution()).RoundToFrame();
	}
}

FString UBoarUIAuthoringLibrary::DescribeWidgetBlueprint(UObject* Object)
{
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Object);
	if (!WBP || !WBP->WidgetTree) return TEXT("<not a widget blueprint>");
	FString Out = FString::Printf(TEXT("Parent=%s\n"), *GetNameSafe(WBP->ParentClass));
	DescribeWidget(WBP->WidgetTree->RootWidget, 0, Out);
	for (const UWidgetAnimation* Animation : WBP->Animations)
	{
		Out += FString::Printf(TEXT("Animation %s end=%.2f bindings="), *GetNameSafe(Animation),
		                       Animation ? Animation->GetEndTime() : 0.f);
		if (Animation) for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings) Out += Binding.
			WidgetName.ToString() + TEXT(" ");
		Out += TEXT("\n");
	}
	return Out;
}

bool UBoarUIAuthoringLibrary::EnsureTextBlock(UObject* Object, FName WidgetName, FName ParentPanelName,
                                              int32 InsertIndex,
                                              FName StyleSourceName, const FText& Text, int32 FontSize,
                                              FLinearColor Color)
{
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Object);
	if (!WBP || !WBP->WidgetTree || WidgetName.IsNone()) return false;
	UTextBlock* TextBlock = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(WidgetName));
	if (!TextBlock)
	{
		UPanelWidget* Parent = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(ParentPanelName));
		if (!Parent) return false;
		WBP->Modify();
		TextBlock = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), WidgetName);
		if (InsertIndex >= 0 && InsertIndex <= Parent->GetChildrenCount()) Parent->
			InsertChildAt(InsertIndex, TextBlock);
		else Parent->AddChild(TextBlock);
		// 変数GUIDを登録する（未登録だとWidget Blueprintのコンパイルでensureが出る）。
		WBP->OnVariableAdded(WidgetName);
	}
	TextBlock->Modify();
	TextBlock->bIsVariable = true;
	if (const UTextBlock* Style = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(StyleSourceName)))
	{
		TextBlock->SetFont(Style->GetFont());
		TextBlock->SetShadowOffset(Style->GetShadowOffset());
		TextBlock->SetShadowColorAndOpacity(Style->GetShadowColorAndOpacity());
	}
	FSlateFontInfo Font = TextBlock->GetFont();
	if (FontSize > 0) Font.Size = FontSize;
	TextBlock->SetFont(Font);
	TextBlock->SetText(Text);
	TextBlock->SetColorAndOpacity(FSlateColor(Color));
	TextBlock->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(TextBlock->Slot)) Slot->SetHorizontalAlignment(HAlign_Center);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	return true;
}

UWidget* UBoarUIAuthoringLibrary::EnsureWidget(UObject* Object, TSubclassOf<UWidget> WidgetClass, FName WidgetName,
                                               FName ParentPanelName, bool bIsVariable)
{
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Object);
	if (!WBP || !WBP->WidgetTree || !WidgetClass || WidgetName.IsNone()) return nullptr;
	UWidget* Widget = WBP->WidgetTree->FindWidget(WidgetName);
	if (Widget && !Widget->IsA(WidgetClass)) return nullptr;
	if (!Widget)
	{
		UPanelWidget* Parent = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(ParentPanelName));
		if (!Parent && (!ParentPanelName.IsNone() || WBP->WidgetTree->RootWidget)) return nullptr;
		WBP->Modify();
		Widget = WBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, WidgetName);
		if (Parent) Parent->AddChild(Widget);
		else WBP->WidgetTree->RootWidget = Widget;
		WBP->OnVariableAdded(WidgetName);
	}
	Widget->Modify();
	Widget->bIsVariable = bIsVariable;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	return Widget;
}

bool UBoarUIAuthoringLibrary::SimulateLeftClickOnWidget(UWidget* Widget)
{
	if (!Widget || !FSlateApplication::IsInitialized()) return false;
	const FGeometry& G = Widget->GetCachedGeometry();
	const FVector2D Pos = G.LocalToAbsolute(G.GetLocalSize() * .5f);
	FSlateApplication& App = FSlateApplication::Get();
	const TSet<FKey> Pressed{EKeys::LeftMouseButton};
	const FPointerEvent Down(App.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, Pos, Pos, Pressed,
	                         EKeys::LeftMouseButton, 0.f, FModifierKeysState());
	const bool bHandled = App.ProcessMouseButtonDownEvent(nullptr, Down);
	const FPointerEvent Up(App.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, Pos, Pos, TSet<FKey>(),
	                       EKeys::LeftMouseButton, 0.f, FModifierKeysState());
	App.ProcessMouseButtonUpEvent(Up);
	return bHandled;
}

UFontFace* UBoarUIAuthoringLibrary::EnsureFontFaceFromFile(const FString& PackagePath, const FString& FontFilePath)
{
	TArray<uint8> Data;
	if (PackagePath.IsEmpty() || !FFileHelper::LoadFileToArray(Data, *FontFilePath) || Data.IsEmpty()) return nullptr;
	UPackage* Package = CreatePackage(*PackagePath);
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
	UFontFace* Face = FindObject<UFontFace>(Package, *AssetName);
	if (!Face) Face = NewObject<UFontFace>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	Face->Modify();
	Face->InitializeFromBulkData(FontFilePath, EFontHinting::Default, Data.GetData(), Data.Num());
	Face->MarkPackageDirty();
	return Face;
}

UFont* UBoarUIAuthoringLibrary::EnsureCompositeFont(const FString& PackagePath, const TArray<FName>& TypefaceNames,
                                                   const TArray<UFontFace*>& Faces)
{
	if (PackagePath.IsEmpty() || TypefaceNames.Num() != Faces.Num() || Faces.IsEmpty()) return nullptr;
	UPackage* Package = CreatePackage(*PackagePath);
	const FName AssetName(*FPackageName::GetLongPackageAssetName(PackagePath));
	UFont* Font = FindObject<UFont>(Package, *AssetName.ToString());
	if (!Font) Font = NewObject<UFont>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
	Font->Modify();
	Font->FontCacheType = EFontCacheType::Runtime;
	FCompositeFont& Composite = Font->GetMutableInternalCompositeFont();
	Composite.DefaultTypeface.Fonts.Reset();
	for (int32 I = 0; I < Faces.Num(); ++I)
	{
		if (!Faces[I]) continue;
		FTypefaceEntry& Entry = Composite.DefaultTypeface.Fonts.AddDefaulted_GetRef();
		Entry.Name = TypefaceNames[I];
		Entry.Font = FFontData(Faces[I]);
	}
	Font->PostEditChange();
	Font->MarkPackageDirty();
	return Font;
}

bool UBoarUIAuthoringLibrary::SimulateMouseMoveOverWidget(UWidget* Widget)
{
	if (!Widget || !FSlateApplication::IsInitialized()) return false;
	const FGeometry& G = Widget->GetCachedGeometry();
	const FVector2D From = G.LocalToAbsolute(G.GetLocalSize() * .5f);
	const FVector2D To = From + FVector2D(24.f, 12.f);
	FSlateApplication& App = FSlateApplication::Get();
	const FPointerEvent Move(App.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, To, From, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState());
	return App.ProcessMouseMoveEvent(Move);
}

bool UBoarUIAuthoringLibrary::SimulateKeyPress(FKey Key)
{
	if (!Key.IsValid() || !FSlateApplication::IsInitialized()) return false;
	FSlateApplication& App = FSlateApplication::Get();
	const FKeyEvent Event(Key, FModifierKeysState(), App.GetUserIndexForKeyboard(), false, 0, 0);
	const bool bHandled = App.ProcessKeyDownEvent(Event);
	App.ProcessKeyUpEvent(Event);
	return bHandled;
}

bool UBoarUIAuthoringLibrary::RemoveWidget(UObject* Object, FName WidgetName)
{
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Object);
	UWidget* Widget = WBP && WBP->WidgetTree ? WBP->WidgetTree->FindWidget(WidgetName) : nullptr;
	if (!Widget || Widget == WBP->WidgetTree->RootWidget) return false;
	WBP->Modify();
	// 子孫も変数としてBPに登録されているので、まとめて名前を解放します。
	TArray<UWidget*> Removed{Widget};
	UWidgetTree::GetChildWidgets(Widget, Removed);
	WBP->WidgetTree->RemoveWidget(Widget);
	for (UWidget* Child : Removed)
	{
		WBP->OnVariableRemoved(Child->GetFName());
		Child->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	return true;
}

FString UBoarUIAuthoringLibrary::DescribeBlueprintGraphs(UObject* Object)
{
	UBlueprint* BP = Cast<UBlueprint>(Object);
	if (!BP) return TEXT("not a blueprint");
	FString Out;
	TArray<UEdGraph*> Graphs = BP->UbergraphPages;
	Graphs.Append(BP->FunctionGraphs);
	for (UEdGraph* Graph : Graphs)
		if (Graph)
			for (UEdGraphNode* Node : Graph->Nodes)
				if (Node) Out += FString::Printf(TEXT("%s: %s\n"), *Graph->GetName(), *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
	return Out;
}

int32 UBoarUIAuthoringLibrary::RetargetFunctionCalls(UObject* Object, FName OldFunctionName, UClass* NewOwnerClass, FName NewFunctionName)
{
	UBlueprint* BP = Cast<UBlueprint>(Object);
	UFunction* NewFunction = NewOwnerClass ? NewOwnerClass->FindFunctionByName(NewFunctionName) : nullptr;
	if (!BP || !NewFunction)
		return 0;
	TArray<UEdGraph*> Graphs = BP->UbergraphPages;
	Graphs.Append(BP->FunctionGraphs);
	int32 Count = 0;
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			if (!Call || Call->FunctionReference.GetMemberName() != OldFunctionName)
				continue;
			Call->Modify();
			Call->SetFromFunction(NewFunction);
			// ReconstructNodeは同じ名前のピンの接続と既定値を新しいピンへ移します。
			Call->ReconstructNode();
			++Count;
		}
	}
	if (Count > 0)
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	return Count;
}

FString UBoarUIAuthoringLibrary::ClearBlueprintGraphs(UObject* Object)
{
	UBlueprint* BP = Cast<UBlueprint>(Object);
	if (!BP) return TEXT("not a blueprint");
	FString Out;
	BP->Modify();
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : TArray<UEdGraphNode*>(Graph->Nodes))
		{
			if (!Node) continue;
			Out += FString::Printf(TEXT("%s: %s\n"), *Graph->GetName(),
			                       *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
			FBlueprintEditorUtils::RemoveNode(BP, Node, true);
		}
	}
	for (UEdGraph* Graph : TArray<UEdGraph*>(BP->FunctionGraphs))
	{
		if (!Graph) continue;
		Out += FString::Printf(TEXT("function %s\n"), *Graph->GetName());
		FBlueprintEditorUtils::RemoveGraph(BP, Graph, EGraphRemoveFlags::Recompile);
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	return Out;
}

bool UBoarUIAuthoringLibrary::ConfigureTextBlockStyle(UObject* Object, FName WidgetName, const FText& Text,
                                                      int32 FontSize, FName Typeface,
                                                      FLinearColor Color, int32 OutlineSize, FLinearColor OutlineColor,
                                                      FVector2D ShadowOffset, FLinearColor ShadowColor)
{
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Object);
	UTextBlock* TextBlock = WBP && WBP->WidgetTree
		                        ? Cast<UTextBlock>(WBP->WidgetTree->FindWidget(WidgetName))
		                        : nullptr;
	if (!TextBlock) return false;
	WBP->Modify();
	TextBlock->Modify();
	FSlateFontInfo Font = TextBlock->GetFont();
	if (FontSize > 0) Font.Size = FontSize;
	if (!Typeface.IsNone()) Font.TypefaceFontName = Typeface;
	Font.OutlineSettings.OutlineSize = FMath::Max(0, OutlineSize);
	Font.OutlineSettings.OutlineColor = OutlineColor;
	TextBlock->SetFont(Font);
	if (!Text.IsEmpty()) TextBlock->SetText(Text);
	TextBlock->SetColorAndOpacity(FSlateColor(Color));
	TextBlock->SetShadowOffset(ShadowOffset);
	TextBlock->SetShadowColorAndOpacity(ShadowColor);
	TextBlock->SetJustification(ETextJustify::Center);
	FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
	return true;
}

bool UBoarUIAuthoringLibrary::SetCanvasSlotLayout(UObject* Object, FName WidgetName, FVector2D Position, FVector2D Size,
                                                  bool bAutoSize)
{
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Object);
	UWidget* Widget = WBP && WBP->WidgetTree ? WBP->WidgetTree->FindWidget(WidgetName) : nullptr;
	UCanvasPanelSlot* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
	if (!Slot) return false;
	WBP->Modify();
	Slot->Modify();
	Slot->SetPosition(Position);
	Slot->SetSize(Size);
	Slot->SetAutoSize(bAutoSize);
	FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
	return true;
}

bool UBoarUIAuthoringLibrary::EnsureBlinkAnimation(UObject* Object, FName AnimationName, FName WidgetName,
                                                   float Period, float OnDuration, float OffOpacity)
{
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Object);
	UWidget* Target = WBP && WBP->WidgetTree ? WBP->WidgetTree->FindWidget(WidgetName) : nullptr;
	if (!Target || AnimationName.IsNone() || Period <= 0.f) return false;
	WBP->Modify();
	// 作り直しでは変数名が登録済みなので、新規のときだけOnVariableAddedを呼びます（二重登録のensure回避）。
	bool bReplaced = false;
	for (int32 I = WBP->Animations.Num() - 1; I >= 0; --I)
		if (WBP->Animations[I] && WBP->Animations[I]->GetFName() == AnimationName)
		{
			bReplaced = true;
			UWidgetAnimation* Old = WBP->Animations[I];
			WBP->Animations.RemoveAt(I);
			Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		}
	UWidgetAnimation* Animation = NewObject<UWidgetAnimation>(WBP, AnimationName, RF_Transactional);
	Animation->SetDisplayLabel(AnimationName.ToString());
	UMovieScene* Scene = NewObject<UMovieScene>(Animation, AnimationName, RF_Transactional);
	Animation->MovieScene = Scene;
	Scene->SetDisplayRate(FFrameRate(30, 1));
	const FFrameNumber End = ToFrame(Scene, Period);
	Scene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), End));
	Scene->GetEditorData().WorkStart = 0.0;
	Scene->GetEditorData().WorkEnd = Period;

	const FGuid Guid = Scene->AddPossessable(WidgetName.ToString(), Target->GetClass());
	FWidgetAnimationBinding Binding;
	Binding.WidgetName = WidgetName;
	Binding.AnimationGuid = Guid;
	Binding.bIsRootWidget = false;
	Animation->AnimationBindings.Add(Binding);

	UMovieSceneFloatTrack* Opacity = Scene->AddTrack<UMovieSceneFloatTrack>(Guid);
	Opacity->SetPropertyNameAndPath(TEXT("RenderOpacity"), TEXT("RenderOpacity"));
	auto* Section = CastChecked<UMovieSceneFloatSection>(Opacity->CreateNewSection());
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), End));
	Opacity->AddSection(*Section);
	FMovieSceneFloatChannel& Alpha = Section->GetChannel();
	Alpha.AddConstantKey(FFrameNumber(0), 1.f);
	Alpha.AddConstantKey(ToFrame(Scene, FMath::Clamp(OnDuration, 0.f, Period)), OffOpacity);

	WBP->Animations.Add(Animation);
	if (!bReplaced) WBP->OnVariableAdded(AnimationName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	return true;
}

bool UBoarUIAuthoringLibrary::EnsureRevealAnimation(UObject* Object, FName AnimationName, FName WidgetName,
                                                    float Duration, float StartScale, float StartOffsetY)
{
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Object);
	if (!WBP || !WBP->WidgetTree || !WBP->WidgetTree->FindWidget(WidgetName) || AnimationName.IsNone() || Duration <=
		0.f) return false;
	WBP->Modify();
	// 同名アニメは作り直し、再実行しても同じ結果になるようにします。
	// 作り直しでは変数名が登録済みなので、新規のときだけOnVariableAddedを呼びます（二重登録のensure回避）。
	bool bReplaced = false;
	for (int32 I = WBP->Animations.Num() - 1; I >= 0; --I)
		if (WBP->Animations[I] && WBP->Animations[I]->GetFName() == AnimationName)
		{
			bReplaced = true;
			UWidgetAnimation* Old = WBP->Animations[I];
			WBP->Animations.RemoveAt(I);
			Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		}
	UWidgetAnimation* Animation = NewObject<UWidgetAnimation>(WBP, AnimationName, RF_Transactional);
	Animation->SetDisplayLabel(AnimationName.ToString());
	UMovieScene* Scene = NewObject<UMovieScene>(Animation, AnimationName, RF_Transactional);
	Animation->MovieScene = Scene;
	Scene->SetDisplayRate(FFrameRate(30, 1));
	const FFrameNumber End = ToFrame(Scene, Duration);
	Scene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), End + 1));
	Scene->GetEditorData().WorkStart = 0.0;
	Scene->GetEditorData().WorkEnd = Duration;

	const FGuid Guid = Scene->AddPossessable(WidgetName.ToString(), UTextBlock::StaticClass());
	FWidgetAnimationBinding Binding;
	Binding.WidgetName = WidgetName;
	Binding.AnimationGuid = Guid;
	Binding.bIsRootWidget = false;
	Animation->AnimationBindings.Add(Binding);

	UMovieSceneFloatTrack* Opacity = Scene->AddTrack<UMovieSceneFloatTrack>(Guid);
	Opacity->SetPropertyNameAndPath(TEXT("RenderOpacity"), TEXT("RenderOpacity"));
	auto* OpacitySection = CastChecked<UMovieSceneFloatSection>(Opacity->CreateNewSection());
	OpacitySection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), End + 1));
	Opacity->AddSection(*OpacitySection);
	FMovieSceneFloatChannel& Alpha = OpacitySection->GetChannel();
	Alpha.AddCubicKey(FFrameNumber(0), 0.f);
	Alpha.AddCubicKey(ToFrame(Scene, Duration * .55f), 1.f);
	Alpha.AddCubicKey(End, 1.f);

	UMovieScene2DTransformTrack* Transform = Scene->AddTrack<UMovieScene2DTransformTrack>(Guid);
	Transform->SetPropertyNameAndPath(TEXT("RenderTransform"), TEXT("RenderTransform"));
	auto* TransformSection = CastChecked<UMovieScene2DTransformSection>(Transform->CreateNewSection());
	TransformSection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), End + 1));
	Transform->AddSection(*TransformSection);
	// 少し大きく出てから弾むように等倍へ戻し、下から定位置へ滑り込みます。
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		FMovieSceneFloatChannel& Scale = TransformSection->Scale[Axis];
		Scale.AddCubicKey(FFrameNumber(0), StartScale);
		Scale.AddCubicKey(ToFrame(Scene, Duration * .6f), 1.f + (StartScale - 1.f) * -.12f);
		Scale.AddCubicKey(End, 1.f);
	}
	FMovieSceneFloatChannel& OffsetY = TransformSection->Translation[1];
	OffsetY.AddCubicKey(FFrameNumber(0), StartOffsetY);
	OffsetY.AddCubicKey(ToFrame(Scene, Duration * .6f), 0.f);
	OffsetY.AddCubicKey(End, 0.f);

	WBP->Animations.Add(Animation);
	if (!bReplaced) WBP->OnVariableAdded(AnimationName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	return true;
}

bool UBoarUIAuthoringLibrary::RequestVerificationScreenshot(const FString& AbsolutePngPath, bool bShowUI)
{
	if (FScreenshotRequest::IsScreenshotRequested()) return false;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsolutePngPath), true);
	FScreenshotRequest::RequestScreenshot(AbsolutePngPath, bShowUI, false);
	return true;
}

bool UBoarUIAuthoringLibrary::IsVerificationScreenshotPending()
{
	return FScreenshotRequest::IsScreenshotRequested();
}

namespace
{
	bool bPIEWindowSaved = false;
	TEnumAsByte<EPlayModeType> SavedPlayMode;
	int32 SavedWindowWidth = 0, SavedWindowHeight = 0;
}

void UBoarUIAuthoringLibrary::UsePIENewWindow(int32 Width, int32 Height)
{
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	if (!bPIEWindowSaved)
	{
		SavedPlayMode = Settings->LastExecutedPlayModeType;
		SavedWindowWidth = Settings->NewWindowWidth;
		SavedWindowHeight = Settings->NewWindowHeight;
		bPIEWindowSaved = true;
	}
	Settings->LastExecutedPlayModeType = PlayMode_InEditorFloating;
	Settings->NewWindowWidth = Width;
	Settings->NewWindowHeight = Height;
}

void UBoarUIAuthoringLibrary::RestorePIEWindow()
{
	if (!bPIEWindowSaved) return;
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->LastExecutedPlayModeType = SavedPlayMode;
	Settings->NewWindowWidth = SavedWindowWidth;
	Settings->NewWindowHeight = SavedWindowHeight;
	bPIEWindowSaved = false;
}

void UBoarUIAuthoringLibrary::RequestPIEInNewWindow(int32 Width, int32 Height)
{
	if (!GEditor) return;
	ULevelEditorPlaySettings* Settings = DuplicateObject<ULevelEditorPlaySettings>(
		GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
	Settings->LastExecutedPlayModeType = PlayMode_InEditorFloating;
	Settings->NewWindowWidth = Width;
	Settings->NewWindowHeight = Height;
	FRequestPlaySessionParams Params;
	Params.WorldType = EPlaySessionWorldType::PlayInEditor;
	Params.EditorPlaySettings = Settings; // DestinationSlateViewport unset -> PIE opens its own window
	GEditor->RequestPlaySession(Params);
}

ASpecialCoin* UBoarUIAuthoringLibrary::SpawnVerificationCoin(UObject* WorldContextObject, FName CoinId,
                                                             FVector Location)
{
	UWorld* World = GEngine
		                ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		                : nullptr;
	if (!World || CoinId.IsNone()) return nullptr;
	const FTransform Transform(Location);
	ASpecialCoin* Coin = World->SpawnActorDeferred<ASpecialCoin>(ASpecialCoin::StaticClass(), Transform);
	if (!Coin) return nullptr;
	Coin->SpecialCoinId = CoinId;
	Coin->FinishSpawning(Transform);
	return Coin;
}
#endif
