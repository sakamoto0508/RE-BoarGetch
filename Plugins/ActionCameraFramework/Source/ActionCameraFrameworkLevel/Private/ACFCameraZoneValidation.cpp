#include "ACFCameraZoneValidation.h"

#if WITH_EDITOR

#include "ACFCameraPreset.h"
#include "ACFCameraZoneComponent.h"
#include "ACFCameraZoneResolver.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/DataValidation.h"
#include "Misc/UObjectToken.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "ACFCameraZoneValidation"

FText FACFCameraZoneIssue::ToText() const
{
	return FText::Format(LOCTEXT("IssueFormat", "[{0}] {1}"), FText::FromName(Id), Message);
}

namespace ACFCameraZoneValidationImpl
{
	using ESeverity = EACFCameraPresetIssueSeverity;

	/** Seconds above which a delay is most likely a typo (a player would wonder why nothing happens). */
	constexpr float LongDelay = 10.f;

	static void Add(TArray<FACFCameraZoneIssue>& Out, ESeverity Severity, const TCHAR* Id, FText Message)
	{
		FACFCameraZoneIssue& Issue = Out.AddDefaulted_GetRef();
		Issue.Severity = Severity;
		Issue.Id = FName(Id);
		Issue.Message = MoveTemp(Message);
	}

	static FText ZoneName(const UACFCameraZoneComponent& Zone)
	{
		return FText::FromString(Zone.GetOwner() ? Zone.GetOwner()->GetActorNameOrLabel() : Zone.GetName());
	}

	static bool UsesControlRotation(const UACFCameraPreset* Preset)
	{
		if (!Preset)
		{
			return false;
		}
		for (const UACFCameraNode* Node : Preset->Nodes)
		{
			const UACFCameraNode_OrbitalFollow* Orbit = Cast<UACFCameraNode_OrbitalFollow>(Node);
			if (Orbit && Orbit->bEnabled && Orbit->InputSource == EACFCameraOrbitInputSource::ControlRotation)
			{
				return true;
			}
		}
		return false;
	}

	static void ValidateBlend(TArray<FACFCameraZoneIssue>& Out, const FACFCameraBlendSettings& Blend, const FText& Name)
	{
		if (Blend.Curve == EACFCameraBlendCurve::Cut)
		{
			return;
		}
		if (!FMath::IsFinite(Blend.Duration))
		{
			Add(Out, ESeverity::Error, TEXT("ACF.Zone.Blend.NonFinite"), FText::Format(LOCTEXT("BlendNonFinite", "{0}: the duration is not a finite number (the camera would cut and report a health issue). Enter seconds >= 0."), Name));
		}
		else if (Blend.Duration < 0.f)
		{
			Add(Out, ESeverity::Warning, TEXT("ACF.Zone.Blend.Negative"), FText::Format(LOCTEXT("BlendNegative", "{0}: a negative duration ({1} s) is a cut. Use the Cut curve or a duration >= 0."), Name, FText::AsNumber(Blend.Duration)));
		}
		if (Blend.Curve == EACFCameraBlendCurve::Custom && !Blend.CustomCurve)
		{
			Add(Out, ESeverity::Warning, TEXT("ACF.Zone.Blend.MissingCurve"), FText::Format(LOCTEXT("BlendMissingCurve", "{0}: the Custom curve has no Curve Float asset (the blend is linear)."), Name));
		}
		if (Blend.Curve != EACFCameraBlendCurve::Custom && (!FMath::IsFinite(Blend.Exponent) || Blend.Exponent <= 0.f))
		{
			Add(Out, ESeverity::Error, TEXT("ACF.Zone.Blend.Exponent"), FText::Format(LOCTEXT("BlendExponent", "{0}: the exponent must be a number > 0."), Name));
		}
	}

	static void ValidateSeconds(TArray<FACFCameraZoneIssue>& Out, float Value, const TCHAR* Property)
	{
		const FText Name = FText::FromString(Property);
		if (!FMath::IsFinite(Value) || Value < 0.f)
		{
			Add(Out, ESeverity::Error, TEXT("ACF.Zone.Delay.Invalid"), FText::Format(LOCTEXT("DelayInvalid", "{0} must be a number >= 0 (seconds); it is treated as 0."), Name));
		}
		else if (Value > LongDelay)
		{
			Add(Out, ESeverity::Warning, TEXT("ACF.Zone.Delay.Long"), FText::Format(LOCTEXT("DelayLong", "{0} is {1} s: the player has to stay that long before the camera changes. Intended?"), Name, FText::AsNumber(Value)));
		}
	}

	/** World bounds of the zone's shape (false without one). */
	static bool ShapeBounds(const UACFCameraZoneComponent& Zone, FBox& Out)
	{
		const UShapeComponent* Shape = Zone.GetShapeComponent();
		if (!Shape)
		{
			return false;
		}
		Out = Shape->CalcBounds(Shape->GetComponentTransform()).GetBox();
		return Out.IsValid != 0;
	}
}

