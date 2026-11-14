#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "ACFCameraTypes.h"
#include "ACFCameraZoneComponent.generated.h"

class UACFCameraPreset;
class UShapeComponent;

/** How a zone camera takes over (or hands back) the camera rotation (Phase 7C-1B, README 32 章). */
UENUM(BlueprintType)
enum class EACFCameraZoneRotationHandoff : uint8
{
	/**
	 * Continue from the view on screen: the camera that takes over gets the current view rotation (its Orbital Follow
	 * rotation; for a camera driven by the Control Rotation, the PlayerController's control rotation). Only when the zone
	 * camera is (or was) the camera on screen: a Lock-On / Finisher above it is never changed.
	 */
	KeepView,
	/** Leave the rotation to the camera (its Initial Rotation, its own orbit state or the control rotation). */
	UseCameraSetting,
};

/**
 * Phase 7C-1A: a Camera Zone (README 32 章). The space is a Box or Sphere component of the same actor; a player whose pawn
 * is inside selects the zone (ZonePriority and the tie-breaks of ACFCameraZoneResolver.h decide between overlapping zones).
 * The zone registers itself with the World's UACFCameraZoneSubsystem from BeginPlay to EndPlay (destruction, level
 * streaming, end of play). Inside / outside is a point test of the shape every frame, not overlap events: the shape's
 * collision is not used at all.
 * 7C-1B: while a player has the zone selected, the zone subsystem keeps one camera request of Preset for that player
 * (Priority: UACFCameraZoneSettings::ZoneRequestPriority).
 */
UCLASS(ClassGroup = "Action Camera", meta = (BlueprintSpawnableComponent, DisplayName = "ACF Camera Zone"))
class ACTIONCAMERAFRAMEWORKLEVEL_API UACFCameraZoneComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UACFCameraZoneComponent();

	/** Among overlapping zones the highest wins. Equal: the zone the player is in stays, then the smaller one, then a stable order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	int32 ZonePriority = 0;

	/**
	 * Spatial hysteresis: once selected, the zone is kept until the pawn is this far outside its shape (cm, world units).
	 * Entering uses the shape itself. Walking along the boundary therefore does not switch back and forth.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone", meta = (ClampMin = "0.0", Units = cm))
	float ExitMargin = 50.f;

	/** The shape: a Box or Sphere component of the owner. Empty: the owner's first Box or Sphere component. */
	UPROPERTY(EditAnywhere, Category = "Zone", meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.BoxComponent,/Script/Engine.SphereComponent"))
	FComponentReference Shape;

	/** The camera of the zone. Empty: the zone is selected but requests nothing (a warning is logged once). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UACFCameraPreset> Preset;

	/** Look-At target of the request (an actor of the level). Empty or destroyed: the request has no Look-At target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<AActor> LookAtTarget;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (InlineEditConditionToggle))
	bool bOverrideBlendIn = false;

	/** Blend into this zone's camera (from the exploration camera or another zone). Default: the preset's Default Blend In. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (EditCondition = "bOverrideBlendIn"))
	FACFCameraBlendSettings BlendIn;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (InlineEditConditionToggle))
	bool bOverrideBlendOut = false;

	/**
	 * Blend when the player leaves the zone for no zone while this camera is on screen: to the camera below it (the
	 * exploration request) or to the UE camera. Default: the preset's Default Blend Out. To another zone, that zone's Blend
	 * In is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (EditCondition = "bOverrideBlendOut"))
	FACFCameraBlendSettings BlendOut;

	/** Rotation of this zone's camera when it takes over. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	EACFCameraZoneRotationHandoff EnterRotation = EACFCameraZoneRotationHandoff::KeepView;

	/** Rotation of the camera that takes over when the player leaves this zone for no zone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	EACFCameraZoneRotationHandoff ExitRotation = EACFCameraZoneRotationHandoff::KeepView;

	/** Blend Out if overridden, else the preset's Default Blend Out (a default blend without preset). */
	FACFCameraBlendSettings GetExitBlend() const;

	/** Preset, or null with a warning (logged once per zone). */
	UACFCameraPreset* GetPresetOrWarn() const;

	/** A disabled zone is not selected (players inside leave it at once). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Zone")
	void SetZoneEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Action Camera|Zone")
	bool IsZoneEnabled() const { return bZoneEnabled; }

	/** The shape in use (Box or Sphere), null if the owner has none. */
	UShapeComponent* GetShapeComponent() const;

	/** Is the world point inside the shape grown by Margin cm (false without a valid shape). */
	bool ContainsPoint(const FVector& WorldPoint, double Margin = 0.0) const;

	/** World volume of the shape (cm^3), 0 without a valid shape. */
	double GetZoneVolume() const;

	/** The final tie-break of the resolver: the path name, fixed at BeginPlay (unique, the same in every run of the map). */
	const FString& GetStableKey() const { return StableKey; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Initial state (Set Zone Enabled at run time). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone", meta = (DisplayName = "Zone Enabled"))
	bool bZoneEnabled = true;

private:
	/** Resolves Shape (cached; looked up again if the component went away). */
	UShapeComponent* ResolveShape() const;

	mutable TWeakObjectPtr<UShapeComponent> CachedShape;
	FString StableKey;
	bool bRegistered = false;
	mutable bool bWarnedMissingPreset = false;
};
