#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RetroRenderWorldSubsystem.generated.h"
class ARetroRenderActor;

// Applies URetroRenderSettings to game worlds that have no Retro Render actor of their own.
UCLASS()
class RETRORENDER_API URetroRenderWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	// The actor spawned by this subsystem (null when the level provides its own or auto apply is off).
	UFUNCTION(BlueprintPure, Category="Retro") ARetroRenderActor* GetAutoActor() const { return AutoActor; }
protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
private:
	UPROPERTY(Transient) TObjectPtr<ARetroRenderActor> AutoActor;
};