void ACFCameraZoneValidation::ValidateZone(const UACFCameraZoneComponent& Zone, TArray<FACFCameraZoneIssue>& Out)
{
	using namespace ACFCameraZoneValidationImpl;
	const AActor* Owner = Zone.GetOwner();
	if (Zone.IsTemplate() || !Owner)
	{
		return; // a Blueprint's component template: the shape and the other zones exist only in a level
	}

	// Shape.
	const UShapeComponent* Shape = Zone.GetShapeComponent();
	if (!Shape)
	{
		const UActorComponent* Picked = Zone.Shape.GetComponent(const_cast<AActor*>(Owner));
		Add(Out, ESeverity::Error, Picked ? TEXT("ACF.Zone.WrongShape") : TEXT("ACF.Zone.NoShape"), Picked
			? FText::Format(LOCTEXT("WrongShape", "Shape picks {0}, which is not a Box or Sphere component: the zone is never selected."), FText::FromString(Picked->GetName()))
			: LOCTEXT("NoShape", "The actor has no Box or Sphere component: the zone is never selected. Add one (Camera Zone actors have a Box)."));
	}
	else if (const UBoxComponent* Box = Cast<UBoxComponent>(Shape))
	{
		const FVector Scale = Box->GetComponentTransform().GetScale3D().GetAbs();
		const FVector Extent = Box->GetUnscaledBoxExtent();
		if (Extent.GetMin() <= 0.0 || Scale.GetMin() <= UE_SMALL_NUMBER)
		{
			Add(Out, ESeverity::Error, TEXT("ACF.Zone.EmptyShape"), FText::Format(LOCTEXT("EmptyBox", "The box has no volume (extent {0}, scale {1}): the zone is never selected."),
				FText::FromString(Extent.ToCompactString()), FText::FromString(Scale.ToCompactString())));
		}
	}
	else if (const USphereComponent* Sphere = Cast<USphereComponent>(Shape))
	{
		const FVector Scale = Sphere->GetComponentTransform().GetScale3D().GetAbs();
		if (Sphere->GetUnscaledSphereRadius() <= 0.f || Scale.GetMin() <= UE_SMALL_NUMBER)
		{
			Add(Out, ESeverity::Error, TEXT("ACF.Zone.EmptyShape"), LOCTEXT("EmptySphere", "The sphere has no volume (radius or scale 0): the zone is never selected."));
		}
		else if (!FMath::IsNearlyEqual(Scale.GetMin(), Scale.GetMax(), 1e-3 * Scale.GetMax()))
		{
			Add(Out, ESeverity::Warning, TEXT("ACF.Zone.SphereScale"), FText::Format(LOCTEXT("SphereScale", "The sphere is scaled non-uniformly ({0}); like USphereComponent the zone uses the smallest scale, radius {1} cm. Use a Box for a stretched space."),
				FText::FromString(Scale.ToCompactString()), FText::AsNumber(Sphere->GetScaledSphereRadius())));
		}
	}

	// Camera.
	if (!Zone.Preset)
	{
		Add(Out, ESeverity::Warning, TEXT("ACF.Zone.NoPreset"), LOCTEXT("NoPreset", "No Camera Preset: the zone is selected but requests no camera (a warning is logged when a player enters)."));
	}
	if (Zone.bOverrideBlendIn)
	{
		ValidateBlend(Out, Zone.BlendIn, LOCTEXT("BlendInName", "Blend In"));
	}
	if (Zone.bOverrideBlendOut)
	{
		ValidateBlend(Out, Zone.BlendOut, LOCTEXT("BlendOutName", "Blend Out"));
	}
	const bool bKeepView = Zone.EnterRotation == EACFCameraZoneRotationHandoff::KeepView || Zone.ExitRotation == EACFCameraZoneRotationHandoff::KeepView;
	if (bKeepView && UsesControlRotation(Zone.Preset))
	{
		Add(Out, ESeverity::Info, TEXT("ACF.Zone.KeepViewControlRotation"), LOCTEXT("KeepViewControlRotation", "Keep View with a camera driven by the Control Rotation: entering the zone sets the player's control rotation to the view (the movement direction of most games follows it)."));
	}

	// Timing and margin.
	ValidateSeconds(Out, Zone.EnterDelay, TEXT("Enter Delay"));
	ValidateSeconds(Out, Zone.ExitDelay, TEXT("Exit Delay"));
	if (!FMath::IsFinite(Zone.ExitMargin) || Zone.ExitMargin < 0.f)
	{
		Add(Out, ESeverity::Error, TEXT("ACF.Zone.Margin.Invalid"), LOCTEXT("MarginInvalid", "Exit Margin must be a number >= 0 (cm); it is treated as 0."));
	}
	if (!Zone.IsZoneEnabled())
	{
		Add(Out, ESeverity::Info, TEXT("ACF.Zone.Disabled"), LOCTEXT("Disabled", "The zone starts disabled (Set Zone Enabled turns it on)."));
	}
}

