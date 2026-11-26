#include "SACFCameraTuningPanel.h"
#include "ACFCameraDirector.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "ACFCameraTuningPanel"

namespace ACFTuningPanelView
{
	template <typename ItemType, typename Predicate>
	static void SyncListSelection(SListView<ItemType>& List, const TArray<ItemType>& Items, Predicate MatchesSelection)
	{
		const ItemType* Selected = Items.FindByPredicate(MatchesSelection);
		if (Selected && !List.IsItemSelected(*Selected))
		{
			List.SetSelection(*Selected, ESelectInfo::Direct);
		}
		else if (!Selected && List.GetNumItemsSelected() > 0)
		{
			List.ClearSelection();
		}
	}

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

	enum class EParameterGroup : uint8
	{
		FollowAndPosition,
		Lens,
		Collision,
		Recentering,
		MovementAssistance,
		Count,
	};

	struct FParameterPresentation
	{
		EParameterGroup Group;
		FText Tooltip;
	};

	static FParameterPresentation GetParameterPresentation(EACFCameraTuningParameter Parameter, FName Property)
	{
		using EGroup = EParameterGroup;
		switch (Parameter)
		{
		case EACFCameraTuningParameter::FollowDamping:
			return { EGroup::FollowAndPosition, Property == TEXT("PivotDamping.Time")
				? LOCTEXT("PivotDampingTip", "How smoothly the camera pivot follows its target on each axis (seconds). Higher values make the pivot follow more slowly. Zero follows immediately. The node's Pivot Damping Space determines the axes.")
				: LOCTEXT("FollowDampingTip", "How smoothly the camera follows its target on each axis (seconds). Higher values make the camera follow more slowly. Zero follows immediately. The node's Damping Space determines the axes.") };
		case EACFCameraTuningParameter::FieldOfView:
			return { EGroup::Lens, LOCTEXT("FieldOfViewTip", "Camera field of view (degrees). Higher values fit more of the scene in the frame. Lower values make subjects appear larger.") };
		case EACFCameraTuningParameter::CameraOffset:
			// The same tuning parameter maps to different properties and axes depending on the node.
			if (Property == TEXT("ShoulderOffset"))
			{
				return { EGroup::FollowAndPosition, LOCTEXT("ShoulderOffsetTip", "Offset along the orbit camera's axes (cm). Positive X moves forward along the view, positive Y moves right, and positive Z moves up along the camera's up axis.") };
			}
			if (Property == TEXT("CameraOffset"))
			{
				return { EGroup::FollowAndPosition, LOCTEXT("ShotOffsetTip", "Offset from the shot anchor (cm). Positive X, Y and Z move forward, right and up in the node's Basis. With Line of Action, forward points from the follow target toward the look-at target.") };
			}
			if (Property == TEXT("Offset"))
			{
				return { EGroup::FollowAndPosition, LOCTEXT("FollowOffsetTip", "Offset from the follow target (cm). Positive X, Y and Z move forward, right and up in the node's Offset Space. Target Local rotates with the target. World uses fixed world axes.") };
			}
			return { EGroup::FollowAndPosition, LOCTEXT("CameraOffsetTip", "Position offset (cm). Positive X, Y and Z move forward, right and up along the axes defined by the camera node.") };
		case EACFCameraTuningParameter::CollisionRadius:
			return { EGroup::Collision, LOCTEXT("CollisionRadiusTip", "Radius of the camera collision probe (cm). A larger probe keeps the camera farther from walls and may pull it in sooner in tight spaces. Zero uses a line trace.") };
		case EACFCameraTuningParameter::RecenterDelay:
			return { EGroup::Recentering, LOCTEXT("RecenterDelayTip", "Wait before automatic recentering can begin (seconds). Higher values let the player look around longer before it starts. Movement Assistance also waits for its activation conditions.") };
		case EACFCameraTuningParameter::RecenterTime:
			return { EGroup::Recentering, LOCTEXT("RecenterTimeTip", "Time constant for the turn toward the recenter heading (seconds). Higher values ease back more slowly.") };
		case EACFCameraTuningParameter::AssistStartSpeed:
			return { EGroup::MovementAssistance, LOCTEXT("AssistStartSpeedTip", "Horizontal speed the target must reach to count as moving (cm/s). Raising it makes assistance less likely to start at low speeds.") };
		case EACFCameraTuningParameter::AssistStopSpeed:
			return { EGroup::MovementAssistance, LOCTEXT("AssistStopSpeedTip", "Horizontal speed below which a moving target counts as stopped (cm/s). Raising it ends assistance sooner when slowing down. The effective threshold is capped at Start Speed Threshold.") };
		case EACFCameraTuningParameter::AssistMovementDuration:
			return { EGroup::MovementAssistance, LOCTEXT("AssistMovementDurationTip", "How long the target must keep moving before assistance can start (seconds). Raising it filters out shorter bursts of movement. This timer runs alongside Recenter Delay.") };
		case EACFCameraTuningParameter::AssistBackwardExclusion:
			return { EGroup::MovementAssistance, LOCTEXT("AssistBackwardExclusionTip", "Angle around the direction opposite the camera's forward view where assistance stays off (degrees). A larger angle suppresses assistance for more headings toward the camera.") };
		case EACFCameraTuningParameter::AssistDeadZone:
			return { EGroup::MovementAssistance, LOCTEXT("AssistDeadZoneTip", "Yaw difference from the desired heading that assistance ignores (degrees). A larger angle lets more sideways deviation remain before the camera turns.") };
		case EACFCameraTuningParameter::AssistMaxYawSpeed:
			return { EGroup::MovementAssistance, LOCTEXT("AssistMaxYawSpeedTip", "Maximum horizontal turn speed that assistance can add (degrees per second). Raising it allows faster corrections when the camera is far from the desired heading.") };
		case EACFCameraTuningParameter::AssistInputThreshold:
			return { EGroup::MovementAssistance, LOCTEXT("AssistInputThresholdTip", "Turning speed after sensitivity at which input pauses assistance (degrees per second). Higher values allow stronger input before pausing. Zero makes any input pause it.") };
		default:
			return { EGroup::Count, FText::GetEmpty() };
		}
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
		// Phase 7B-2: Orbital Follow recenter / Movement Assistance.
		case EACFCameraTuningParameter::RecenterDelay: OutMin = 0.0; OutMax = 5.0; OutDelta = 0.05; break;
		case EACFCameraTuningParameter::RecenterTime: OutMin = 0.01; OutMax = 3.0; OutDelta = 0.01; break;
		case EACFCameraTuningParameter::AssistStartSpeed:
		case EACFCameraTuningParameter::AssistStopSpeed: OutMin = 0.0; OutMax = 1000.0; OutDelta = 5.0; break;
		case EACFCameraTuningParameter::AssistMovementDuration: OutMin = 0.0; OutMax = 2.0; OutDelta = 0.05; break;
		case EACFCameraTuningParameter::AssistBackwardExclusion:
		case EACFCameraTuningParameter::AssistDeadZone: OutMin = 0.0; OutMax = 180.0; OutDelta = 1.0; break;
		case EACFCameraTuningParameter::AssistMaxYawSpeed: OutMin = 1.0; OutMax = 360.0; OutDelta = 1.0; break;
		case EACFCameraTuningParameter::AssistInputThreshold: OutMin = 0.0; OutMax = 30.0; OutDelta = 0.5; break;
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
	Model = MakeShared<FACFCameraTuningPanelModel>();
	if (InArgs._DirectorForTesting.IsValid())
	{
		Model->SetDirectorForTesting(InArgs._DirectorForTesting.Get());
	}
	Diagnostics = MakeShared<FACFCameraDiagnosticsModel>(*Model);

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
				MakeTargetSelectors()

			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock).Text(this, &SACFCameraTuningPanel::GetTargetText).AutoWrapText(true)
			]
			// Operations on the selected request.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				MakeTuningActions()

			]
			// Four views share the target and tuning controls above.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(SSegmentedControl<int32>)
				.Value_Lambda([this]() { return static_cast<int32>(ActiveView); })
				.OnValueChanged_Lambda([this](int32 View) { SetActiveView(static_cast<EView>(View)); })
				+ SSegmentedControl<int32>::Slot(static_cast<int32>(EView::Tuning)).Text(LOCTEXT("ViewTuning", "Tuning"))
				+ SSegmentedControl<int32>::Slot(static_cast<int32>(EView::Profiler)).Text(LOCTEXT("ViewProfiler", "Profiler"))
				+ SSegmentedControl<int32>::Slot(static_cast<int32>(EView::Debugger)).Text(LOCTEXT("ViewDebugger", "Debugger"))
				+ SSegmentedControl<int32>::Slot(static_cast<int32>(EView::Composition)).Text(LOCTEXT("ViewComposition", "Composition"))
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return static_cast<int32>(ActiveView); })
				+ SWidgetSwitcher::Slot()
				[
					MakeTuningView()

				]
				+ SWidgetSwitcher::Slot()[ MakeProfilerView() ]
				+ SWidgetSwitcher::Slot()[ MakeDebuggerView() ]
				+ SWidgetSwitcher::Slot()[ MakeCompositionView() ]
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

