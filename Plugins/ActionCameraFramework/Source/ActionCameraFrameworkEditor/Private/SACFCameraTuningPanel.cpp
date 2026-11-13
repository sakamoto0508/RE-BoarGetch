#include "SACFCameraTuningPanel.h"
#include "ACFCameraDirector.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "ACFCameraTuningPanel"

namespace ACFTuningPanelView
{
	static FText StageName(EACFCameraNodeStage Stage)
	{
		switch (Stage)
		{
		case EACFCameraNodeStage::Body: return LOCTEXT("Body", "Body");
		case EACFCameraNodeStage::Aim: return LOCTEXT("Aim", "Aim");
		case EACFCameraNodeStage::Framing: return LOCTEXT("Framing", "Framing");
		case EACFCameraNodeStage::PostBlend: return LOCTEXT("PostBlend", "PostBlend");
		default: return FText::GetEmpty();
		}
	}

	static FString NodeName(FName ClassName)
	{
		FString Name = ClassName.ToString();
		Name.RemoveFromStart(TEXT("ACFCameraNode_"));
		return Name;
	}

	static FString FormatValue(const FACFCameraTuningValue& Value, const FVector& V)
	{
		return Value.bScalar ? FString::Printf(TEXT("%.2f"), V.X) : FString::Printf(TEXT("%.2f, %.2f, %.2f"), V.X, V.Y, V.Z);
	}

	static FString FormatRange(double Min, double Max, EACFCameraTuningParameter Parameter)
	{
		if (Parameter == EACFCameraTuningParameter::FieldOfView)
		{
			return TEXT("(0, 180) deg");
		}
		const FString Low = FMath::IsFinite(Min) ? FString::Printf(TEXT("[%g"), Min) : FString(TEXT("(-inf"));
		const FString High = FMath::IsFinite(Max) ? FString::Printf(TEXT("%g]"), Max) : FString(TEXT("inf)"));
		return Low + TEXT(", ") + High;
	}

	/** Slider range of the editors (convenience only: typed values outside it reach the runtime, which validates them). */
	static void SliderRange(EACFCameraTuningParameter Parameter, double& OutMin, double& OutMax, double& OutDelta)
	{
		switch (Parameter)
		{
		case EACFCameraTuningParameter::FollowDamping: OutMin = 0.0; OutMax = 2.0; OutDelta = 0.01; break;
		case EACFCameraTuningParameter::FieldOfView: OutMin = 5.0; OutMax = 170.0; OutDelta = 0.5; break;
		case EACFCameraTuningParameter::CollisionRadius: OutMin = 0.0; OutMax = 100.0; OutDelta = 0.5; break;
		default: OutMin = -1000.0; OutMax = 1000.0; OutDelta = 1.0; break;
		}
	}

	class SRequestRow : public SMultiColumnTableRow<TSharedPtr<FACFCameraRequestInfo>>
	{
	public:
		SLATE_BEGIN_ARGS(SRequestRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TSharedRef<STableViewBase>& Owner, TSharedPtr<FACFCameraRequestInfo> InItem)
		{
			Item = InItem;
			SMultiColumnTableRow::Construct(FSuperRowType::FArguments(), Owner);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			FString Text;
			if (Column == TEXT("Handle")) { Text = Item->Handle.ToString(); }
			else if (Column == TEXT("Preset")) { Text = Item->Preset.ToString() + (Item->DebugName.IsNone() ? FString() : FString::Printf(TEXT("  (%s)"), *Item->DebugName.ToString())); }
			else if (Column == TEXT("Priority")) { Text = FString::FromInt(Item->Priority); }
			else if (Column == TEXT("Active")) { Text = Item->bActive ? TEXT("active") : TEXT("-"); }
			else if (Column == TEXT("Tuning")) { Text = Item->bTuning ? TEXT("TUNING") : TEXT("-"); }
			return SNew(STextBlock).Text(FText::FromString(Text));
		}

	private:
		TSharedPtr<FACFCameraRequestInfo> Item;
	};

