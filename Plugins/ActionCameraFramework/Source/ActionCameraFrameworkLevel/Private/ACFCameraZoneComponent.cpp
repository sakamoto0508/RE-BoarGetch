#include "ACFCameraZoneComponent.h"
#include "ACFCameraPreset.h"
#include "ACFCameraZoneResolver.h"
#include "ACFCameraZoneSubsystem.h"
#include "ACFCameraZoneValidation.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UACFCameraZoneComponent::UACFCameraZoneComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // the zone subsystem tests the zones; the component does nothing per frame
}

void UACFCameraZoneComponent::BeginPlay()
{
	Super::BeginPlay();
	StableKey = GetPathName();
	if (!GetShapeComponent())
	{
		UE_LOG(LogTemp, Warning, TEXT("ACF Camera Zone %s: no Box or Sphere component on %s; the zone is never selected."), *GetName(), *GetNameSafe(GetOwner()));
	}
	if (UACFCameraZoneSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UACFCameraZoneSubsystem>() : nullptr)
	{
		Subsystem->RegisterZone(this);
		bRegistered = true;
	}
}

void UACFCameraZoneComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Destroyed, removed with its streamed level, or the end of play: no player keeps this zone selected.
	if (bRegistered)
	{
		if (UACFCameraZoneSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UACFCameraZoneSubsystem>() : nullptr)
		{
			Subsystem->UnregisterZone(this);
		}
		bRegistered = false;
	}
	Super::EndPlay(EndPlayReason);
}

void UACFCameraZoneComponent::SetZoneEnabled(bool bEnabled)
{
	if (bZoneEnabled == bEnabled)
	{
		return;
	}
	bZoneEnabled = bEnabled;
	if (bRegistered)
	{
		if (UACFCameraZoneSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UACFCameraZoneSubsystem>() : nullptr)
		{
			Subsystem->NotifyZoneChanged(this);
		}
	}
}

#if WITH_EDITOR
EDataValidationResult UACFCameraZoneComponent::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TArray<FACFCameraZoneIssue> Issues;
	ACFCameraZoneValidation::Validate(*this, Issues);
	if (ACFCameraZoneValidation::Report(*this, Issues, Context) == EDataValidationResult::Invalid)
	{
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}

void UACFCameraZoneComponent::CheckForErrors()
{
	Super::CheckForErrors();
	TArray<FACFCameraZoneIssue> Issues;
	ACFCameraZoneValidation::Validate(*this, Issues);
	ACFCameraZoneValidation::ReportMapCheck(*this, Issues);
}
#endif

FACFCameraBlendSettings UACFCameraZoneComponent::GetExitBlend() const
{
	if (bOverrideBlendOut)
	{
		return BlendOut;
	}
	return Preset ? Preset->DefaultBlendOut : FACFCameraBlendSettings();
}

UACFCameraPreset* UACFCameraZoneComponent::GetPresetOrWarn() const
{
	if (!Preset && !bWarnedMissingPreset)
	{
		bWarnedMissingPreset = true;
		UE_LOG(LogActionCamera, Warning, TEXT("ACF Camera Zone %s on %s has no Camera Preset: the zone is selected but requests no camera."), *GetName(), *GetNameSafe(GetOwner()));
	}
	return Preset;
}

UShapeComponent* UACFCameraZoneComponent::ResolveShape() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	auto IsSupported = [](const UActorComponent* Component) { return Component && (Component->IsA<UBoxComponent>() || Component->IsA<USphereComponent>()); };
	if (UActorComponent* Picked = Shape.GetComponent(Owner))
	{
		return IsSupported(Picked) ? Cast<UShapeComponent>(Picked) : nullptr; // an explicit pick of another kind is not replaced
	}
	if (IsSupported(Owner->GetRootComponent()))
	{
		return Cast<UShapeComponent>(Owner->GetRootComponent());
	}
	TInlineComponentArray<UShapeComponent*> Shapes(Owner);
	for (UShapeComponent* Candidate : Shapes)
	{
		if (IsSupported(Candidate))
		{
			return Candidate;
		}
	}
	return nullptr;
}

UShapeComponent* UACFCameraZoneComponent::GetShapeComponent() const
{
	UShapeComponent* Found = CachedShape.Get();
	if (!Found)
	{
		Found = ResolveShape();
		CachedShape = Found;
	}
	return Found;
}

bool UACFCameraZoneComponent::ContainsPoint(const FVector& WorldPoint, double Margin) const
{
	const UShapeComponent* ShapeComponent = GetShapeComponent();
	if (const UBoxComponent* Box = Cast<UBoxComponent>(ShapeComponent))
	{
		return ACFCameraZoneGeometry::BoxContains(Box->GetComponentTransform(), Box->GetUnscaledBoxExtent(), WorldPoint, Margin);
	}
	if (const USphereComponent* Sphere = Cast<USphereComponent>(ShapeComponent))
	{
		return ACFCameraZoneGeometry::SphereContains(Sphere->GetComponentLocation(), Sphere->GetScaledSphereRadius(), WorldPoint, Margin);
	}
	return false;
}

double UACFCameraZoneComponent::GetZoneVolume() const
{
	const UShapeComponent* ShapeComponent = GetShapeComponent();
	if (const UBoxComponent* Box = Cast<UBoxComponent>(ShapeComponent))
	{
		return ACFCameraZoneGeometry::BoxVolume(Box->GetComponentTransform(), Box->GetUnscaledBoxExtent());
	}
	if (const USphereComponent* Sphere = Cast<USphereComponent>(ShapeComponent))
	{
		return ACFCameraZoneGeometry::SphereVolume(Sphere->GetScaledSphereRadius());
	}
	return 0.0;
}
