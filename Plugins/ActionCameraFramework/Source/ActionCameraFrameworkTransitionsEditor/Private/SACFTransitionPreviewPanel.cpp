#include "SACFTransitionPreviewPanel.h"

#include "ACFTransitionPreset.h"
#include "ACFTransitionSubsystem.h"
#include "ACFTransitionView.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Factories/DataAssetFactory.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "ACFTransitionPreview"

void SACFTransitionPreviewPanel::Construct(const FArguments&)
{
	Settings.Reset(NewObject<UACFTransitionPreviewSettings>());
	Picture.Reset(FACFTransitionView::CreatePreviewPicture(640, 360));
	PictureBrush.SetResourceObject(Picture.Get());
	PictureBrush.ImageSize = FVector2D(640.0, 360.0);
	View = MakeShared<FACFTransitionView>();
	View->SetFallbackSize(FVector2D(640.0, 360.0));

	for (const FACFTransitionEffectInfo& Info : ACFTransition::GetAllEffects())
	{
		Items.Add(MakeShared<FACFTransitionEffectInfo>(Info));
	}

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.bAllowSearch = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Details = PropertyEditor.CreateDetailView(Args);
	Details->SetObject(Settings.Get());
	Details->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&) { ApplyFrame(); });

	auto Button = [](const FText& Label, const FText& Tip, TFunction<FReply()> OnClick)
	{
		return SNew(SButton).Text(Label).ToolTipText(Tip).OnClicked_Lambda(MoveTemp(OnClick));
	};

	ChildSlot
	[
		SNew(SSplitter)
		+ SSplitter::Slot().Value(0.22f)
		[
			SAssignNew(List, SListView<TSharedPtr<FACFTransitionEffectInfo>>)
			.ListItemsSource(&Items)
			.OnGenerateRow(this, &SACFTransitionPreviewPanel::MakeRow)
			.OnSelectionChanged(this, &SACFTransitionPreviewPanel::OnListSelection)
			.SelectionMode(ESelectionMode::Single)
		]
		+ SSplitter::Slot().Value(0.5f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(4.f)
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
				[
					SNew(SBox).WidthOverride(640.f).HeightOverride(360.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[SNew(SImage).Image(&PictureBrush)]
						+ SOverlay::Slot()[View->GetWidget()]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				SNew(STextBlock).Text(this, &SACFTransitionPreviewPanel::GetStatusText).AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.0, 4.0))
				+ SWrapBox::Slot()[Button(LOCTEXT("Play", "Play (Close + Open)"), LOCTEXT("PlayTip", "Close, hold, then Open."), [this]() { Play(true, true); return FReply::Handled(); })]
				+ SWrapBox::Slot()[Button(LOCTEXT("Close", "Close"), LOCTEXT("CloseTip", "Only the Close (stays covered)."), [this]() { Play(true, false); return FReply::Handled(); })]
				+ SWrapBox::Slot()[Button(LOCTEXT("Open", "Open"), LOCTEXT("OpenTip", "Only the Open (from covered)."), [this]() { Play(false, true); return FReply::Handled(); })]
				+ SWrapBox::Slot()[Button(LOCTEXT("All", "Preview All"), LOCTEXT("AllTip", "Plays every effect in ID order."), [this]() { PlayAll(); return FReply::Handled(); })]
				+ SWrapBox::Slot()[Button(LOCTEXT("Stop", "Stop"), LOCTEXT("StopTip", "Stop and clear the preview in this tab."), [this]() { Stop(); return FReply::Handled(); })]
				+ SWrapBox::Slot()[Button(LOCTEXT("Pie", "Play in PIE"), LOCTEXT("PieTip", "Play Close, Hold and Open in the running PIE viewport. Wait for any active transition to finish first."), [this]() { PlayInPie(); return FReply::Handled(); })]
				+ SWrapBox::Slot()[Button(LOCTEXT("Save", "Save as Preset..."), LOCTEXT("SaveTip", "Creates a Transition Preset asset with these Close / Open settings."), [this]() { return SaveAsPreset(); })]
				+ SWrapBox::Slot()[Button(LOCTEXT("Reset", "Reset to Defaults"), LOCTEXT("ResetTip", "The catalog defaults of the selected effect."), [this]() { SelectEffect(Selected); return FReply::Handled(); })]
			]
		]
		+ SSplitter::Slot().Value(0.28f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				SNew(STextBlock).Text(LOCTEXT("PreviewPreset", "Preview Preset"))
				.ToolTipText(LOCTEXT("PreviewPresetTip", "Copy Close and Open from a preset into this preview. The preset asset is unchanged when you adjust the preview."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 4.f, 4.f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UACFTransitionPreset::StaticClass())
				.ObjectPath_Lambda([this]() { return PreviewPresetPath; })
				.OnObjectChanged(this, &SACFTransitionPreviewPanel::OnPresetSelected)
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				Details.ToSharedRef()
			]
		]
	];
	SelectEffect(EACFTransitionEffect::BlackFade);
}