	class SNodeRow : public SMultiColumnTableRow<TSharedPtr<FACFCameraTuningNodeInfo>>
	{
	public:
		SLATE_BEGIN_ARGS(SNodeRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TSharedRef<STableViewBase>& Owner, TSharedPtr<FACFCameraTuningNodeInfo> InItem)
		{
			Item = InItem;
			SMultiColumnTableRow::Construct(FSuperRowType::FArguments(), Owner);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			if (Column == TEXT("Index")) { return SNew(STextBlock).Text(FText::AsNumber(Item->NodeIndex)); }
			if (Column == TEXT("Node")) { return SNew(STextBlock).Text(FText::FromString(NodeName(Item->NodeClass))); }
			if (Column == TEXT("Stage")) { return SNew(STextBlock).Text(StageName(Item->Stage)); }
			if (Column == TEXT("Enabled")) { return SNew(STextBlock).Text(Item->bEnabled ? LOCTEXT("On", "on") : LOCTEXT("Off", "off")); }
			TArray<FString> Names;
			for (const EACFCameraTuningParameter Parameter : Item->Parameters)
			{
				Names.Add(ACFCameraTuning::GetParameterName(Parameter));
			}
			return SNew(STextBlock).Text(FText::FromString(Names.IsEmpty() ? FString(TEXT("-")) : FString::Join(Names, TEXT(", "))));
		}

	private:
		TSharedPtr<FACFCameraTuningNodeInfo> Item;
	};
}

void SACFCameraTuningPanel::Construct(const FArguments& InArgs)
{
	using namespace ACFTuningPanelView;
	Model = MakeShared<FACFCameraTuningPanelModel>();
	if (InArgs._DirectorForTesting.IsValid())
	{
		Model->SetDirectorForTesting(InArgs._DirectorForTesting.Get());
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.f)
		[
			SNew(SVerticalBox)
			// Target: PIE World and local player.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(STextBlock).Text(LOCTEXT("World", "PIE World"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)
				[
					SAssignNew(WorldCombo, SComboBox<TSharedPtr<FACFCameraTuningPanelModel::FWorldEntry>>)
					.OptionsSource(&WorldItems)
					.OnGenerateWidget_Lambda([](TSharedPtr<FACFCameraTuningPanelModel::FWorldEntry> Item) { return SNew(STextBlock).Text(FText::FromString(Item->Label)); })
					.OnSelectionChanged_Lambda([this](TSharedPtr<FACFCameraTuningPanelModel::FWorldEntry> Item, ESelectInfo::Type Info)
					{
						if (Item && Info != ESelectInfo::Direct)
						{
							Model->SelectWorld(Item->World.Get());
							Refresh();
						}
					})
					[
						SNew(STextBlock).Text_Lambda([this]()
						{
							const UWorld* W = Model->GetWorld();
							for (const TSharedPtr<FACFCameraTuningPanelModel::FWorldEntry>& Item : WorldItems)
							{
								if (Item->World.Get() == W && W) { return FText::FromString(Item->Label); }
							}
							return WorldItems.IsEmpty() ? LOCTEXT("NoPIE", "PIE is not running") : LOCTEXT("PickWorld", "Select a PIE World");
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(STextBlock).Text(LOCTEXT("Player", "Player"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SAssignNew(PlayerCombo, SComboBox<TSharedPtr<FACFCameraTuningPanelModel::FPlayerEntry>>)
					.OptionsSource(&PlayerItems)
					.OnGenerateWidget_Lambda([](TSharedPtr<FACFCameraTuningPanelModel::FPlayerEntry> Item) { return SNew(STextBlock).Text(FText::FromString(Item->Label)); })
					.OnSelectionChanged_Lambda([this](TSharedPtr<FACFCameraTuningPanelModel::FPlayerEntry> Item, ESelectInfo::Type Info)
					{
						if (Item && Info != ESelectInfo::Direct)
						{
							Model->SelectPlayer(Item->Player.Get());
							Refresh();
						}
					})
					[
						SNew(STextBlock).Text_Lambda([this]()
						{
							for (const TSharedPtr<FACFCameraTuningPanelModel::FPlayerEntry>& Item : PlayerItems)
							{
								if (Item->Player.Get() == Model->GetPlayer() && Model->GetPlayer()) { return FText::FromString(Item->Label); }
							}
							return LOCTEXT("PickPlayer", "Select a local player");
						})
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock).Text(this, &SACFCameraTuningPanel::GetTargetText).AutoWrapText(true)
			]
			// Operations on the selected request.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SButton).Text(LOCTEXT("Start", "Start Tuning")).IsEnabled(this, &SACFCameraTuningPanel::CanStart).OnClicked(this, &SACFCameraTuningPanel::OnStart)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SButton).Text(LOCTEXT("Stop", "Stop Tuning (discard)")).IsEnabled_Lambda([this]() { return Model->IsTuning(); }).OnClicked(this, &SACFCameraTuningPanel::OnStop)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SButton).Text(LOCTEXT("ResetAll", "Reset All")).IsEnabled_Lambda([this]() { return Model->IsTuning(); }).OnClicked(this, &SACFCameraTuningPanel::OnResetAll)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text_Lambda([this]() { return Model->IsShowingOriginal() ? LOCTEXT("ShowTuned", "Show Tuned Values") : LOCTEXT("ShowOriginal", "Show Original Values"); })
					.ToolTipText(LOCTEXT("CompareTip", "A / B: puts the asset values back for a moment (the tuned values are kept aside), then writes the tuned values again. No second camera is created."))
					.IsEnabled_Lambda([this]() { return Model->IsTuning(); })
					.OnClicked(this, &SACFCameraTuningPanel::OnCompare)
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SSplitter).Orientation(Orient_Vertical)
				+ SSplitter::Slot().Value(0.25f)
				[
					SAssignNew(RequestList, SListView<TSharedPtr<FACFCameraRequestInfo>>)
					.ListItemsSource(&RequestItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SACFCameraTuningPanel::MakeRequestRow)
					.OnSelectionChanged(this, &SACFCameraTuningPanel::OnRequestSelected)
					.HeaderRow(SNew(SHeaderRow)
						+ SHeaderRow::Column(TEXT("Handle")).DefaultLabel(LOCTEXT("ColHandle", "Request")).FillWidth(0.15f)
						+ SHeaderRow::Column(TEXT("Preset")).DefaultLabel(LOCTEXT("ColPreset", "Preset (asset)")).FillWidth(0.5f)
						+ SHeaderRow::Column(TEXT("Priority")).DefaultLabel(LOCTEXT("ColPriority", "Priority")).FillWidth(0.1f)
						+ SHeaderRow::Column(TEXT("Active")).DefaultLabel(LOCTEXT("ColActive", "State")).FillWidth(0.12f)
						+ SHeaderRow::Column(TEXT("Tuning")).DefaultLabel(LOCTEXT("ColTuning", "Tuning")).FillWidth(0.13f))
				]
				+ SSplitter::Slot().Value(0.25f)
				[
					SAssignNew(NodeList, SListView<TSharedPtr<FACFCameraTuningNodeInfo>>)
					.ListItemsSource(&NodeItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SACFCameraTuningPanel::MakeNodeRow)
					.OnSelectionChanged(this, &SACFCameraTuningPanel::OnNodeSelected)
					.HeaderRow(SNew(SHeaderRow)
						+ SHeaderRow::Column(TEXT("Index")).DefaultLabel(LOCTEXT("ColIndex", "#")).FillWidth(0.06f)
						+ SHeaderRow::Column(TEXT("Node")).DefaultLabel(LOCTEXT("ColNode", "Node")).FillWidth(0.25f)
						+ SHeaderRow::Column(TEXT("Stage")).DefaultLabel(LOCTEXT("ColStage", "Stage")).FillWidth(0.14f)
						+ SHeaderRow::Column(TEXT("Enabled")).DefaultLabel(LOCTEXT("ColEnabled", "Enabled")).FillWidth(0.1f)
						+ SHeaderRow::Column(TEXT("Params")).DefaultLabel(LOCTEXT("ColParams", "Tunable parameters")).FillWidth(0.45f))
				]
				+ SSplitter::Slot().Value(0.3f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(ParameterBox, SVerticalBox)
					]
				]
				+ SSplitter::Slot().Value(0.2f)
				[
					MakeChangesSection()
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Model->GetLastMessage()); }).AutoWrapText(true)
			]
		]
	];

	Refresh();
	RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SACFCameraTuningPanel::OnTimer));
}