TSharedRef<SWidget> SACFCameraTuningPanel::MakeTargetSelectors()
{
	return SNew(SHorizontalBox)
	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
	[
		SNew(STextBlock).Text(LOCTEXT("World", "PIE World"))
	]
	+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)
	[
		SAssignNew(WorldCombo, SComboBox<TSharedPtr<FACFCameraTuningPanelModel::FWorldEntry>>)
		.OptionsSource(&WorldItems)
		.IsEnabled_Lambda([this]() { return !WorldItems.IsEmpty(); })
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
		.IsEnabled_Lambda([this]() { return !PlayerItems.IsEmpty(); })
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
				return Model->GetWorld() && PlayerItems.IsEmpty()
					? LOCTEXT("NoPlayer", "No local players")
					: LOCTEXT("PickPlayer", "Select a local player");
			})
		]
	];
}

TSharedRef<SWidget> SACFCameraTuningPanel::MakeTuningActions()
{
	return SNew(SWrapBox)
	.UseAllottedSize(true)
	.InnerSlotPadding(FVector2D(4.0, 4.0))
	+ SWrapBox::Slot()
	[
		SNew(SButton)
		.Text(LOCTEXT("Start", "Start Tuning"))
		.ToolTipText(LOCTEXT("StartTip", "Edit a runtime copy of the selected request's preset for this play session."))
		.IsEnabled(this, &SACFCameraTuningPanel::CanStart)
		.OnClicked(this, &SACFCameraTuningPanel::OnStart)
	]
	+ SWrapBox::Slot()
	[
		SNew(SButton)
		.Text(LOCTEXT("Stop", "Stop Tuning (discard)"))
		.ToolTipText(LOCTEXT("StopTip", "Restore the preset values and discard the selected request's pending edits."))
		.IsEnabled_Lambda([this]() { return Model->IsTuning(); })
		.OnClicked(this, &SACFCameraTuningPanel::OnStop)
	]
	+ SWrapBox::Slot()
	[
		SNew(SButton)
		.Text(LOCTEXT("ResetAll", "Reset All"))
		.ToolTipText(LOCTEXT("ResetAllTip", "Restore all parameters of the selected request to their preset values and keep tuning active."))
		.IsEnabled_Lambda([this]() { return Model->IsTuning(); })
		.OnClicked(this, &SACFCameraTuningPanel::OnResetAll)
	]
	+ SWrapBox::Slot()
	[
		SNew(SButton)
		.Text_Lambda([this]() { return Model->IsShowingOriginal() ? LOCTEXT("ShowTuned", "Show Tuned Values") : LOCTEXT("ShowOriginal", "Show Original Values"); })
		.ToolTipText(LOCTEXT("CompareTip", "Switch between the preset values and your tuned values on the current camera. Parameter editing pauses while the preset values are shown."))
		.IsEnabled_Lambda([this]() { return Model->IsTuning(); })
		.OnClicked(this, &SACFCameraTuningPanel::OnCompare)
	];
}

