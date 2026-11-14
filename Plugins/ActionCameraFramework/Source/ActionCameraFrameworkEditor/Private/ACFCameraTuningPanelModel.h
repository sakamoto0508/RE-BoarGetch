#pragma once

#include "CoreMinimal.h"
#include "ACFCameraTuning.h"
#include "ACFCameraTuningChangeSet.h"
#include "Engine/EngineTypes.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UACFCameraDirector;
class ULocalPlayer;
class UWorld;

/**
 * Phase 7A-2: what the tuning panel shows and does, without any Slate (SACFCameraTuningPanel is only the view; the tests
 * drive this class). It only OBSERVES the game and calls the runtime tuning API (UACFCameraDirector Start / Set / Reset /
 * Stop, ACFCameraTuning): it never writes a preset, never creates requests / rigs, never changes the pipeline.
 *
 * The target is (PIE World, local player, request handle, node index + class). Everything is held weakly / by value and
 * re-resolved on every use, so a stale target (PIE ended, player removed, request released) is detected, never touched.
 */
class FACFCameraTuningPanelModel
{
public:
	struct FWorldEntry
	{
		TWeakObjectPtr<UWorld> World;
		FString Label;
	};

	struct FPlayerEntry
	{
		TWeakObjectPtr<ULocalPlayer> Player;
		int32 Index = INDEX_NONE;
		FString Label;
	};

	/** One parameter row of the selected node (or why the request cannot tune it). */
	struct FParameterRow
	{
		EACFCameraTuningParameter Parameter = EACFCameraTuningParameter::FollowDamping;
		bool bAvailable = false;
		/** Empty when available. */
		FString Reason;
		FACFCameraTuningValue Value;
		double Min = 0.0;
		double Max = 0.0;
	};

	/** Only PIE worlds are tunable from the editor (the editor world, previews and inactive worlds are not). */
	static bool IsTunableWorldType(EWorldType::Type Type) { return Type == EWorldType::PIE; }
	static void GetPIEWorlds(TArray<FWorldEntry>& Out);
	static void GetPlayers(UWorld* World, TArray<FPlayerEntry>& Out);

	// --- Selection (each level clears the levels below) ----------------------------------------------------------
	void SelectWorld(UWorld* World);
	void SelectPlayer(ULocalPlayer* Player);
	void SelectRequest(FACFCameraRequestHandle Handle);
	void SelectNode(int32 NodeIndex, FName NodeClass);

	UWorld* GetWorld() const { return World.Get(); }
	ULocalPlayer* GetPlayer() const { return Player.Get(); }
	FACFCameraRequestHandle GetRequest() const { return Request; }
	int32 GetNodeIndex() const { return NodeIndex; }

	/**
	 * Drops what is no longer valid (World gone or not PIE any more, player removed, request ended, node class changed).
	 * Returns true if the selection changed. Cheap: called by the panel a few times per second while it is open.
	 */
	bool Validate();

	/** The selected player's director, or null (no PIE, stale player). Tests can bind a director directly instead. */
	UACFCameraDirector* ResolveDirector() const;
	void SetDirectorForTesting(UACFCameraDirector* Director);

	// --- Queries -------------------------------------------------------------------------------------------------
	void GetRequests(TArray<FACFCameraRequestInfo>& Out) const;
	void GetNodes(TArray<FACFCameraTuningNodeInfo>& Out) const;
	/** Rows of the selected node's parameters, then the parameters the request cannot tune with the reason. */
	void GetParameterRows(TArray<FParameterRow>& Out) const;
	FACFCameraTuningStatus GetStatus() const;
	bool IsTuning() const { return GetStatus().bTuning; }

	// --- Operations (runtime tuning API; the message says what happened) ------------------------------------------
	EACFCameraTuningResult Start();
	EACFCameraTuningResult Stop();
	/** Writes the selected node's parameter. Invalid values are rejected by the runtime and change nothing. */
	EACFCameraTuningResult SetValue(EACFCameraTuningParameter Parameter, const FVector& Value);
	EACFCameraTuningResult SetComponent(EACFCameraTuningParameter Parameter, int32 Axis, double Value);
	EACFCameraTuningResult ResetParameter(EACFCameraTuningParameter Parameter);
	EACFCameraTuningResult ResetAll();

	/**
	 * A / B comparison without a second request or rig: "Show Original" remembers the tuned values of every node and
	 * resets them (the request keeps its rig state), "Show Tuned" writes them back. Editing is blocked while showing the
	 * original. Ends by itself when the target changes or tuning stops.
	 */
	EACFCameraTuningResult ToggleCompare();
	bool IsShowingOriginal() const { return bShowingOriginal; }

	const FString& GetLastMessage() const { return LastMessage; }

	// --- Phase 7A-3: changes kept for the preset assets (ACFCameraTuningChangeSet.h, README 26 章) -------------------
	FACFCameraTuningPanelModel();
	~FACFCameraTuningPanelModel();
	FACFCameraTuningPanelModel(const FACFCameraTuningPanelModel&) = delete;
	FACFCameraTuningPanelModel& operator=(const FACFCameraTuningPanelModel&) = delete;

	/**
	 * Records the tuned values of every tuned request of every PIE player (or of the test director). Read-only; the panel
	 * calls it with its refresh, and it runs once more just before PIE ends (the tuning copies are gone afterwards).
	 */
	int32 CaptureAll();
	FACFCameraTuningChangeSet& GetChangeSet() { return ChangeSet; }
	const FACFCameraTuningChangeSet& GetChangeSet() const { return ChangeSet; }
	/** Check / Apply / Save with the result as the last message. */
	FACFCameraTuningChangeSet::FReport CheckChanges();
	FACFCameraTuningChangeSet::FReport ApplyChanges(bool bConfirmWarnings);
	FACFCameraTuningChangeSet::FReport SaveChanges();
	/** Source key / label of a request of the selected player (what the change set records it under). */
	FString MakeSourceKey(FACFCameraRequestHandle Handle) const;
	FString MakeSourceLabel(const FACFCameraRequestInfo& Info) const;

private:
	void OnPrePIEEnded(bool bIsSimulating);
	FDelegateHandle PrePIEEndedHandle;
	FACFCameraTuningChangeSet ChangeSet;

	EACFCameraTuningResult Report(EACFCameraTuningResult Result, const FString& What);
	bool NodeMatches(UACFCameraDirector& Director) const;
	void ClearCompare();

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ULocalPlayer> Player;
	FACFCameraRequestHandle Request;
	int32 NodeIndex = INDEX_NONE;
	FName NodeClass;
	TWeakObjectPtr<UACFCameraDirector> TestDirector;

	struct FStashed
	{
		int32 NodeIndex = INDEX_NONE;
		EACFCameraTuningParameter Parameter = EACFCameraTuningParameter::FollowDamping;
		FVector Value = FVector::ZeroVector;
	};
	TArray<FStashed> CompareStash;
	bool bShowingOriginal = false;
	FString LastMessage;
};