EActiveTimerReturnType SACFCameraTuningPanel::OnTimer(double CurrentTime, float DeltaTime)
{
	Refresh();
	return EActiveTimerReturnType::Continue;
}

void SACFCameraTuningPanel::Refresh()
{
	Model->Validate();

	// PIE Worlds (auto-select the only one, and its first player, for the common single-player case).
	TArray<FACFCameraTuningPanelModel::FWorldEntry> Worlds;
	FACFCameraTuningPanelModel::GetPIEWorlds(Worlds);
	bool bWorldsChanged = Worlds.Num() != WorldItems.Num();
	for (int32 I = 0; !bWorldsChanged && I < Worlds.Num(); ++I)
	{
		bWorldsChanged = Worlds[I].World != WorldItems[I]->World || Worlds[I].Label != WorldItems[I]->Label;
	}
	if (bWorldsChanged)
	{
		WorldItems.Reset();
		for (const FACFCameraTuningPanelModel::FWorldEntry& Entry : Worlds)
		{
			WorldItems.Add(MakeShared<FACFCameraTuningPanelModel::FWorldEntry>(Entry));
		}
		if (WorldCombo)
		{
			WorldCombo->RefreshOptions();
		}
	}
	if (!Model->GetWorld() && WorldItems.Num() == 1)
	{
		Model->SelectWorld(WorldItems[0]->World.Get());
	}

	TArray<FACFCameraTuningPanelModel::FPlayerEntry> Players;
	FACFCameraTuningPanelModel::GetPlayers(Model->GetWorld(), Players);
	bool bPlayersChanged = Players.Num() != PlayerItems.Num();
	for (int32 I = 0; !bPlayersChanged && I < Players.Num(); ++I)
	{
		bPlayersChanged = Players[I].Player != PlayerItems[I]->Player || Players[I].Label != PlayerItems[I]->Label;
	}
	if (bPlayersChanged)
	{
		PlayerItems.Reset();
		for (const FACFCameraTuningPanelModel::FPlayerEntry& Entry : Players)
		{
			PlayerItems.Add(MakeShared<FACFCameraTuningPanelModel::FPlayerEntry>(Entry));
		}
		if (PlayerCombo)
		{
			PlayerCombo->RefreshOptions();
		}
	}
	if (!Model->GetPlayer() && Model->GetWorld() && PlayerItems.Num() > 0)
	{
		Model->SelectPlayer(PlayerItems[0]->Player.Get());
	}

	// Requests and nodes: lists are rebuilt only when their content changed.
	TArray<FACFCameraRequestInfo> Requests;
	Model->GetRequests(Requests);
	bool bRequestsChanged = Requests.Num() != RequestItems.Num();
	for (int32 I = 0; !bRequestsChanged && I < Requests.Num(); ++I)
	{
		const FACFCameraRequestInfo& A = Requests[I];
		const FACFCameraRequestInfo& B = *RequestItems[I];
		bRequestsChanged = A.Handle != B.Handle || A.Preset != B.Preset || A.bActive != B.bActive || A.bTuning != B.bTuning || A.Priority != B.Priority;
	}
	if (bRequestsChanged)
	{
		RequestItems.Reset();
		for (const FACFCameraRequestInfo& Info : Requests)
		{
			RequestItems.Add(MakeShared<FACFCameraRequestInfo>(Info));
		}
		if (RequestList)
		{
			RequestList->RequestListRefresh();
			for (const TSharedPtr<FACFCameraRequestInfo>& Item : RequestItems)
			{
				if (Item->Handle == Model->GetRequest())
				{
					RequestList->SetItemSelection(Item, true, ESelectInfo::Direct);
				}
			}
		}
	}

	TArray<FACFCameraTuningNodeInfo> Nodes;
	Model->GetNodes(Nodes);
	bool bNodesChanged = Nodes.Num() != NodeItems.Num();
	for (int32 I = 0; !bNodesChanged && I < Nodes.Num(); ++I)
	{
		bNodesChanged = Nodes[I].NodeIndex != NodeItems[I]->NodeIndex || Nodes[I].NodeClass != NodeItems[I]->NodeClass || Nodes[I].bEnabled != NodeItems[I]->bEnabled
			|| Nodes[I].Parameters != NodeItems[I]->Parameters;
	}
	if (bNodesChanged)
	{
		NodeItems.Reset();
		for (const FACFCameraTuningNodeInfo& Info : Nodes)
		{
			NodeItems.Add(MakeShared<FACFCameraTuningNodeInfo>(Info));
		}
		if (NodeList)
		{
			NodeList->RequestListRefresh();
			for (const TSharedPtr<FACFCameraTuningNodeInfo>& Item : NodeItems)
			{
				if (Item->NodeIndex == Model->GetNodeIndex())
				{
					NodeList->SetItemSelection(Item, true, ESelectInfo::Direct);
				}
			}
		}
	}

	Model->GetParameterRows(Rows);
	FString Shape = FString::Printf(TEXT("%s/%d:"), *Model->GetRequest().ToString(), Model->GetNodeIndex());
	for (const FACFCameraTuningPanelModel::FParameterRow& Row : Rows)
	{
		Shape += FString::Printf(TEXT("%d%d%d,"), static_cast<int32>(Row.Parameter), Row.bAvailable ? 1 : 0, Row.Value.bScalar ? 1 : 0);
	}
	if (Shape != RowsShape)
	{
		RowsShape = Shape;
		RebuildParameterBox();
	}

	// Phase 7A-3: keep the change set current (read-only), rebuild its rows only when the list itself changed. The row
	// states are read live, so Undo / Redo / external edits show up without a rebuild.
	Model->CaptureAll();
	FString NewChangesShape;
	const TArray<FACFCameraTuningChangeSet::FChange>& Changes = Model->GetChangeSet().GetChanges();
	for (int32 Index = 0; Index < Changes.Num(); ++Index)
	{
		if (Changes[Index].IsChanged())
		{
			NewChangesShape += FString::Printf(TEXT("%d:%s|%s|%d|%d;"), Index, *Changes[Index].SourceKey, *Changes[Index].Preset.ToString(),
				Changes[Index].NodeIndex, static_cast<int32>(Changes[Index].Parameter));
		}
	}
	if (NewChangesShape != ChangesShape)
	{
		ChangesShape = NewChangesShape;
		RebuildChangesBox();
	}
}

