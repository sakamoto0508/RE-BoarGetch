#pragma once

#include "CoreMinimal.h"
#include "ACFTransitionTypes.generated.h"

class UCurveFloat;
class UMaterialInterface;
class USoundBase;
class UTexture;

ACTIONCAMERAFRAMEWORKTRANSITIONS_API DECLARE_LOG_CATEGORY_EXTERN(LogACFTransition, Log, All);

/**
 * The 42 screen transitions (README 33 章). The numeric values are fixed IDs: never renumber them (presets, console
 * commands and saved settings use them). Every ID draws its own pattern in M_ACFTransition (Scripts/Transitions/
 * ACFTransitionEffects.hlsl); the catalog (ACFTransitionCatalog.h) lists names, categories and defaults.
 */
UENUM(BlueprintType)
enum class EACFTransitionEffect : uint8
{
	None = 0 UMETA(Hidden),
	BlackFade = 1,
	WhiteFlash = 2,
	ColorFade = 3,
	CrossDissolve = 4,
	HorizontalWipe = 5,
	DiagonalWipe = 6,
	CircleIris = 7,
	IrisCloseOpen = 8 UMETA(DisplayName = "Iris Close / Open"),
	VerticalBlinds = 9,
	HorizontalShutter = 10,
	Scanlines = 11,
	DigitalGlitch = 12,
	PixelDisintegration = 13,
	MosaicTransition = 14,
	ZoomRush = 15,
	RotationSpin = 16,
	PortalRing = 17,
	CellDisintegration = 18,
	InkSpread = 19,
	SliceShift = 20,
	LightSweep = 21,
	FilmBurn = 22,
	SpatialDistortion = 23,
	LoadingCard = 24,
	NetSweep = 25,
	CageDoor = 26,
	CaptureBeam = 27,
	BoarFootprints = 28,
	HologramTransfer = 29,
	WireframeConversion = 30,
	DataCubes = 31,
	CRTPowerOff = 32 UMETA(DisplayName = "CRT Power Off"),
	LaboratoryAutoDoor = 33,
	StageCard = 34,
	MangaPanels = 35,
	SwordSlash = 36,
	FogSmoke = 37 UMETA(DisplayName = "Fog / Smoke"),
	Shockwave = 38,
	MatchCut = 39,
	WhipPan = 40,
	SpiralClose = 41,
	SpiralOpen = 42,
};

/** Which way a transition plays. Close: the scene is visible at the start and fully covered at the end. Open: the reverse. */
UENUM(BlueprintType)
enum class EACFTransitionPhase : uint8
{
	Close,
	Open,
};

UENUM(BlueprintType)
enum class EACFTransitionEasing : uint8
{
	Linear,
	EaseIn,
	EaseOut,
	EaseInOut,
	/** The Curve of the settings (time 0..1 -> value 0..1). Linear when no curve is set. */
	Curve,
};

/** What happens when a transition is requested while another one is playing (README 33 章「多重要求」). */
UENUM(BlueprintType)
enum class EACFTransitionConcurrency : uint8
{
	/** The playing transition ends with Interrupted and the new one starts at once. */
	Interrupt,
	/** The new one waits until the playing one (and the queue before it) has finished. */
	Queue,
	/** The new one is not started: its handle reports Failed at once. */
	Reject,
};

/** Events of one transition handle, in the order they can happen. */
UENUM(BlueprintType)
enum class EACFTransitionEvent : uint8
{
	/** The transition began to play (after a queued one: when it left the queue). */
	Started,
	/** Close finished: the screen is fully covered and a covered frame has been presented. Safe to load or cut. */
	Covered,
	/** Level transitions: the map load began (PreLoadMap, or NotifyLoadStarted). */
	LoadStarted,
	/** Level transitions: the new world is ready (loaded, begun play, frames rendered, game's ready signal). */
	LoadCompleted,
	/** The transition reached its end (Open: the screen is visible; Close with Hold: still covered). Terminal. */
	Finished,
	/** Rejected, load failure, travel failure or timeout. The screen is uncovered unless Hold was asked. Terminal. */
	Failed,
	/** Cancel() was called. Terminal. */
	Cancelled,
	/** Another transition replaced this one (Interrupt policy). Terminal. */
	Interrupted,
};

