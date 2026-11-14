#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "ACFCameraPresetValidation.h"

class FDataValidationContext;
class UACFCameraZoneComponent;
enum class EDataValidationResult : uint8;

/**
 * Phase 7C-3A: static checks of a Camera Zone (README 32 章 "Editor での検証"), editor only. One implementation used by
 * UACFCameraZoneComponent::IsDataValid (Data Validation), UACFCameraZoneComponent::CheckForErrors (Map Check) and the tests.
 * It only reads the zone. Severities as for presets: Error = cannot work as authored, Warning = works but most likely not
 * as intended, Info = allowed, worth knowing. Overlapping zones whose order the resolver decides (priority, then the smaller
 * volume) are not errors; only an exact tie (same priority and volume, decided by the path name) is a warning.
 */
struct ACTIONCAMERAFRAMEWORKLEVEL_API FACFCameraZoneIssue
{
	EACFCameraPresetIssueSeverity Severity = EACFCameraPresetIssueSeverity::Warning;
	/** Stable identifier, e.g. "ACF.Zone.NoShape" (README lists them). */
	FName Id;
	FText Message;

	/** "[Id] Message". */
	FText ToText() const;
};

namespace ACFCameraZoneValidation
{
	/** The zone alone: shape, preset, blends, delays, margin, rotation handoff. Appends to Out. */
	ACTIONCAMERAFRAMEWORKLEVEL_API void ValidateZone(const UACFCameraZoneComponent& Zone, TArray<FACFCameraZoneIssue>& Out);

	/** The zone against the other zones of its World (overlaps, ties). Appends to Out. */
	ACTIONCAMERAFRAMEWORKLEVEL_API void ValidateOverlaps(const UACFCameraZoneComponent& Zone, TArray<FACFCameraZoneIssue>& Out);

	/** Both. */
	ACTIONCAMERAFRAMEWORKLEVEL_API void Validate(const UACFCameraZoneComponent& Zone, TArray<FACFCameraZoneIssue>& Out);

	/** Data Validation: errors / warnings / infos into the context. Invalid if any error, else Valid. */
	ACTIONCAMERAFRAMEWORKLEVEL_API EDataValidationResult Report(const UACFCameraZoneComponent& Zone, TConstArrayView<FACFCameraZoneIssue> Issues, FDataValidationContext& Context);

	/** Map Check: one message per issue, with a link to the zone's actor. */
	ACTIONCAMERAFRAMEWORKLEVEL_API void ReportMapCheck(const UACFCameraZoneComponent& Zone, TConstArrayView<FACFCameraZoneIssue> Issues);

	/** Highest severity in Issues (Info if empty). */
	ACTIONCAMERAFRAMEWORKLEVEL_API EACFCameraPresetIssueSeverity MaxSeverity(TConstArrayView<FACFCameraZoneIssue> Issues);
}

#endif // WITH_EDITOR