// --- Phase 7A-3: changes for the preset assets -------------------------------------------------------------------------

TSharedRef<SWidget> SACFCameraTuningPanel::MakeChangesSection()
{
	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(LOCTEXT("ChangesTitle", "Changes for the preset assets (kept after PIE ends; written only by Apply to Asset)"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
			[
				SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return GetChangesSummary(); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
			[
				SAssignNew(ChangesBox, SVerticalBox)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
				[
					SNew(SButton).Text(LOCTEXT("Check", "Check"))
					.ToolTipText(LOCTEXT("CheckTip", "Conflicts with the assets as they are now, and Preset Validation of the result. Changes nothing."))
					.IsEnabled_Lambda([this]() { return Model->GetChangeSet().CountState(FACFCameraTuningChangeSet::EState::Pending, true) > 0
						|| Model->GetChangeSet().CountState(FACFCameraTuningChangeSet::EState::Conflict, true) > 0; })
					.OnClicked(this, &SACFCameraTuningPanel::OnCheckChanges)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
				[
					SNew(SButton).Text(LOCTEXT("Apply", "Apply to Asset"))
					.ToolTipText(LOCTEXT("ApplyTip", "Writes the included pending changes to their preset assets in memory, as one undoable step (Edit > Undo). Not available during PIE. Does not save."))
					.IsEnabled_Lambda([this]() { return CanApply(); })
					.OnClicked(this, &SACFCameraTuningPanel::OnApplyChanges)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
				[
					SNew(SButton).Text(LOCTEXT("Save", "Save Asset"))
					.ToolTipText(LOCTEXT("SaveTip", "Saves only the packages of applied changes to disk. Other dirty packages are not saved."))
					.IsEnabled_Lambda([this]() { return Model->GetChangeSet().HasUnsavedApplied(); })
					.OnClicked(this, &SACFCameraTuningPanel::OnSaveChanges)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
				[
					SNew(SButton).Text(LOCTEXT("ClearChanges", "Clear List"))
					.ToolTipText(LOCTEXT("ClearChangesTip", "Forgets the listed changes. Never touches an asset (applied values stay applied)."))
					.IsEnabled_Lambda([this]() { return !ChangesShape.IsEmpty(); })
					.OnClicked(this, &SACFCameraTuningPanel::OnClearChanges)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 2.f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bConfirmWarnings ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bConfirmWarnings = State == ECheckBoxState::Checked; })
					[
						SNew(STextBlock).Text(LOCTEXT("ConfirmWarnings", "Apply despite validation warnings (I reviewed them)"))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
			[
				SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return FText::FromString(ValidationText); })
			]
		];
}

void SACFCameraTuningPanel::RebuildChangesBox()
{
	if (!ChangesBox)
	{
		return;
	}
	ChangesBox->ClearChildren();
	NumChangeRows = 0;
	const TArray<FACFCameraTuningChangeSet::FChange>& Changes = Model->GetChangeSet().GetChanges();
	for (int32 Index = 0; Index < Changes.Num(); ++Index)
	{
		if (!Changes[Index].IsChanged())
		{
			continue;
		}
		++NumChangeRows;
		const FACFCameraTuningChangeSet::FChange& Change = Changes[Index];
		auto Format = [&Change](const FVector& V)
		{
			return Change.bScalar ? FString::Printf(TEXT("%.3f"), V.X) : FString::Printf(TEXT("(%.3f, %.3f, %.3f)"), V.X, V.Y, V.Z);
		};
		const FText What = FText::FromString(FString::Printf(TEXT("%s  ->  %s : node %d %s : %s  %s -> %s"), *Change.SourceLabel, *Change.Preset.GetAssetName(),
			Change.NodeIndex, *Change.NodeClass.ToString(), *ACFCameraTuning::GetParameterName(Change.Parameter), *Format(Change.BaseValue), *Format(Change.Value)));
		ChangesBox->AddSlot().AutoHeight().Padding(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("IncludeTip", "Include this change in Check / Apply."))
				.IsChecked_Lambda([this, Index]() { const auto& All = Model->GetChangeSet().GetChanges(); return All.IsValidIndex(Index) && All[Index].bInclude ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State) { Model->GetChangeSet().SetIncluded(Index, State == ECheckBoxState::Checked); })
			]
			+ SHorizontalBox::Slot().FillWidth(0.7f).VAlign(VAlign_Center).Padding(4.f, 0.f)
			[
				SNew(STextBlock).Text(What).ToolTipText(FText::FromString(Change.Preset.ToString()))
			]
			+ SHorizontalBox::Slot().FillWidth(0.3f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).AutoWrapText(true).Text_Lambda([this, Index]() { return GetChangeStateText(Index); })
			]
		];
	}
}

FText SACFCameraTuningPanel::GetChangeStateText(int32 Index) const
{
	using EState = FACFCameraTuningChangeSet::EState;
	const FACFCameraTuningChangeSet::FStatus Status = Model->GetChangeSet().GetStatus(Index);
	switch (Status.State)
	{
	case EState::Pending: return LOCTEXT("StatePending", "Not applied");
	case EState::Applied: return Status.bUnsaved ? LOCTEXT("StateUnsaved", "Applied, NOT saved") : LOCTEXT("StateSaved", "Applied (asset saved)");
	case EState::Conflict: return FText::Format(LOCTEXT("StateConflict", "Conflict: {0}"), FText::FromString(Status.Reason));
	default: return LOCTEXT("StateUnchanged", "Unchanged");
	}
}

FText SACFCameraTuningPanel::GetChangesSummary() const
{
	using EState = FACFCameraTuningChangeSet::EState;
	const FACFCameraTuningChangeSet& Set = Model->GetChangeSet();
	const int32 Pending = Set.CountState(EState::Pending, false);
	const int32 Applied = Set.CountState(EState::Applied, false);
	const int32 Conflicts = Set.CountState(EState::Conflict, false);
	FString Text = FString::Printf(TEXT("%d not applied, %d applied, %d conflict(s)."), Pending, Applied, Conflicts);
	if (Set.HasUnsavedApplied())
	{
		Text += TEXT(" Unsaved changes in preset assets: use Save Asset (Undo does not change the file on disk).");
	}
	if (FACFCameraTuningChangeSet::IsPlaySessionRunning() && Pending > 0)
	{
		Text += TEXT(" End PIE to apply: the shared preset is in use by the game.");
	}
	return FText::FromString(Text);
}

bool SACFCameraTuningPanel::CanApply() const
{
	return !FACFCameraTuningChangeSet::IsPlaySessionRunning() && Model->GetChangeSet().CountState(FACFCameraTuningChangeSet::EState::Pending, true) > 0;
}

void SACFCameraTuningPanel::ShowReport(const FACFCameraTuningChangeSet::FReport& Report)
{
	TArray<FString> Lines;
	for (const FString& Conflict : Report.Conflicts)
	{
		Lines.Add(FString::Printf(TEXT("CONFLICT  %s"), *Conflict));
	}
	for (const FACFCameraPresetIssue& Issue : Report.Issues)
	{
		const TCHAR* Severity = Issue.Severity == EACFCameraPresetIssueSeverity::Error ? TEXT("ERROR") : Issue.Severity == EACFCameraPresetIssueSeverity::Warning ? TEXT("WARNING") : TEXT("INFO");
		Lines.Add(FString::Printf(TEXT("%s  %s"), Severity, *Issue.Message.ToString()));
	}
	if (!Report.UnsavedPackages.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("UNSAVED  %s"), *FString::Join(Report.UnsavedPackages, TEXT(", "))));
	}
	ValidationText = FString::Join(Lines, TEXT("\n"));
}