/** Look of one transition: the effect and its parameters. A preset (UACFTransitionPreset) holds one for each phase. */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORKTRANSITIONS_API FACFTransitionSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	EACFTransitionEffect Effect = EACFTransitionEffect::BlackFade;

	/** Seconds. 0 = jump to the end at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (ClampMin = "0", UIMin = "0", UIMax = "5"))
	float Duration = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	EACFTransitionEasing Easing = EACFTransitionEasing::EaseInOut;

	/** Used when Easing is Curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (EditCondition = "Easing == EACFTransitionEasing::Curve"))
	TObjectPtr<UCurveFloat> Curve = nullptr;

	/** Main colour (the covered area). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FLinearColor Color = FLinearColor::Black;

	/** Accent colour (edges, glow, ropes, bars, ...; what it is depends on the effect). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FLinearColor SecondaryColor = FLinearColor::White;

	/** Direction of wipes / sweeps / slashes in degrees (0 = left to right, 90 = top to bottom). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (UIMin = "-180", UIMax = "180"))
	float DirectionDegrees = 0.f;

	/** Reverses the rotation / order (spirals: the winding and spin direction; blinds, slices: the order). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bReverse = false;

	/** Effect strength (distortion amount, spiral tightness, glitch amount, net density, print size, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", UIMin = "0.2", UIMax = "3"))
	float Intensity = 1.f;

	/** Edge softness (0 = hard). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "1"))
	float Softness = 0.f;

	/** Count of the effect (blinds, scan lines, pixels per column, iris blades, cells, slices, bars, spiral turns). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0.25", UIMin = "1", UIMax = "120"))
	float Count = 1.f;

	/** Screen point (0..1) of irises, portals, beams, shockwaves, spirals, match cuts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FVector2D Center = FVector2D(0.5, 0.5);

	/** Random seed of the noise-based effects. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	float Seed = 0.f;

	/** Optional art: replaces the procedural shape of Net Sweep (tiled net), Cage Door (bar tile) and Boar Footprints
	 *  (one print, alpha = shape); tints the covered area of the other effects. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assets")
	TObjectPtr<UTexture> OverlayTexture = nullptr;

	/** Optional material that replaces M_ACFTransition (UI domain; it receives the same parameters, README 33 章). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assets")
	TObjectPtr<UMaterialInterface> MaterialOverride = nullptr;

	/** Optional 2D sound played when the transition starts (it survives a level change). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assets")
	TObjectPtr<USoundBase> Sound = nullptr;

	/** Optional text shown while the screen is covered (Loading Card, Stage Card or any effect). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assets")
	FText LoadingText;

	/** True when the effect would draw something (a valid ID). */
	bool IsValid() const;
};

/** How a transition is played (independent of the look). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORKTRANSITIONS_API FACFTransitionOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	EACFTransitionConcurrency Concurrency = EACFTransitionConcurrency::Interrupt;

	/** Close: keep the screen covered after Finished until an Open (or Hide) is played. Default true for Close. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	bool bHoldWhenCovered = true;

	/** Ignore move / look input of the local players while the transition plays and the overlay absorbs mouse clicks.
	 *  Restored when the handle ends in any way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	bool bLockInput = false;

	/** Plays while the game is paused (real time). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	bool bPlayWhenPaused = true;

	/** Level transitions: wait for NotifyWorldReady() after the new world has loaded (e.g. stage setup, streaming). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	bool bWaitForReadySignal = false;

	/** Level transitions: no map load is expected (level streaming / own loading). The game calls NotifyLoadStarted
	 *  (optional) and NotifyLoadCompleted when the content is ready. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	bool bManualLoad = false;

	/** Level transitions: seconds the screen may stay covered waiting for the load / ready signal before the transition
	 *  fails and opens anyway (never leaves a black screen). <= 0 uses the project setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level")
	float TimeoutSeconds = 0.f;
};

/** Identifies one requested transition. */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORKTRANSITIONS_API FACFTransitionHandle
{
	GENERATED_BODY()

	FACFTransitionHandle() = default;
	explicit FACFTransitionHandle(int32 InId) : Id(InId) {}

	bool IsValid() const { return Id != INDEX_NONE; }
	void Invalidate() { Id = INDEX_NONE; }
	int32 GetId() const { return Id; }

	bool operator==(const FACFTransitionHandle& Other) const { return Id == Other.Id; }
	bool operator!=(const FACFTransitionHandle& Other) const { return Id != Other.Id; }
	friend uint32 GetTypeHash(const FACFTransitionHandle& Handle) { return ::GetTypeHash(Handle.Id); }

	FString ToString() const { return IsValid() ? FString::Printf(TEXT("ACFTransition#%d"), Id) : TEXT("ACFTransition#None"); }

private:
	UPROPERTY()
	int32 Id = INDEX_NONE;
};

/** State of the overlay (not of one handle). */
UENUM(BlueprintType)
enum class EACFTransitionScreenState : uint8
{
	/** Nothing drawn. */
	Clear,
	/** A Close or Open is animating. */
	Playing,
	/** Fully covered and not animating (after a Close, during a level load). */
	Covered,
};

UENUM(BlueprintType)
enum class EACFTransitionKind : uint8
{
	Close,
	Open,
	/** Close -> (load) -> Open. */
	Level,
};

/** Read-only state of one handle (UACFTransitionSubsystem::GetTransitionInfo). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORKTRANSITIONS_API FACFTransitionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	FACFTransitionHandle Handle;

	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	EACFTransitionKind Kind = EACFTransitionKind::Close;

	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	EACFTransitionEffect Effect = EACFTransitionEffect::None;

	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	EACFTransitionPhase Phase = EACFTransitionPhase::Close;

	/** 0..1 of the current phase (not eased). */
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	float Progress = 0.f;

	/** 0 = scene visible .. 1 = fully covered (eased; what is on screen). */
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	float Coverage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	bool bQueued = false;

	/** The last event of the handle (terminal events stay). */
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	EACFTransitionEvent LastEvent = EACFTransitionEvent::Started;
};

namespace ACFTransition
{
	/** Applies the easing of the settings to a linear 0..1 value. */
	ACTIONCAMERAFRAMEWORKTRANSITIONS_API float Ease(const FACFTransitionSettings& Settings, float Alpha);

	/** True for the terminal events (Finished, Failed, Cancelled, Interrupted). */
	inline bool IsTerminal(EACFTransitionEvent Event)
	{
		return Event == EACFTransitionEvent::Finished || Event == EACFTransitionEvent::Failed || Event == EACFTransitionEvent::Cancelled || Event == EACFTransitionEvent::Interrupted;
	}

	ACTIONCAMERAFRAMEWORKTRANSITIONS_API const TCHAR* LexEvent(EACFTransitionEvent Event);
}
