#pragma once

#include "CoreMinimal.h"
#include "ACFCameraPresetValidation.h"
#include "ACFCameraTuning.h"
#include "UObject/SoftObjectPath.h"

class UACFCameraDirector;
class UACFCameraPreset;
class UPackage;

/**
 * Phase 7A-3: tuned values waiting to be applied to their preset assets (README 26 章).
 *
 * Method B: during PIE only a CHANGE SET is kept (what changed, from which value, to which value; never the tuning copy).
 * Writing the shared asset is a separate, explicit step that is refused while a play session runs, so a running request
 * never sees its asset change under it and the editor transaction never lands in the PIE undo window (UE resets the whole
 * undo history at PIE end when it holds a PIE object, and sets an undo barrier at PIE start).
 *
 *   Capture  (read-only runtime tuning API, a few times per second while the panel is open, and at PIE end)
 *   Check    (conflicts with the asset as it is now + Preset Validation on a transient candidate copy)
 *   Apply    (one FScopedTransaction: Modify / write / PostEditChangeProperty / MarkPackageDirty; one Undo reverts it)
 *   Save     (only the packages of applied changes, to disk; a failure is reported, the package stays dirty)
 *
 * Holds no object reference: assets by path, players and requests by labels / handles. Nothing here runs in game builds.
 */
class FACFCameraTuningChangeSet
{
public:
	/** One tunable value of one node of one asset, as tuned by one request of one player. */
	struct FChange
	{
		/** Who tuned it: "PIE 0 / Player 0 / ACFRequest#12 (DebugName)"; SourceKey identifies the request of that play session. */
		FString SourceKey;
		FString SourceLabel;
		/** The shared preset asset (the original, never the tuning copy). */
		FSoftObjectPath Preset;
		/** Node identity, re-checked before every write: index, class, object name, and the whole node layout. */
		int32 NodeIndex = INDEX_NONE;
		FName NodeClass;
		FName NodeName;
		TArray<FName> Layout;
		EACFCameraTuningParameter Parameter = EACFCameraTuningParameter::FollowDamping;
		FName Property;
		bool bScalar = false;
		/** The asset value when this request's tuning was first seen (the base the tuned value was made from). */
		FVector BaseValue = FVector::ZeroVector;
		/** The tuned value. */
		FVector Value = FVector::ZeroVector;
		/** Included in Apply (the user can leave a player's / request's changes out). */
		bool bInclude = true;

		bool IsChanged() const { return Value != BaseValue; }
	};

	/** What a change is, judged against the asset as it is NOW (so Undo / Redo / external edits are reflected). */
	enum class EState : uint8
	{
		Unchanged,  // tuned value == base: nothing to apply
		Pending,    // asset still at the base value: Apply would write it
		Applied,    // asset already at the tuned value (by Apply or otherwise)
		Conflict,   // asset / node / layout no longer what the change was made from, or another player tuned it differently
	};

	struct FStatus
	{
		EState State = EState::Unchanged;
		/** Why it conflicts (empty otherwise). */
		FString Reason;
		/** The asset's current value (when readable). */
		FVector AssetValue = FVector::ZeroVector;
		/** Applied and its package has unsaved changes. */
		bool bUnsaved = false;
	};

	/** Result of Check / Apply / Save. bOk = the operation did (or would do) what was asked. */
	struct FReport
	{
		bool bOk = false;
		FString Message;
		TArray<FString> Conflicts;
		/** Preset Validation of the candidate assets (the asset with the changes applied, on a transient copy). */
		TArray<FACFCameraPresetIssue> Issues;
		int32 NumErrors = 0;
		int32 NumWarnings = 0;
		int32 NumInfos = 0;
		/** Changes written (Apply) / packages saved (Save). */
		int32 NumDone = 0;
		/** Packages that still have unsaved changes after the operation. */
		TArray<FString> UnsavedPackages;
	};

	/**
	 * Records the tuned values of one tuned request (read-only: GetRequestNodes / GetTuningValue). New entries take the
	 * asset value of the moment as their base; existing ones only update the tuned value. Returns the number of changed
	 * values of that request.
	 */
	int32 Capture(UACFCameraDirector& Director, FACFCameraRequestHandle Handle, const FString& SourceKey, const FString& SourceLabel);

	/** Forgets one request's changes (Stop = discard) / everything. Never touches an asset. */
	void Discard(const FString& SourceKey);
	void Clear() { Changes.Reset(); }

	const TArray<FChange>& GetChanges() const { return Changes; }
	void SetIncluded(int32 Index, bool bInclude);
	FStatus GetStatus(int32 Index) const;
	int32 CountState(EState State, bool bIncludedOnly) const;
	bool HasUnsavedApplied() const;

	/** Conflicts + validation of what Apply would write. Changes nothing. */
	FReport Check() const;

	/**
	 * Writes the included Pending changes to their assets as one editor transaction. Refused (nothing written) while a play
	 * session runs, without pending changes, with any conflict, with a validation error, or with warnings unless
	 * bConfirmWarnings.
	 */
	FReport Apply(bool bConfirmWarnings);

	/** Saves the packages of applied changes that have unsaved edits (only those packages). */
	FReport Save();

	/** For the tests: whether a play session blocks Apply (GEditor->PlayWorld). */
	static bool IsPlaySessionRunning();

	/** Saves one package to its file; false with the reason (read-only file, not an asset, save error). */
	static bool SavePackage(UPackage& Package, FString& OutError);

private:
	FChange* Find(const FString& SourceKey, const FSoftObjectPath& Preset, int32 NodeIndex, EACFCameraTuningParameter Parameter);
	/** The live checks of one change against its asset (layout, node, parameter, value) and the other players' changes. */
	FStatus Evaluate(const FChange& Change) const;

	TArray<FChange> Changes;
};