FReply SACFCameraTuningPanel::OnCheckChanges()
{
	ShowReport(Model->CheckChanges());
	return FReply::Handled();
}

FReply SACFCameraTuningPanel::OnApplyChanges()
{
	ShowReport(Model->ApplyChanges(bConfirmWarnings));
	return FReply::Handled();
}

FReply SACFCameraTuningPanel::OnSaveChanges()
{
	ShowReport(Model->SaveChanges());
	return FReply::Handled();
}

FReply SACFCameraTuningPanel::OnClearChanges()
{
	Model->GetChangeSet().Clear();
	ValidationText.Reset();
	Refresh();
	return FReply::Handled();
}

void SACFCameraTuningPanel::RebuildParameterBox()
{
	if (!ParameterBox)
	{
		return;
	}
	ParameterBox->ClearChildren();
	if (Model->GetNodeIndex() == INDEX_NONE)
	{
		ParameterBox->AddSlot().AutoHeight().Padding(2.f)
		[
			SNew(STextBlock).Text(Model->GetRequest().IsValid() ? LOCTEXT("PickNode", "Select a node to edit its parameters.") : LOCTEXT("PickRequest", "Select a request."))
		];
	}
	else
	{
		ParameterBox->AddSlot().AutoHeight().Padding(2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.18f)[SNew(STextBlock).Text(LOCTEXT("HParam", "Parameter"))]
			+ SHorizontalBox::Slot().FillWidth(0.18f)[SNew(STextBlock).Text(LOCTEXT("HOriginal", "Original (asset)"))]
			+ SHorizontalBox::Slot().FillWidth(0.34f)[SNew(STextBlock).Text(LOCTEXT("HCurrent", "Current"))]
			+ SHorizontalBox::Slot().FillWidth(0.14f)[SNew(STextBlock).Text(LOCTEXT("HDiff", "Difference"))]
			+ SHorizontalBox::Slot().FillWidth(0.1f)[SNew(STextBlock).Text(LOCTEXT("HRange", "Valid range"))]
			+ SHorizontalBox::Slot().FillWidth(0.06f)[SNullWidget::NullWidget]
		];
	}
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (Rows[Index].bAvailable || Model->GetNodeIndex() == INDEX_NONE || !Rows[Index].Reason.IsEmpty())
		{
			ParameterBox->AddSlot().AutoHeight().Padding(2.f)[MakeParameterRow(Index)];
		}
	}
}