void ACFCameraZoneValidation::ValidateOverlaps(const UACFCameraZoneComponent& Zone, TArray<FACFCameraZoneIssue>& Out)
{
	using namespace ACFCameraZoneValidationImpl;
	const UWorld* World = Zone.GetWorld();
	FBox Bounds;
	if (Zone.IsTemplate() || !World || !Zone.IsZoneEnabled() || !ShapeBounds(Zone, Bounds))
	{
		return;
	}
	const double Volume = Zone.GetZoneVolume();
	const FString Key = Zone.GetPathName(); // the resolver's final tie-break (GetStableKey is set at BeginPlay)
	for (TObjectIterator<UACFCameraZoneComponent> It; It; ++It)
	{
		const UACFCameraZoneComponent* Other = *It;
		FBox OtherBounds;
		if (Other == &Zone || !IsValid(Other) || Other->IsTemplate() || Other->GetWorld() != World || !Other->GetOwner() || !Other->IsZoneEnabled()
			|| Other->ZonePriority != Zone.ZonePriority || !ShapeBounds(*Other, OtherBounds) || !Bounds.Intersect(OtherBounds))
		{
			continue;
		}
		const double OtherVolume = Other->GetZoneVolume();
		const bool bTie = FMath::Abs(Volume - OtherVolume) <= ACFCameraZoneResolver::VolumeTolerance * FMath::Max(Volume, OtherVolume);
		if (bTie)
		{
			const bool bThisWins = Key.Compare(Other->GetPathName(), ESearchCase::CaseSensitive) < 0;
			Add(Out, ESeverity::Warning, TEXT("ACF.Zone.Tie"), FText::Format(LOCTEXT("Tie", "Overlaps {0} with the same priority ({1}) and the same volume: where both contain the player, the path name decides ({2} wins). That order changes when an actor is renamed; give one of them another priority."),
				ZoneName(*Other), FText::AsNumber(Zone.ZonePriority), bThisWins ? ZoneName(Zone) : ZoneName(*Other)));
		}
		else
		{
			Add(Out, ESeverity::Info, TEXT("ACF.Zone.Overlap"), FText::Format(LOCTEXT("Overlap", "Overlaps {0} with the same priority ({1}): a player already in one of them stays in it (the current zone wins ties); entering both at once selects the smaller one ({2}). Use a higher priority for a zone that must win inside the other."),
				ZoneName(*Other), FText::AsNumber(Zone.ZonePriority), Volume < OtherVolume ? ZoneName(Zone) : ZoneName(*Other)));
		}
	}
}

void ACFCameraZoneValidation::Validate(const UACFCameraZoneComponent& Zone, TArray<FACFCameraZoneIssue>& Out)
{
	ValidateZone(Zone, Out);
	ValidateOverlaps(Zone, Out);
}

EACFCameraPresetIssueSeverity ACFCameraZoneValidation::MaxSeverity(TConstArrayView<FACFCameraZoneIssue> Issues)
{
	EACFCameraPresetIssueSeverity Max = EACFCameraPresetIssueSeverity::Info;
	for (const FACFCameraZoneIssue& Issue : Issues)
	{
		Max = static_cast<uint8>(Issue.Severity) > static_cast<uint8>(Max) ? Issue.Severity : Max;
	}
	return Max;
}

EDataValidationResult ACFCameraZoneValidation::Report(const UACFCameraZoneComponent& Zone, TConstArrayView<FACFCameraZoneIssue> Issues, FDataValidationContext& Context)
{
	bool bError = false;
	for (const FACFCameraZoneIssue& Issue : Issues)
	{
		const FText Text = FText::Format(LOCTEXT("ContextFormat", "Camera Zone {0}: {1}"), ACFCameraZoneValidationImpl::ZoneName(Zone), Issue.ToText());
		switch (Issue.Severity)
		{
		case EACFCameraPresetIssueSeverity::Error:
			Context.AddError(Text);
			bError = true;
			break;
		case EACFCameraPresetIssueSeverity::Warning:
			Context.AddWarning(Text);
			break;
		default:
			Context.AddMessage(EMessageSeverity::Info, Text);
			break;
		}
	}
	return bError ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}

void ACFCameraZoneValidation::ReportMapCheck(const UACFCameraZoneComponent& Zone, TConstArrayView<FACFCameraZoneIssue> Issues)
{
	FMessageLog MapCheck(TEXT("MapCheck"));
	for (const FACFCameraZoneIssue& Issue : Issues)
	{
		const EMessageSeverity::Type Severity = Issue.Severity == EACFCameraPresetIssueSeverity::Error ? EMessageSeverity::Error
			: Issue.Severity == EACFCameraPresetIssueSeverity::Warning ? EMessageSeverity::Warning : EMessageSeverity::Info;
		MapCheck.Message(Severity)
			->AddToken(FUObjectToken::Create(Zone.GetOwner() ? static_cast<const UObject*>(Zone.GetOwner()) : &Zone))
			->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheckFormat", " Camera Zone: {0}"), Issue.ToText())));
	}
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITOR
