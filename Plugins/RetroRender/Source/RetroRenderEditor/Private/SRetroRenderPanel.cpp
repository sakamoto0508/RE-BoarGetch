#include "SRetroRenderPanel.h"
#include "RetroLevelMaterials.h"
#include "RetroRenderEditorTypes.h"
#include "Rendering/RetroRenderActor.h"
#include "Rendering/RetroRenderComponent.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SRetroRenderPanel"

static TSharedRef<IDetailsView> MakeDetails(UObject* Object)
{
	FPropertyEditorModule& Module = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Args.bAllowSearch = true;
	Args.bHideSelectionTip = true;
	TSharedRef<IDetailsView> View = Module.CreateDetailView(Args);
	View->SetObject(Object);
	return View;
}

void SRetroRenderPanel::Construct(const FArguments& Args)
{
	LookState.Reset(NewObject<URetroLookEditorState>());
	TextureOptions.Reset(NewObject<URetroTextureOptions>());
	MeshOptions.Reset(NewObject<URetroDecimateOptions>());
	PullLook();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[TabButton(LOCTEXT("Look", "Look"), 0)]
			+ SHorizontalBox::Slot().AutoWidth()[TabButton(LOCTEXT("Textures", "Textures"), 1)]
			+ SHorizontalBox::Slot().AutoWidth()[TabButton(LOCTEXT("Meshes", "Meshes"), 2)]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(Switcher, SWidgetSwitcher)
			+ SWidgetSwitcher::Slot()[BuildLook()]
			+ SWidgetSwitcher::Slot()[BuildTextures()]
			+ SWidgetSwitcher::Slot()[BuildMeshes()]
		]
	];
}

TSharedRef<SWidget> SRetroRenderPanel::TabButton(const FText& Label, int32 Index)
{
	return SNew(SButton).Text(Label).OnClicked_Lambda([this, Index]() { Switcher->SetActiveWidgetIndex(Index); return FReply::Handled(); });
}

TSharedRef<SWidget> SRetroRenderPanel::ActionButton(const FText& Label, const FText& Tip, TFunction<void()> Action)
{
	return SNew(SBox).Padding(2)[SNew(SButton).Text(Label).ToolTipText(Tip).OnClicked_Lambda([Action]() { Action(); return FReply::Handled(); })];
}

void SRetroRenderPanel::SetActiveTab(int32 Index)
{
	if (Switcher) Switcher->SetActiveWidgetIndex(Index);
}

void SRetroRenderPanel::TestPreviewTextures(const TArray<UTexture2D*>& Textures)
{
	TextureReport = RetroTextureUtility::Describe(RetroTextureUtility::Plan(Textures, *TextureOptions, false), false);
}

// ------------------------------------------------------------------ Look
TArray<URetroRenderComponent*> SRetroRenderPanel::LiveComponents() const
{
	TArray<URetroRenderComponent*> Result;
	for (TObjectIterator<URetroRenderComponent> It; It; ++It)
	{
		UWorld* World = It->GetWorld();
		if (!It->IsRegistered() || !World || It->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) continue;
		if (World->WorldType == EWorldType::Editor || World->WorldType == EWorldType::PIE) Result.Add(*It);
	}
	// PIE first: while playing, the panel follows what the player sees.
	Result.Sort([](const URetroRenderComponent& A, const URetroRenderComponent& B) { return A.GetWorld()->WorldType == EWorldType::PIE && B.GetWorld()->WorldType != EWorldType::PIE; });
	return Result;
}

URetroRenderComponent* SRetroRenderPanel::Primary() const
{
	TArray<URetroRenderComponent*> All = LiveComponents();
	return All.IsEmpty() ? nullptr : All[0];
}

void SRetroRenderPanel::PullLook()
{
	if (URetroRenderComponent* C = Primary())
	{
		Preset = C->GetPreset();
		bCRTOverlay = C->IsCRTOverlay();
		LookState->Parameters = C->GetParameters();
	}
	else
	{
		LookState->Parameters = MakeDefaultRetroPreset(Preset);
	}
	if (LookDetails) LookDetails->ForceRefresh();
}

void SRetroRenderPanel::PushLook()
{
	for (URetroRenderComponent* C : LiveComponents())
	{
		if (C->GetPreset() != Preset) C->SetPreset(Preset);
		C->SetCRTOverlay(bCRTOverlay);
		C->SetPresetParameters(LookState->Parameters);
	}
}

void SRetroRenderPanel::SelectPreset(ERetroRenderPreset NewPreset)
{
	Preset = NewPreset;
	for (URetroRenderComponent* C : LiveComponents())
	{
		// Editor-world value is serialized with the level; PIE changes are runtime only.
		if (C->GetWorld()->WorldType == EWorldType::Editor) C->Modify();
		C->SetPreset(Preset);
	}
	PullLook();
	if (!Primary()) LookState->Parameters = MakeDefaultRetroPreset(Preset);
}

