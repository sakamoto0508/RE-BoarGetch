#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "ACFCameraZoneComponent.generated.h"

class UShapeComponent;

/**
 * Phase 7C-1A: a Camera Zone (README 32 章). The space is a Box or Sphere component of the same actor; a player whose pawn
 * is inside selects the zone (ZonePriority and the tie-breaks of ACFCameraZoneResolver.h decide between overlapping zones).
 * The zone registers itself with the World's UACFCameraZoneSubsystem from BeginPlay to EndPlay (destruction, level
 * streaming, end of play). Inside / outside is a point test of the shape every frame, not overlap events: the shape's
 * collision is not used at all.
 * 7C-1A only selects the zone; requesting its camera comes in 7C-1B.
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
};