TSharedRef<SWidget> SACFCameraTuningPanel::MakeTuningView()
{
	return SNew(SSplitter).Orientation(Orient_Vertical)
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
	];
}

EActiveTimerReturnType SACFCameraTuningPanel::OnTimer(double CurrentTime, float DeltaTime)
{
	Refresh();
	return EActiveTimerReturnType::Continue;
}

void SACFCameraTuningPanel::Refresh()
{
	Model->Validate();
	Diagnostics->Validate(); // Phase 7A-4: profiling follows the target (released when it changes)

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
		bRequestsChanged = A.Handle != B.Handle || A.Preset != B.Preset || A.DebugName != B.DebugName || A.bActive != B.bActive || A.bTuning != B.bTuning || A.Priority != B.Priority;
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
		}
	}
	ACFTuningPanelView::SyncListSelection(*RequestList, RequestItems, [this](const auto& Item)
	{
		return Item->Handle == Model->GetRequest();
	});

	TArray<FACFCameraTuningNodeInfo> Nodes;
	Model->GetNodes(Nodes);
	bool bNodesChanged = Nodes.Num() != NodeItems.Num();
	for (int32 I = 0; !bNodesChanged && I < Nodes.Num(); ++I)
	{
		bNodesChanged = Nodes[I].NodeIndex != NodeItems[I]->NodeIndex || Nodes[I].NodeClass != NodeItems[I]->NodeClass || Nodes[I].bEnabled != NodeItems[I]->bEnabled
			|| Nodes[I].Stage != NodeItems[I]->Stage || Nodes[I].Parameters != NodeItems[I]->Parameters;
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
		}
	}
	// Two requests can expose identical node rows. Sync selection even when the list did not need rebuilding.
	ACFTuningPanelView::SyncListSelection(*NodeList, NodeItems, [this](const auto& Item)
	{
		return Item->NodeIndex == Model->GetNodeIndex();
	});

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
	RefreshDiagnostics(); // Phase 7A-4: only the visible diagnostics view
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
				.Text(LOCTEXT("ChangesTitle", "Preset Changes"))
				.ToolTipText(LOCTEXT("ChangesTitleTip", "Changes stay listed after PIE ends. Use Apply to Asset, then Save Asset to write them to disk."))
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
				SNew(SWrapBox)
				.UseAllottedSize(true)
				.InnerSlotPadding(FVector2D(4.0, 4.0))
				+ SWrapBox::Slot()
				[
					SNew(SButton).Text(LOCTEXT("Check", "Check"))
					.ToolTipText(LOCTEXT("CheckTip", "Conflicts with the assets as they are now, and Preset Validation of the result. Changes nothing."))
					.IsEnabled_Lambda([this]() { return Model->GetChangeSet().CountState(FACFCameraTuningChangeSet::EState::Pending, true) > 0
						|| Model->GetChangeSet().CountState(FACFCameraTuningChangeSet::EState::Conflict, true) > 0; })
					.OnClicked(this, &SACFCameraTuningPanel::OnCheckChanges)
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton).Text(LOCTEXT("Apply", "Apply to Asset"))
					.ToolTipText(LOCTEXT("ApplyTip", "Writes the included pending changes to their preset assets in memory, as one undoable step (Edit > Undo). Not available during PIE. Does not save."))
					.IsEnabled_Lambda([this]() { return CanApply(); })
					.OnClicked(this, &SACFCameraTuningPanel::OnApplyChanges)
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton).Text(LOCTEXT("Save", "Save Asset"))
					.ToolTipText(LOCTEXT("SaveTip", "Saves only the packages of applied changes to disk. Other dirty packages are not saved."))
					.IsEnabled_Lambda([this]() { return Model->GetChangeSet().HasUnsavedApplied(); })
					.OnClicked(this, &SACFCameraTuningPanel::OnSaveChanges)
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton).Text(LOCTEXT("ClearChanges", "Clear List"))
					.ToolTipText(LOCTEXT("ClearChangesTip", "Forgets the listed changes. Never touches an asset (applied values stay applied)."))
					.IsEnabled_Lambda([this]() { return !ChangesShape.IsEmpty(); })
					.OnClicked(this, &SACFCameraTuningPanel::OnClearChanges)
				]
				+ SWrapBox::Slot().VAlign(VAlign_Center)
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
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(this, &SACFCameraTuningPanel::GetChangeDescription, Index)
				.ToolTipText(this, &SACFCameraTuningPanel::GetChangeDescription, Index)
			]
			+ SHorizontalBox::Slot().FillWidth(0.3f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).AutoWrapText(true).Text_Lambda([this, Index]() { return GetChangeStateText(Index); })
			]
		];
	}
}