void SRetroRenderPanel::AddActorToLevel()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return;
	for (TActorIterator<ARetroRenderActor> It(World); It; ++It) { GEditor->SelectNone(false, true); GEditor->SelectActor(*It, true, true); return; }
	const FScopedTransaction Transaction(LOCTEXT("AddActor", "Add Retro Render Settings"));
	ARetroRenderActor* Actor = World->SpawnActor<ARetroRenderActor>();
	Actor->Renderer->InitialPreset = Preset;
	Actor->Renderer->SetPreset(Preset);
	GEditor->SelectNone(false, true);
	GEditor->SelectActor(Actor, true, true);
	PullLook();
}

void SRetroRenderPanel::SaveToPresetAsset()
{
	URetroRenderComponent* C = Primary();
	URetroRenderPresetData* Data = C ? C->PresetData.LoadSynchronous() : nullptr;
	if (!Data || !Data->Presets.IsValidIndex(int32(Preset))) return;
	const FText Message = FText::Format(LOCTEXT("Confirm", "Overwrite the {0} defaults in {1} with the current values?"),
		FText::FromString(RetroPresetName(Preset)), FText::FromString(Data->GetPathName()));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Message) != EAppReturnType::Yes) return;
	Data->Modify();
	Data->Presets[int32(Preset)] = LookState->Parameters;
	Data->MarkPackageDirty();
	UEditorLoadingAndSavingUtils::SavePackages({Data->GetPackage()}, true);
}

FText SRetroRenderPanel::StatusText() const
{
	const TArray<URetroRenderComponent*> All = LiveComponents();
	if (All.IsEmpty()) return LOCTEXT("NoActor", "No Retro Render component in the open level. Use 'Add to Level' (or open L_RetroCompare).");
	int32 Pie = 0;
	for (URetroRenderComponent* C : All) Pie += C->GetWorld()->WorldType == EWorldType::PIE ? 1 : 0;
	return FText::FromString(FString::Printf(TEXT("Preset %s%s  |  driving %d component(s): %d editor, %d PIE"), *RetroPresetName(Preset),
		bCRTOverlay ? TEXT(" + CRT") : TEXT(""), All.Num(), All.Num() - Pie, Pie));
}

TSharedRef<SWidget> SRetroRenderPanel::BuildLook()
{
	LookDetails = MakeDetails(LookState.Get());
	LookDetails->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&) { PushLook(); });
	TSharedRef<SHorizontalBox> Presets = SNew(SHorizontalBox);
	for (uint8 i = 0; i < 5; ++i)
	{
		const ERetroRenderPreset P = ERetroRenderPreset(i);
		Presets->AddSlot().AutoWidth().Padding(2)
		[
			SNew(SButton).Text(FText::FromString(RetroPresetName(P)))
			.ButtonColorAndOpacity_Lambda([this, P]() { return Preset == P ? FLinearColor(1.f, .75f, .1f) : FLinearColor::White; })
			.OnClicked_Lambda([this, P]() { SelectPreset(P); return FReply::Handled(); })
		];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)[Presets]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SWrapBox).UseAllottedSize(true)
			+ SWrapBox::Slot()
			[
				SNew(SCheckBox).IsChecked_Lambda([this]() { return bCRTOverlay ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bCRTOverlay = S == ECheckBoxState::Checked; PushLook(); })
				[SNew(STextBlock).Text(LOCTEXT("CRT", "CRT overlay"))]
			]
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("Add", "Add to Level"), LOCTEXT("AddTip", "Spawn (or select) a Retro Render Settings actor in the open level."), [this]() { AddActorToLevel(); })]
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("Pull", "Refresh"), LOCTEXT("PullTip", "Read the values from the live component."), [this]() { PullLook(); })]
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("Reset", "Reset Preset"), LOCTEXT("ResetTip", "Restore the preset defaults from DA_RetroPresets."), [this]() { for (URetroRenderComponent* C : LiveComponents()) C->ResetCurrentPreset(); PullLook(); })]
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("Save", "Save as Preset Default"), LOCTEXT("SaveTip", "Write these values into DA_RetroPresets for the current preset."), [this]() { SaveToPresetAsset(); })]
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("ScanLevel", "Scan Level Materials"),
				LOCTEXT("ScanLevelTip", "List the open level's materials and whether they have the retro geometry patch (no changes)."),
				[this]() { LevelReport = RetroLevelMaterials::RunLevelScan(true); })]
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("PatchLevel", "Patch Level Materials"),
				LOCTEXT("PatchLevelTip", "Add vertex snapping to the open level's unpatched project materials (backed up first; revert with PatchRetroMaterials.py -RetroUnpatch)."),
				[this]()
				{
					const FString Preview = RetroLevelMaterials::RunLevelScan(true);
					if (FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Preview + TEXT("\nPatch the materials marked [patch] / [reparent]?"))) == EAppReturnType::Yes)
					{
						LevelReport = RetroLevelMaterials::RunLevelScan(false);
					}
				})]
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("Off", "Original (OFF)"), LOCTEXT("OffTip", "Disable every retro effect; pipeline CVars and clouds are restored."), [this]() { bCRTOverlay = false; SelectPreset(ERetroRenderPreset::OFF); PushLook(); })]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6, 2)[SNew(STextBlock).Text_Lambda([this]() { return StatusText(); }).AutoWrapText(true)]
		+ SVerticalBox::Slot().AutoHeight().MaxHeight(160.f).Padding(6, 2)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(LevelReport); })]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[LookDetails.ToSharedRef()];
}

