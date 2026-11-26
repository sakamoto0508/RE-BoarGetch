#pragma once

#include "CoreMinimal.h"
#include "ACFTransitionCatalog.h"
#include "ACFTransitionTypes.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ACFTransitionLibrary.generated.h"

class UACFTransitionPreset;
class UACFTransitionSubsystem;

/** Blueprint helpers of the transition system (README 33 章「Blueprint」). */
UCLASS()
class ACTIONCAMERAFRAMEWORKTRANSITIONS_API UACFTransitionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "ACF|Transition", meta = (WorldContext = "WorldContextObject"))
	static UACFTransitionSubsystem* GetTransitionSubsystem(const UObject* WorldContextObject);

	/** Plays one effect with its default look. Duration < 0 keeps the default. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition", meta = (WorldContext = "WorldContextObject"))
	static FACFTransitionHandle PlayTransitionEffect(const UObject* WorldContextObject, EACFTransitionEffect Effect, EACFTransitionPhase Phase, float Duration = -1.f, bool bLockInput = false);

	/** The same with the fixed catalog ID. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition", meta = (WorldContext = "WorldContextObject"))
	static FACFTransitionHandle PlayTransitionById(const UObject* WorldContextObject, int32 EffectId, EACFTransitionPhase Phase, float Duration = -1.f);

	/** Plays the preset's Close, hold and Open in the current viewport. Returns an invalid handle if the preset or viewport is unavailable, or a transition already occupies the screen. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition|Preview", meta = (WorldContext = "WorldContextObject", DisplayName = "Preview Transition Preset", Keywords = "camera screen effect test fade wipe"))
	static FACFTransitionHandle PreviewTransitionPreset(const UObject* WorldContextObject, const UACFTransitionPreset* Preset, float HoldSeconds = 0.4f);

	/** The default look of an effect, to change and pass to the subsystem. */
	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	static FACFTransitionSettings MakeDefaultTransitionSettings(EACFTransitionEffect Effect, EACFTransitionPhase Phase = EACFTransitionPhase::Close);

	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	static TArray<FACFTransitionEffectInfo> GetAllTransitionEffects();

	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	static FACFTransitionEffectInfo GetTransitionEffectInfo(EACFTransitionEffect Effect);

	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	static EACFTransitionEffect TransitionEffectFromId(int32 EffectId);

	/** "41", "SpiralClose" or "Spiral Close" -> the effect (None when unknown). */
	UFUNCTION(BlueprintPure, Category = "ACF|Transition")
	static EACFTransitionEffect ParseTransitionEffect(const FString& Text);

	UFUNCTION(BlueprintPure, Category = "ACF|Transition", meta = (DisplayName = "Is Valid (Transition Handle)"))
	static bool IsValidTransitionHandle(FACFTransitionHandle Handle) { return Handle.IsValid(); }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FACFTransitionActionPin, FACFTransitionHandle, Handle);

/**
 * Latent Blueprint nodes: play a transition and continue on its events. Covered fires once the screen is fully covered
 * (load or cut there), Completed when it finished, Failed on Failed / Cancelled / Interrupted.
 */
UCLASS()
class ACTIONCAMERAFRAMEWORKTRANSITIONS_API UACFPlayTransitionAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Close or Open with settings. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "Options", DisplayName = "Play Transition (Async)"))
	static UACFPlayTransitionAction* PlayTransitionAsync(const UObject* WorldContextObject, const FACFTransitionSettings& Settings, EACFTransitionPhase Phase, const FACFTransitionOptions& Options);

	/** Close or Open of a preset. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "Options", DisplayName = "Play Transition Preset (Async)"))
	static UACFPlayTransitionAction* PlayTransitionPresetAsync(const UObject* WorldContextObject, const UACFTransitionPreset* Preset, EACFTransitionPhase Phase, const FACFTransitionOptions& Options);

	/** Close -> OpenLevel(LevelName) (or the game loads on Covered when LevelName is None) -> Open. */
	UFUNCTION(BlueprintCallable, Category = "ACF|Transition", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "Options", DisplayName = "Play Level Transition (Async)"))
	static UACFPlayTransitionAction* PlayLevelTransitionAsync(const UObject* WorldContextObject, const UACFTransitionPreset* Preset, FName LevelName, const FACFTransitionOptions& Options);

	UPROPERTY(BlueprintAssignable)
	FACFTransitionActionPin Covered;

	UPROPERTY(BlueprintAssignable)
	FACFTransitionActionPin LoadCompleted;

	UPROPERTY(BlueprintAssignable)
	FACFTransitionActionPin Completed;

	UPROPERTY(BlueprintAssignable)
	FACFTransitionActionPin Failed;

	virtual void Activate() override;

private:
	void HandleEvent(FACFTransitionHandle Handle, EACFTransitionEvent Event);

	TWeakObjectPtr<UACFTransitionSubsystem> Subsystem;
	FACFTransitionHandle MyHandle;
	TFunction<FACFTransitionHandle(UACFTransitionSubsystem&)> Starter;
	FDelegateHandle Binding;
};