FText SACFCameraTuningPanel::GetChangeDescription(int32 Index) const
{
	const TArray<FACFCameraTuningChangeSet::FChange>& Changes = Model->GetChangeSet().GetChanges();
	if (!Changes.IsValidIndex(Index))
	{
		return FText::GetEmpty();
	}
	const FACFCameraTuningChangeSet::FChange& Change = Changes[Index];
	const auto Format = [&Change](const FVector& Value)
	{
		return Change.bScalar ? FString::Printf(TEXT("%.3f"), Value.X)
			: FString::Printf(TEXT("(%.3f, %.3f, %.3f)"), Value.X, Value.Y, Value.Z);
	};
	return FText::Format(LOCTEXT("ChangeDescription", "{0} → {1}\nNode {2} ({3}) · {4} · {5} → {6}"),
		FText::FromString(Change.SourceLabel), FText::FromString(Change.Preset.GetLongPackageName()),
		FText::AsNumber(Change.NodeIndex), FText::FromString(ACFTuningPanelView::NodeName(Change.NodeClass)),
		FText::FromString(ACFCameraTuning::GetParameterName(Change.Parameter)),
		FText::FromString(Format(Change.BaseValue)), FText::FromString(Format(Change.Value)));
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
			+ SHorizontalBox::Slot().FillWidth(0.18f)[SNew(STextBlock).Text(LOCTEXT("HOriginal", "Original (asset)")).ToolTipText(LOCTEXT("HOriginalTip", "Value from the preset. The row Reset restores this value in the runtime copy."))]
			+ SHorizontalBox::Slot().FillWidth(0.34f)[SNew(STextBlock).Text(LOCTEXT("HCurrent", "Current"))]
			+ SHorizontalBox::Slot().FillWidth(0.14f)[SNew(STextBlock).Text(LOCTEXT("HDiff", "Difference")).ToolTipText(LOCTEXT("HDiffTip", "Current minus original. Vector differences follow the X, Y, Z order."))]
			+ SHorizontalBox::Slot().FillWidth(0.1f)[SNew(STextBlock).Text(LOCTEXT("HRange", "Valid range")).ToolTipText(LOCTEXT("HRangeTip", "Accepted values. Square brackets include the boundary. Parentheses exclude it."))]
			+ SHorizontalBox::Slot().FillWidth(0.06f)[SNullWidget::NullWidget]
		];
	}
	using namespace ACFTuningPanelView;
	const FText GroupTitles[] =
	{
		LOCTEXT("GroupFollowAndPosition", "Follow & Position"),
		LOCTEXT("GroupLens", "Lens"),
		LOCTEXT("GroupCollision", "Collision"),
		LOCTEXT("GroupRecentering", "Recentering"),
		LOCTEXT("GroupMovementAssistance", "Movement Assistance"),
		LOCTEXT("GroupOther", "Other"),
	};
	static_assert(UE_ARRAY_COUNT(GroupTitles) == static_cast<int32>(EParameterGroup::Count) + 1);
	struct FGroupedRow
	{
		int32 RowIndex;
		FText Tooltip;
	};
	TArray<FGroupedRow> GroupRows[UE_ARRAY_COUNT(GroupTitles)];
	// Keep the row index from the model so live value bindings still address the original row.
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FACFCameraTuningPanelModel::FParameterRow& Row = Rows[Index];
		if (Row.bAvailable || Model->GetNodeIndex() == INDEX_NONE || !Row.Reason.IsEmpty())
		{
			FParameterPresentation Presentation = GetParameterPresentation(Row.Parameter, Row.Value.Property);
			GroupRows[static_cast<int32>(Presentation.Group)].Add({ Index, MoveTemp(Presentation.Tooltip) });
		}
	}

	for (int32 GroupIndex = 0; GroupIndex < UE_ARRAY_COUNT(GroupTitles); ++GroupIndex)
	{
		const TArray<FGroupedRow>& Group = GroupRows[GroupIndex];
		if (Group.IsEmpty())
		{
			continue;
		}
		ParameterBox->AddSlot().AutoHeight().Padding(2.f, 5.f, 2.f, 1.f)
		[
			SNew(STextBlock).Text(GroupTitles[GroupIndex]).Font(FAppStyle::GetFontStyle("BoldFont"))
		];
		for (const FGroupedRow& Row : Group)
		{
			ParameterBox->AddSlot().AutoHeight().Padding(2.f)[MakeParameterRow(Row.RowIndex, Row.Tooltip)];
		}
	}
}