SACFTransitionPreviewPanel::~SACFTransitionPreviewPanel()
{
	if (View.IsValid())
	{
		View->Release();
	}
}

TSharedRef<ITableRow> SACFTransitionPreviewPanel::MakeRow(TSharedPtr<FACFTransitionEffectInfo> Item, const TSharedRef<STableViewBase>& Owner)
{
	const FText Label = FText::FromString(FString::Printf(TEXT("%02d  %s%s"), Item->Id, *Item->Name, Item->bUsesSnapshot ? TEXT("  [capture]") : TEXT("")));
	return SNew(STableRow<TSharedPtr<FACFTransitionEffectInfo>>, Owner)
		.ToolTipText(FText::FromString(Item->Description))
		[
			SNew(STextBlock).Text(Label)
		];
}

void SACFTransitionPreviewPanel::OnListSelection(TSharedPtr<FACFTransitionEffectInfo> Item, ESelectInfo::Type Type)
{
	if (Item.IsValid() && Type != ESelectInfo::Direct)
	{
		SelectEffect(Item->Effect);
		Play(true, true);
	}
}

void SACFTransitionPreviewPanel::SelectEffect(EACFTransitionEffect Effect)
{
	PreviewPresetPath.Reset();
	Selected = Effect;
	Settings->Close = ACFTransition::MakeDefaultSettings(Effect, EACFTransitionPhase::Close);
	Settings->Open = ACFTransition::MakeDefaultSettings(Effect, EACFTransitionPhase::Open);
	SyncEffectSelection();
	Mode = EMode::Idle;
	Coverage = 0.f;
	ApplyFrame();
}

void SACFTransitionPreviewPanel::OnPresetSelected(const FAssetData& Asset)
{
	if (const UACFTransitionPreset* Preset = Cast<UACFTransitionPreset>(Asset.GetAsset()))
	{
		LoadPreset(*Preset);
	}
	else
	{
		Stop();
		SelectEffect(Selected);
	}
}

void SACFTransitionPreviewPanel::LoadPreset(const UACFTransitionPreset& Preset)
{
	bPlayingAll = false;
	PreviewPresetPath = Preset.GetPathName();
	Selected = Preset.Close.Effect;
	Settings->Close = Preset.Close;
	Settings->Open = Preset.Open;
	SyncEffectSelection();
	Play(true, true);
}

void SACFTransitionPreviewPanel::SyncEffectSelection()
{
	if (Details.IsValid())
	{
		Details->ForceRefresh();
	}
	const int32 EffectIndex = static_cast<int32>(Selected) - 1;
	if (List.IsValid() && Items.IsValidIndex(EffectIndex))
	{
		List->SetSelection(Items[EffectIndex], ESelectInfo::Direct);
		List->RequestScrollIntoView(Items[EffectIndex]);
	}
}

void SACFTransitionPreviewPanel::Play(bool bClose, bool bOpen)
{
	bPlayOpenAfterClose = bOpen;
	EffectTime = 0.f;
	StartPhase(bClose ? EMode::Closing : EMode::Opening);
	EnsureTimer();
}

void SACFTransitionPreviewPanel::PlayAll()
{
	bPlayingAll = true;
	SelectEffect(EACFTransitionEffect::BlackFade);
	Play(true, true);
}

void SACFTransitionPreviewPanel::Stop()
{
	bPlayingAll = false;
	Mode = EMode::Idle;
	Coverage = 0.f;
	ApplyFrame();
}

void SACFTransitionPreviewPanel::StartPhase(EMode NewMode)
{
	Mode = NewMode;
	PhaseTime = 0.f;
	DrawPhase = NewMode == EMode::Opening ? EACFTransitionPhase::Open : EACFTransitionPhase::Close;
	Coverage = NewMode == EMode::Closing ? 0.f : 1.f;
	ApplyFrame();
}

void SACFTransitionPreviewPanel::EnsureTimer()
{
	if (!Timer.IsValid())
	{
		Timer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SACFTransitionPreviewPanel::OnTimer));
	}
}

