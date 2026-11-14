#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

class FDataValidationContext;
class UACFCameraNode;
class UACFCameraPreset;
enum class EDataValidationResult : uint8;

/**
 * Static checks of camera preset assets (Phase 6B / B-8), editor only.
 *
 * One implementation, used by UACFCameraPreset::IsDataValid (Data Validation: on save, "Validate Assets", the
 * commandlet), by UACFCameraSequence::IsDataValid (preset transitions between shots) and by the tests. It only reads the
 * preset: nothing is fixed, clamped or saved. Runtime values (NaN targets, invalid node output, ...) are the job of the
 * camera health checks (ACFCameraHealth.h, B-4), not of this file; nothing here runs in game builds.
 *
 * README "Preset の検証".
 */

/** Error: the configuration cannot work as authored. Warning: it works, but most likely not as intended. Info: allowed, worth knowing. */
enum class EACFCameraPresetIssueSeverity : uint8
{
	Info,
	Warning,
	Error,
};

/** One finding. Node / NodeIndex are empty for preset-level findings (blends, no nodes, transitions). */
struct ACTIONCAMERAFRAMEWORK_API FACFCameraPresetIssue
{
	EACFCameraPresetIssueSeverity Severity = EACFCameraPresetIssueSeverity::Warning;

	/** Stable identifier, e.g. "ACF.Param.NonFinite" (README lists them all). */
	FName Id;

	const UACFCameraPreset* Preset = nullptr;
	const UACFCameraNode* Node = nullptr;
	int32 NodeIndex = INDEX_NONE;

	/** Property path inside the node / preset for parameter findings ("PivotDamping.Time.X"), else empty. */
	FString Property;

	FText Message;
	FText Fix;

	/** "[Id] Node 1 (Orbital Follow): Message Fix" — the text Data Validation shows. */
	FText ToText() const;
};

namespace ACFCameraPresetValidation
{
	/** Structure (roles, stages, references), parameters and collision presence of one preset. Appends to OutIssues. */
	ACTIONCAMERAFRAMEWORK_API void ValidatePreset(const UACFCameraPreset& Preset, TArray<FACFCameraPresetIssue>& OutIssues);

	/**
	 * Findings that need to know that From blends into To (e.g. consecutive sequence shots). During a blend the PostBlend
	 * nodes of the incoming camera (To) run on the blended pose, from the first frame of the blend.
	 */
	ACTIONCAMERAFRAMEWORK_API void ValidateTransition(const UACFCameraPreset* From, const UACFCameraPreset* To, TArray<FACFCameraPresetIssue>& OutIssues);

	/** True when the preset has an enabled PostBlend node (Collision): the camera publishes an output guard constraint. */
	ACTIONCAMERAFRAMEWORK_API bool HasCollision(const UACFCameraPreset* Preset);

	/** Adds the issues to a Data Validation context (Error / Warning / Info). Invalid if any error, else Valid. */
	ACTIONCAMERAFRAMEWORK_API EDataValidationResult Report(TConstArrayView<FACFCameraPresetIssue> Issues, FDataValidationContext& Context);
}

#endif // WITH_EDITOR
