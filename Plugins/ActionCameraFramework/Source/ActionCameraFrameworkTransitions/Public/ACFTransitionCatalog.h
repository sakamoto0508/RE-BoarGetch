#pragma once

#include "CoreMinimal.h"
#include "ACFTransitionTypes.h"
#include "ACFTransitionCatalog.generated.h"

/** Families of the screen effects (how they are built, README 33 章). */
UENUM(BlueprintType)
enum class EACFTransitionCategory : uint8
{
	Fade,
	WipeMask,
	IrisSpiral,
	Distortion,
	Digital,
	OverlayAnimation,
	CameraScreenshot,
	CardLoading,
};

/** Static description of one effect ID. */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORKTRANSITIONS_API FACFTransitionEffectInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	EACFTransitionEffect Effect = EACFTransitionEffect::None;

	/** The fixed catalog ID. */
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	int32 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	EACFTransitionCategory Category = EACFTransitionCategory::Fade;

	/** Close captures the screen (a still of the game view including UI) and distorts it. Without a capture (no RHI,
	 *  capture failed) the effect draws its procedural variant instead. */
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	bool bUsesSnapshot = false;

	/** Open keeps using the capture made by the Close before it (Cross Dissolve: old frame -> new live scene). */
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	bool bOpenUsesSnapshot = false;

	/** One line: how the effect is drawn. */
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	FString Description;
};

namespace ACFTransition
{
	inline constexpr int32 NumEffects = static_cast<int32>(EACFTransitionEffect::SubjectIris);

	/** Info of an effect; a None entry for invalid values. */
	ACTIONCAMERAFRAMEWORKTRANSITIONS_API const FACFTransitionEffectInfo& GetEffectInfo(EACFTransitionEffect Effect);

	/** All effects in ID order. */
	ACTIONCAMERAFRAMEWORKTRANSITIONS_API const TArray<FACFTransitionEffectInfo>& GetAllEffects();

	/** The tuned default look of an effect (the standard presets are made from it). */
	ACTIONCAMERAFRAMEWORKTRANSITIONS_API FACFTransitionSettings MakeDefaultSettings(EACFTransitionEffect Effect, EACFTransitionPhase Phase = EACFTransitionPhase::Close);

	/** "41", "SpiralClose", "Spiral Close" or "spiralclose" -> the effect (None when unknown). */
	ACTIONCAMERAFRAMEWORKTRANSITIONS_API EACFTransitionEffect ParseEffect(const FString& Text);

	inline EACFTransitionEffect FromId(int32 Id)
	{
		return (Id >= 1 && Id <= NumEffects) ? static_cast<EACFTransitionEffect>(Id) : EACFTransitionEffect::None;
	}
}
