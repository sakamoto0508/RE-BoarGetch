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
#include "Kismet2/BlueprintEditorUtils.h"
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

namespace
{
	void DescribeWidget(UWidget* Widget, int32 Depth, FString& Out)
	{
		if (!Widget) return;
		Out += FString::ChrN(Depth * 2, TEXT(' ')) + FString::Printf(TEXT("%s [%s] var=%d vis=%d"), *Widget->GetName(),
			*Widget->GetClass()->GetName(), Widget->bIsVariable ? 1 : 0, int32(Widget->GetVisibility()));
		if (const UTextBlock* Text = Cast<UTextBlock>(Widget))
			Out += FString::Printf(TEXT(" text=\"%s\" size=%.0f font=%s color=%s"), *Text->GetText().ToString(), Text->GetFont().Size,
				*GetNameSafe(Text->GetFont().FontObject), *Text->GetColorAndOpacity().GetSpecifiedColor().ToString());
		if (Widget->Slot) Out += FString::Printf(TEXT(" slot=%s"), *Widget->Slot->GetClass()->GetName());
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
		Out += FString::Printf(TEXT("Animation %s end=%.2f bindings="), *GetNameSafe(Animation), Animation ? Animation->GetEndTime() : 0.f);
		if (Animation) for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings) Out += Binding.WidgetName.ToString() + TEXT(" ");
		Out += TEXT("\n");
	}
	return Out;
}

bool UBoarUIAuthoringLibrary::EnsureTextBlock(UObject* Object, FName WidgetName, FName ParentPanelName, int32 InsertIndex,
	FName StyleSourceName, const FText& Text, int32 FontSize, FLinearColor Color)
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
		if (InsertIndex >= 0 && InsertIndex <= Parent->GetChildrenCount()) Parent->InsertChildAt(InsertIndex, TextBlock);
		else Parent->AddChild(TextBlock);
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

bool UBoarUIAuthoringLibrary::EnsureRevealAnimation(UObject* Object, FName AnimationName, FName WidgetName,
	float Duration, float StartScale, float StartOffsetY)
{
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Object);
	if (!WBP || !WBP->WidgetTree || !WBP->WidgetTree->FindWidget(WidgetName) || AnimationName.IsNone() || Duration <= 0.f) return false;
	WBP->Modify();
	// 同名アニメは作り直し、再実行しても同じ結果になるようにします。
	for (int32 I = WBP->Animations.Num() - 1; I >= 0; --I)
		if (WBP->Animations[I] && WBP->Animations[I]->GetFName() == AnimationName)
		{
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
	ULevelEditorPlaySettings* Settings = DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
	Settings->LastExecutedPlayModeType = PlayMode_InEditorFloating;
	Settings->NewWindowWidth = Width;
	Settings->NewWindowHeight = Height;
	FRequestPlaySessionParams Params;
	Params.WorldType = EPlaySessionWorldType::PlayInEditor;
	Params.EditorPlaySettings = Settings;  // DestinationSlateViewport unset -> PIE opens its own window
	GEditor->RequestPlaySession(Params);
}

ASpecialCoin* UBoarUIAuthoringLibrary::SpawnVerificationCoin(UObject* WorldContextObject, FName CoinId, FVector Location)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || CoinId.IsNone()) return nullptr;
	const FTransform Transform(Location);
	ASpecialCoin* Coin = World->SpawnActorDeferred<ASpecialCoin>(ASpecialCoin::StaticClass(), Transform);
	if (!Coin) return nullptr;
	Coin->SpecialCoinId = CoinId;
	Coin->FinishSpawning(Transform);
	return Coin;
}
#endif