// ------------------------------------------------------------------ Textures
TArray<UTexture2D*> SRetroRenderPanel::SelectedTextures() const
{
	TArray<FAssetData> Assets;
	FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().GetSelectedAssets(Assets);
	TArray<UTexture2D*> Result;
	for (const FAssetData& A : Assets) if (UTexture2D* T = Cast<UTexture2D>(A.GetAsset())) Result.Add(T);
	return Result;
}

TSharedRef<SWidget> SRetroRenderPanel::BuildTextures()
{
	TextureDetails = MakeDetails(TextureOptions.Get());
	auto Run = [this](bool bApply, bool bRevert)
	{
		const TArray<RetroTextureUtility::FPlanItem> Plan = RetroTextureUtility::Plan(SelectedTextures(), *TextureOptions, bRevert);
		TextureReport = RetroTextureUtility::Describe(Plan, bRevert);
		if (!bApply) return;
		const int32 Count = bRevert ? RetroTextureUtility::Revert(Plan, TextureOptions->bSaveAfterApply) : RetroTextureUtility::Apply(Plan, *TextureOptions);
		TextureReport = FString::Printf(TEXT("%s %d textures.\n\n"), bRevert ? TEXT("Reverted") : TEXT("Applied to"), Count) + TextureReport;
	};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("TexHelp",
			"Select Texture2D assets in the Content Browser. Preview lists every change first. Originals are stored in the package metadata; Revert restores them. Source art is never modified."))]
		+ SVerticalBox::Slot().AutoHeight()[TextureDetails.ToSharedRef()]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SWrapBox).UseAllottedSize(true)
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("Preview", "Preview Selected"), FText(), [Run]() { Run(false, false); })]
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("Apply", "Apply to Selected"), FText(), [Run]() { Run(true, false); })]
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("PreviewRevert", "Preview Revert"), FText(), [Run]() { Run(false, true); })]
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("Revert", "Revert Selected"), FText(), [Run]() { Run(true, true); })]
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(6)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(TextureReport); })]
		];
}

// ------------------------------------------------------------------ Meshes
TArray<UStaticMesh*> SRetroRenderPanel::SelectedMeshes() const
{
	TArray<FAssetData> Assets;
	FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().GetSelectedAssets(Assets);
	TArray<UStaticMesh*> Result;
	for (const FAssetData& A : Assets) if (UStaticMesh* M = Cast<UStaticMesh>(A.GetAsset())) Result.Add(M);
	return Result;
}

TSharedRef<SWidget> SRetroRenderPanel::BuildMeshes()
{
	MeshDetails = MakeDetails(MeshOptions.Get());
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("MeshHelp",
			"Bake: duplicates selected Static Meshes (Content Browser) as <Name>_Retro with a reduced LOD0; the source asset is untouched. "
			"Replace: swaps meshes on the selected level actors (undoable; original path kept in a component tag). Skeletal meshes, Landscape and collision assets are not converted."))]
		+ SVerticalBox::Slot().AutoHeight()[MeshDetails.ToSharedRef()]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SWrapBox).UseAllottedSize(true)
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("Bake", "Bake Selected Meshes"), FText(), [this]()
			{
				TArray<RetroMeshBaker::FResult> Results;
				for (UStaticMesh* M : SelectedMeshes()) Results.Add(RetroMeshBaker::Bake(M, *MeshOptions));
				MeshReport = RetroMeshBaker::Describe(Results);
			})]
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("Replace", "Replace Selected Actors"), FText(), [this]() { MeshReport = RetroMeshBaker::ReplaceSelected(*MeshOptions); })]
			+ SWrapBox::Slot()[ActionButton(LOCTEXT("Restore", "Restore Selected Actors"), FText(), [this]() { MeshReport = RetroMeshBaker::RestoreSelected(); })]
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(6)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(MeshReport); })]
		];
}

#undef LOCTEXT_NAMESPACE
