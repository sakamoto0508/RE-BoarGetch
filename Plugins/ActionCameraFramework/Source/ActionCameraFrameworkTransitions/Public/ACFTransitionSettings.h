#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ACFTransitionSettings.generated.h"

/** Project Settings > Plugins > ACF Transitions (README 33 章「設定」). */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ACF Transitions"))
class ACTIONCAMERAFRAMEWORKTRANSITIONS_API UACFTransitionProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UACFTransitionProjectSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** The material that draws the 42 effects (UI domain). The plugin's M_ACFTransition by default. */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath TransitionMaterial;

	/** Z order of the overlay in the game viewport (above the game's UMG). */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering")
	int32 OverlayZOrder = 10000;

	/** Capture the screen for the effects that distort it (Cross Dissolve, Zoom Rush, ...). Off: they draw their
	 *  procedural variant. The capture is taken once at the start of the Close (bCaptureOnGpu). */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering")
	bool bCaptureScreen = true;

	/** Copy the screen on the GPU when the frame is presented (no CPU read back, no game thread stall; the Close starts one or
	 *  two frames later, when the copy is ready). Off, or when the copy is not possible (HDR / non-BGRA back buffer): the
	 *  synchronous read back (a one-frame hitch, README 33 章「キャプチャ」). */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering")
	bool bCaptureOnGpu = true;

	/** Frames a Close waits for the GPU capture before it uses the read back instead. */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering", meta = (ClampMin = "1", ClampMax = "30"))
	int32 MaxCaptureWaitFrames = 6;

	/** The read back capture is halved until it is at most this wide (memory / upload cost). The GPU copy keeps the viewport size. */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering", meta = (ClampMin = "256"))
	int32 MaxCaptureWidth = 1920;

	/** Frames that must be presented fully covered before Covered is sent (the game loads after it). */
	UPROPERTY(Config, EditAnywhere, Category = "Level", meta = (ClampMin = "1", ClampMax = "10"))
	int32 CoveredFrames = 2;

	/** Frames the new world must tick and render after the load before the Open starts. */
	UPROPERTY(Config, EditAnywhere, Category = "Level", meta = (ClampMin = "1", ClampMax = "60"))
	int32 WorldReadyFrames = 3;

	/** Seconds a level transition may wait covered for the load or the ready signal (time spent inside a blocking
	 *  load is not counted). Then it fails and opens. */
	UPROPERTY(Config, EditAnywhere, Category = "Level", meta = (ClampMin = "1"))
	float LevelTimeoutSeconds = 30.f;

	/** Standalone / packaged games: show a MoviePlayer loading screen (the covered colour and the loading text, drawn
	 *  on the loading thread) during a blocking map load. Not in PIE. Off: the last covered frame stays on screen. */
	UPROPERTY(Config, EditAnywhere, Category = "Level")
	bool bUseMoviePlayerDuringLoad = false;

	/** Longest frame step the animation takes (hitches and resumed loads do not skip the effect). */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering", meta = (ClampMin = "0.01", ClampMax = "1"))
	float MaxDeltaSeconds = 0.1f;
};