TSharedRef<SWidget> SACFCameraTuningPanel::MakeParameterRow(int32 RowIndex, const FText& Tooltip)
{
	using namespace ACFTuningPanelView;
	const FACFCameraTuningPanelModel::FParameterRow& Row = Rows[RowIndex];
	const EACFCameraTuningParameter Parameter = Row.Parameter;
	const FText Name = FText::FromString(ACFCameraTuning::GetParameterName(Parameter));
	if (!Row.bAvailable)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.18f)[SNew(STextBlock).Text(Name).ToolTipText(Tooltip)]
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
		+ SHorizontalBox::Slot().FillWidth(0.18f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Name).ToolTipText(Tooltip)]
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
			SNew(SButton).Text(LOCTEXT("Reset", "Reset")).ToolTipText(LOCTEXT("ResetParameterTip", "Restore this parameter to its preset value for the selected request.")).IsEnabled(this, &SACFCameraTuningPanel::CanEdit)
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
		return LOCTEXT("NoTarget", "Start Play In Editor (PIE), then select a world and local player.");
	}
	if (!Model->GetRequest().IsValid())
	{
		return LOCTEXT("NoRequest", "Select a camera request.");
	}
	const FACFCameraTuningStatus Status = Model->GetStatus();
	if (!Status.bTuning)
	{
		return FText::Format(LOCTEXT("NotTuningText", "Request {0} uses {1}. Select Start Tuning to edit a runtime copy."),
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
	Diagnostics->NotifyTuningChanged();
	Refresh();
	return FReply::Handled();
}

FReply SACFCameraTuningPanel::OnCompare()
{
	Model->ToggleCompare();
	Diagnostics->NotifyTuningChanged();
	Refresh();
	return FReply::Handled();
}

FReply SACFCameraTuningPanel::OnResetParameter(EACFCameraTuningParameter Parameter)
{
	Model->ResetParameter(Parameter);
	Diagnostics->NotifyTuningChanged();
	Refresh();
	return FReply::Handled();
}

void SACFCameraTuningPanel::OnComponentCommitted(double Value, ETextCommit::Type Commit, EACFCameraTuningParameter Parameter, int32 Axis)
{
	Model->SetComponent(Parameter, Axis, Value);
	Diagnostics->NotifyTuningChanged(); // Phase 7A-4: a profile period from here on mixes before / after
	Refresh();
}

#undef LOCTEXT_NAMESPACE
