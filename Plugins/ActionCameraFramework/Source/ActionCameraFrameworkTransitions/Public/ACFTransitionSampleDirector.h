#pragma once

#include "CoreMinimal.h"
#include "ACFTransitionTypes.h"
#include "GameFramework/Actor.h"
#include "ACFTransitionSampleDirector.generated.h"

class UTextRenderComponent;

/**
 * Sample (README 33 章「サンプル」): placed in /ActionCameraFramework/Transitions/Samples/L_ACFTransitionA and _B.
 * It shows how a game drives a level transition and is the automatic Level A -> B -> A check of packaged builds
 * (Development and Shipping, where console commands do not exist):
 *
 *   <Game>.exe /ActionCameraFramework/Transitions/Samples/L_ACFTransitionA -ACFTransitionDemo=Travel:4 [-ACFTransitionClose=41 -ACFTransitionOpen=42] [-ACFTransitionDemoExit]
 *   <Game>.exe /ActionCameraFramework/Transitions/Samples/L_ACFTransitionA -ACFTransitionDemo=All     (every effect, close + open)
 *
 * Shipping ignores a map on the command line; the game then starts on its default map and the plugin opens
 * L_ACFTransitionA right after it (RegisterCommandLineBootstrap).
 * Each step is written to <Saved>/ACFTransitionDemo.log (also in Shipping): the events, whether the screen was fully
 * covered and the overlay present when the map load started and after it, and whether the overlay was gone at the end.
 * Without the command line option the actor does nothing (the maps are plain test maps).
 */
UCLASS(Blueprintable)
class ACTIONCAMERAFRAMEWORKTRANSITIONS_API AACFTransitionSampleDirector : public AActor
{
	GENERATED_BODY()

public:
	AACFTransitionSampleDirector();

	/** The map the travel demo goes to (the other sample map). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sample")
	FName NextLevel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sample")
	EACFTransitionEffect CloseEffect = EACFTransitionEffect::SpiralClose;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sample")
	EACFTransitionEffect OpenEffect = EACFTransitionEffect::SpiralOpen;

	/** Seconds in the map before the next travel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sample")
	float StayTime = 1.5f;

	/** Starts a level transition to NextLevel now (also callable from a level Blueprint / UI). */
	UFUNCTION(BlueprintCallable, Category = "Sample")
	void TravelToNextLevel();

	/** Appends a line to <Saved>/ACFTransitionDemo.log (and the log). */
	static void WriteDemoLog(const FString& Line);

	/** Module start: with -ACFTransitionDemo, a game that starts on another map (Shipping ignores the map on the command
	 *  line) opens L_ACFTransitionA after its first map. */
	static void RegisterCommandLineBootstrap();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void HandleEvent(FACFTransitionHandle Handle, EACFTransitionEvent Event);
	void PlayNextEffect();
	void FinishDemo();

	FTimerHandle Timer;
	FDelegateHandle EventBinding;
	int32 EffectCursor = 0;
	bool bEffectOpenNext = false;
};