TSharedRef<SWidget> SACFCameraTuningPanel::MakeParameterRow(int32 RowIndex)
{
	using namespace ACFTuningPanelView;
	const FACFCameraTuningPanelModel::FParameterRow& Row = Rows[RowIndex];
	const EACFCameraTuningParameter Parameter = Row.Parameter;
	const FText Name = FText::FromString(ACFCameraTuning::GetParameterName(Parameter));
	if (!Row.bAvailable)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.18f)[SNew(STextBlock).Text(Name)]
			+ SHorizontalBox::Slot().FillWidth(0.82f)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("NotTunable", "Not tunable on this request: {0}"), FText::FromString(Row.Reason)))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			];
	}
	double SliderMin, SliderMax, Delta;
	SliderRange(Parameter, SliderMin, SliderMax, Delta);
	TSharedRef<SHorizontalBox> Editors = SNew(SHorizontalBox);
	const int32 NumAxes = Row.Value.bScalar ? 1 : 3;
	static const FText AxisNames[3] = { LOCTEXT("X", "X"), LOCTEXT("Y", "Y"), LOCTEXT("Z", "Z") };
	for (int32 Axis = 0; Axis < NumAxes; ++Axis)
	{
		if (NumAxes > 1)
		{
			Editors->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f)[SNew(STextBlock).Text(AxisNames[Axis])];
		}
		Editors->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SSpinBox<double>)
			.Value_Lambda([this, RowIndex, Axis]() { return Rows.IsValidIndex(RowIndex) ? Rows[RowIndex].Value.Value[Axis] : 0.0; })
			.MinSliderValue(SliderMin)
			.MaxSliderValue(SliderMax)
			.Delta(Delta)
			.IsEnabled(this, &SACFCameraTuningPanel::CanEdit)
			.ToolTipText_Lambda([this]() { return CanEdit() ? LOCTEXT("EditTip", "Type a value (Enter) or drag. Invalid values are rejected by the runtime.")
				: LOCTEXT("EditDisabledTip", "Start Tuning first (or switch back to the tuned values)."); })
			.OnValueCommitted(this, &SACFCameraTuningPanel::OnComponentCommitted, Parameter, Axis)
			.OnValueChanged_Lambda([this, Parameter, Axis](double Value)
			{
				// Live while dragging: the runtime applies it from the next camera update.
				if (CanEdit())
				{
					Model->SetComponent(Parameter, Axis, Value);
					Model->GetParameterRows(Rows);
				}
			})
		];
	}
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.18f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Name)]
		+ SHorizontalBox::Slot().FillWidth(0.18f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text_Lambda([this, RowIndex]() { return Rows.IsValidIndex(RowIndex) ? FText::FromString(FormatValue(Rows[RowIndex].Value, Rows[RowIndex].Value.OriginalValue)) : FText::GetEmpty(); })
		]
		+ SHorizontalBox::Slot().FillWidth(0.34f)[Editors]
		+ SHorizontalBox::Slot().FillWidth(0.14f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text_Lambda([this, RowIndex]()
			{
				if (!Rows.IsValidIndex(RowIndex) || Rows[RowIndex].Value.Value == Rows[RowIndex].Value.OriginalValue)
				{
					return LOCTEXT("NoDiff", "-");
				}
				const FVector D = Rows[RowIndex].Value.Value - Rows[RowIndex].Value.OriginalValue;
				return FText::FromString(Rows[RowIndex].Value.bScalar ? FString::Printf(TEXT("%+.2f"), D.X) : FString::Printf(TEXT("%+.1f, %+.1f, %+.1f"), D.X, D.Y, D.Z));
			})
		]
		+ SHorizontalBox::Slot().FillWidth(0.1f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(FormatRange(Row.Min, Row.Max, Parameter)))]
		+ SHorizontalBox::Slot().FillWidth(0.06f)
		[
			SNew(SButton).Text(LOCTEXT("Reset", "Reset")).IsEnabled(this, &SACFCameraTuningPanel::CanEdit)
			.OnClicked(this, &SACFCameraTuningPanel::OnResetParameter, Parameter)
		];
}