EActiveTimerReturnType SACFTransitionPreviewPanel::OnTimer(double, float DeltaTime)
{
	Advance(FMath::Min(DeltaTime, 0.1f));
	if (Mode == EMode::Idle)
	{
		Timer.Reset();
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

void SACFTransitionPreviewPanel::Advance(float DeltaSeconds)
{
	if (Mode == EMode::Idle)
	{
		return;
	}
	PhaseTime += DeltaSeconds;
	EffectTime += DeltaSeconds;
	const FACFTransitionSettings& S = Mode == EMode::Opening ? Settings->Open : Settings->Close;
	const float Alpha = S.Duration > 0.f ? FMath::Clamp(PhaseTime / S.Duration, 0.f, 1.f) : 1.f;
	switch (Mode)
	{
	case EMode::Closing:
		Coverage = ACFTransition::Ease(S, Alpha);
		if (Alpha >= 1.f)
		{
			if (bPlayOpenAfterClose)
			{
				Mode = EMode::Holding;
				PhaseTime = 0.f;
			}
			else
			{
				Mode = EMode::Idle;
			}
		}
		break;
	case EMode::Holding:
		Coverage = 1.f;
		if (PhaseTime >= Settings->HoldSeconds)
		{
			StartPhase(EMode::Opening);
		}
		break;
	case EMode::Opening:
		Coverage = 1.f - ACFTransition::Ease(S, Alpha);
		if (Alpha >= 1.f)
		{
			Mode = EMode::Idle;
			Coverage = 0.f;
			if (bPlayingAll && static_cast<int32>(Selected) < ACFTransition::NumEffects)
			{
				SelectEffect(ACFTransition::FromId(static_cast<int32>(Selected) + 1));
				Play(true, true);
				return;
			}
			bPlayingAll = false;
			if (Settings->bLoop)
			{
				Play(true, true);
				return;
			}
		}
		break;
	default:
		break;
	}
	ApplyFrame();
}

void SACFTransitionPreviewPanel::ApplyFrame()
{
	if (!View.IsValid() || !Settings.IsValid())
	{
		return;
	}
	const FACFTransitionSettings& S = DrawPhase == EACFTransitionPhase::Open ? Settings->Open : Settings->Close;
	const FACFTransitionEffectInfo& Info = ACFTransition::GetEffectInfo(S.Effect);
	const bool bCapture = DrawPhase == EACFTransitionPhase::Close ? Info.bUsesSnapshot : Info.bOpenUsesSnapshot;
	View->Apply(S, DrawPhase, Coverage, EffectTime, bCapture ? Picture.Get() : nullptr);
}

bool SACFTransitionPreviewPanel::PlayInPie()
{
	if (!GEditor || !GEditor->PlayWorld)
	{
		UE_LOG(LogACFTransition, Warning, TEXT("ACF Transition Preview: start PIE first, then press Play in PIE."));
		return false;
	}
	UACFTransitionSubsystem* Subsystem = UACFTransitionSubsystem::Get(GEditor->PlayWorld);
	if (!Subsystem)
	{
		return false;
	}
	if (!Subsystem->PlayPreview(Settings->Close, Settings->Open, Settings->HoldSeconds).IsValid())
	{
		UE_LOG(LogACFTransition, Warning, TEXT("ACF Transition Preview: Play in PIE needs valid Close and Open effects and a clear, idle viewport."));
		return false;
	}
	return true;
}

FReply SACFTransitionPreviewPanel::SaveAsPreset()
{
	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = UACFTransitionPreset::StaticClass();
	FString Clean;
	for (TCHAR Ch : ACFTransition::GetEffectInfo(Selected).Name)
	{
		if (FChar::IsAlnum(Ch)) { Clean.AppendChar(Ch); }
	}
	IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	if (UACFTransitionPreset* Preset = Cast<UACFTransitionPreset>(Tools.CreateAssetWithDialog(TEXT("DA_Transition_") + Clean, TEXT("/Game"), UACFTransitionPreset::StaticClass(), Factory)))
	{
		Preset->Close = Settings->Close;
		Preset->Open = Settings->Open;
		Preset->MarkPackageDirty();
	}
	return FReply::Handled();
}

FText SACFTransitionPreviewPanel::GetStatusText() const
{
	const FACFTransitionEffectInfo& Info = ACFTransition::GetEffectInfo(Selected);
	static const TCHAR* Modes[] = { TEXT("Idle"), TEXT("Closing"), TEXT("Covered"), TEXT("Opening") };
	return FText::FromString(FString::Printf(TEXT("%02d %s - %s  coverage %.2f   %s"), Info.Id, *Info.Name, Modes[static_cast<int32>(Mode)], Coverage, *Info.Description));
}

#undef LOCTEXT_NAMESPACE
