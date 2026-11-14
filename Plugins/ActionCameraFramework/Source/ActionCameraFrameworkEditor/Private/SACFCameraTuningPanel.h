#pragma once

#include "CoreMinimal.h"
#include "ACFCameraTuningPanelModel.h"
#include "ACFCameraDiagnosticsModel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

template <typename OptionType> class SComboBox;
class SVerticalBox;

/**
 * Phase 7A-2: the "ACF Camera Tuning" editor tab (Window > Developer Tools > Debug). A thin view over
 * FACFCameraTuningPanelModel: target selection (PIE World, local player, request), the request's nodes, and the tunable
 * parameters of the selected node (original / current / difference / valid range / reset), plus Start / Stop / Reset All /
 * Compare. It polls the model a few times per second while open (never per camera frame) and writes nothing but through
 * the runtime tuning API. Room is left below the parameters for the 7A-4 debugger.
 */
class SACFCameraTuningPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SACFCameraTuningPanel) {}
		/** Tests bind a director directly (no PIE needed). */
		SLATE_ARGUMENT(TWeakObjectPtr<UACFCameraDirector>, DirectorForTesting)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Re-reads the model (also what the timer does). Public for the tests. */
	void Refresh();

	FACFCameraTuningPanelModel& GetModel() { return *Model; }
	int32 GetNumListedRequests() const { return RequestItems.Num(); }
	int32 GetNumListedNodes() const { return NodeItems.Num(); }
	int32 GetNumParameterRows() const { return Rows.Num(); }

	// Button / editor handlers (the tests call them as a click would).
	FReply OnStart();
	FReply OnStop();
	FReply OnResetAll();
	FReply OnCompare();
	FReply OnResetParameter(EACFCameraTuningParameter Parameter);
	void OnComponentCommitted(double Value, ETextCommit::Type Commit, EACFCameraTuningParameter Parameter, int32 Axis);
	void OnRequestSelected(TSharedPtr<FACFCameraRequestInfo> Item, ESelectInfo::Type Info);
	void OnNodeSelected(TSharedPtr<FACFCameraTuningNodeInfo> Item, ESelectInfo::Type Info);

	// Phase 7A-3: changes for the preset assets (README 26 章).
	FReply OnCheckChanges();
	FReply OnApplyChanges();
	FReply OnSaveChanges();
	FReply OnClearChanges();
	void SetConfirmWarnings(bool bConfirm) { bConfirmWarnings = bConfirm; }
	bool CanApply() const;
	int32 GetNumChangeRows() const { return NumChangeRows; }
	const FString& GetValidationText() const { return ValidationText; }
	FText GetChangeStateText(int32 ChangeIndex) const;
	FText GetChangesSummary() const;

	// Phase 7A-4: Profiler / Debugger / Composition views of the same target (README 27 章).
	enum class EView : uint8 { Tuning, Profiler, Debugger, Composition };
	void SetActiveView(EView View);
	EView GetActiveView() const { return ActiveView; }
	FACFCameraDiagnosticsModel& GetDiagnostics() { return *Diagnostics; }
	FReply OnStartProfiling();
	FReply OnStopProfiling();
	FReply OnResetStatistics();
	FReply OnCaptureBaseline();
	/** The text the visible view shows (tests read what the user would read). */
	const FString& GetProfilerText() const { return ProfilerText; }
	const FString& GetDebuggerText() const { return DebuggerText; }
	const FString& GetCompositionText() const { return CompositionText; }
	/** Diagnostics refreshes done (they run only for the visible view; tests check that). */
	int32 GetNumDiagnosticsRefreshes() const { return NumDiagnosticsRefreshes; }
	/** Profile statistics actually recomputed (at most once per second, and after a button). */
	int32 GetNumProfileComputations() const { return NumProfileComputations; }

private:
	TSharedRef<SWidget> MakeProfilerView();
	TSharedRef<SWidget> MakeDebuggerView();
	TSharedRef<SWidget> MakeCompositionView();
	void RefreshDiagnostics();
	EView ActiveView = EView::Tuning;
	FString ProfilerText;
	FString DebuggerText;
	FString CompositionText;
	int32 NumDiagnosticsRefreshes = 0;
	double LastProfileUpdate = 0.0;
	bool bProfileDirty = true;
	int32 NumProfileComputations = 0;

	TSharedRef<SWidget> MakeChangesSection();
	void RebuildChangesBox();
	void ShowReport(const FACFCameraTuningChangeSet::FReport& Report);
	TSharedPtr<SVerticalBox> ChangesBox;
	FString ChangesShape;
	int32 NumChangeRows = 0;
	FString ValidationText;
	bool bConfirmWarnings = false;

	EActiveTimerReturnType OnTimer(double CurrentTime, float DeltaTime);
	void RebuildParameterBox();
	TSharedRef<SWidget> MakeParameterRow(int32 RowIndex);
	TSharedRef<ITableRow> MakeRequestRow(TSharedPtr<FACFCameraRequestInfo> Item, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> MakeNodeRow(TSharedPtr<FACFCameraTuningNodeInfo> Item, const TSharedRef<STableViewBase>& Owner);
	bool CanStart() const;
	bool CanEdit() const;
	FText GetTargetText() const;

	TSharedPtr<FACFCameraTuningPanelModel> Model;
	/** Declared after Model: destroyed first (it refers to the model and releases the profiler it forced). */
	TSharedPtr<FACFCameraDiagnosticsModel> Diagnostics;

	TArray<TSharedPtr<FACFCameraTuningPanelModel::FWorldEntry>> WorldItems;
	TArray<TSharedPtr<FACFCameraTuningPanelModel::FPlayerEntry>> PlayerItems;
	TArray<TSharedPtr<FACFCameraRequestInfo>> RequestItems;
	TArray<TSharedPtr<FACFCameraTuningNodeInfo>> NodeItems;
	TArray<FACFCameraTuningPanelModel::FParameterRow> Rows;
	/** Shape of the parameter box (node / parameters / availability): rebuilt only when it changes. */
	FString RowsShape;

	TSharedPtr<SComboBox<TSharedPtr<FACFCameraTuningPanelModel::FWorldEntry>>> WorldCombo;
	TSharedPtr<SComboBox<TSharedPtr<FACFCameraTuningPanelModel::FPlayerEntry>>> PlayerCombo;
	TSharedPtr<SListView<TSharedPtr<FACFCameraRequestInfo>>> RequestList;
	TSharedPtr<SListView<TSharedPtr<FACFCameraTuningNodeInfo>>> NodeList;
	TSharedPtr<SVerticalBox> ParameterBox;
};