TSharedRef<ITableRow> SACFCameraTuningPanel::MakeRequestRow(TSharedPtr<FACFCameraRequestInfo> Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(ACFTuningPanelView::SRequestRow, Owner, Item);
}

TSharedRef<ITableRow> SACFCameraTuningPanel::MakeNodeRow(TSharedPtr<FACFCameraTuningNodeInfo> Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(ACFTuningPanelView::SNodeRow, Owner, Item);
}

void SACFCameraTuningPanel::OnRequestSelected(TSharedPtr<FACFCameraRequestInfo> Item, ESelectInfo::Type Info)
{
	if (Item && Info != ESelectInfo::Direct && Item->Handle != Model->GetRequest())
	{
		Model->SelectRequest(Item->Handle);
		Refresh();
	}
}

void SACFCameraTuningPanel::OnNodeSelected(TSharedPtr<FACFCameraTuningNodeInfo> Item, ESelectInfo::Type Info)
{
	if (Item && Info != ESelectInfo::Direct)
	{
		Model->SelectNode(Item->NodeIndex, Item->NodeClass);
		Refresh();
	}
}

bool SACFCameraTuningPanel::CanStart() const
{
	return Model->GetRequest().IsValid() && Model->ResolveDirector() && !Model->IsTuning();
}

bool SACFCameraTuningPanel::CanEdit() const
{
	return Model->IsTuning() && !Model->IsShowingOriginal() && Model->GetNodeIndex() != INDEX_NONE;
}

FText SACFCameraTuningPanel::GetTargetText() const
{
	if (!Model->ResolveDirector())
	{
		return LOCTEXT("NoTarget", "Start PIE and select a PIE World and a local player. The editor world is never tuned.");
	}
	if (!Model->GetRequest().IsValid())
	{
		return LOCTEXT("NoRequest", "Select a camera request.");
	}
	const FACFCameraTuningStatus Status = Model->GetStatus();
	if (!Status.bTuning)
	{
		return FText::Format(LOCTEXT("NotTuningText", "Request {0} uses the preset asset {1}. Start Tuning to edit a copy (the asset is never changed)."),
			FText::FromString(Model->GetRequest().ToString()), FText::FromName(Status.OriginalPreset));
	}
	return FText::Format(LOCTEXT("TuningText", "TUNING request {0} on a copy of {1}: {2} value(s) differ from the asset{3}. Stop Tuning discards them."),
		FText::FromString(Model->GetRequest().ToString()), FText::FromName(Status.OriginalPreset), FText::AsNumber(Status.NumChangedValues),
		Model->IsShowingOriginal() ? LOCTEXT("ShowingOriginal", " (now showing the ORIGINAL values for comparison)") : FText::GetEmpty());
}

FReply SACFCameraTuningPanel::OnStart()
{
	Model->Start();
	Refresh();
	return FReply::Handled();
}

FReply SACFCameraTuningPanel::OnStop()
{
	Model->Stop();
	Refresh();
	return FReply::Handled();
}

FReply SACFCameraTuningPanel::OnResetAll()
{
	Model->ResetAll();
	Refresh();
	return FReply::Handled();
}

FReply SACFCameraTuningPanel::OnCompare()
{
	Model->ToggleCompare();
	Refresh();
	return FReply::Handled();
}

FReply SACFCameraTuningPanel::OnResetParameter(EACFCameraTuningParameter Parameter)
{
	Model->ResetParameter(Parameter);
	Refresh();
	return FReply::Handled();
}

void SACFCameraTuningPanel::OnComponentCommitted(double Value, ETextCommit::Type Commit, EACFCameraTuningParameter Parameter, int32 Axis)
{
	Model->SetComponent(Parameter, Axis, Value);
	Refresh();
}

#undef LOCTEXT_NAMESPACE
