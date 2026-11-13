#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ACFCameraTuning.h"
#include "ACFCameraTuningLibrary.generated.h"

class APlayerController;

/**
 * Runtime camera tuning from Blueprint (Phase 7A-1, README 24 章). Development builds only (NotAvailable in Shipping).
 * Request = a live camera request of the player; an invalid handle (default) means the player's active request.
 * Nothing here writes the preset asset: the request runs on its own copy until Stop.
 */
UCLASS()
class ACTIONCAMERAFRAMEWORK_API UACFCameraTuningLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Starts tuning the request (Ok if it already is). Its camera keeps running without a jump or reset. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Tuning", meta = (DisplayName = "Start ACF Camera Tuning", AutoCreateRefTerm = "Request"))
	static EACFCameraTuningResult StartCameraTuning(APlayerController* PlayerController, const FACFCameraRequestHandle& Request);

	/** Ends tuning: the request returns to the preset asset (tuned values are discarded), its camera state is kept. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Tuning", meta = (DisplayName = "Stop ACF Camera Tuning", AutoCreateRefTerm = "Request"))
	static EACFCameraTuningResult StopCameraTuning(APlayerController* PlayerController, const FACFCameraRequestHandle& Request);

	/** Current value (Value) and the asset value (Original Value). Works without tuning too (both are the asset value). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Tuning", meta = (DisplayName = "Get ACF Camera Tuning Value", AutoCreateRefTerm = "Request"))
	static EACFCameraTuningResult GetCameraTuningValue(APlayerController* PlayerController, const FACFCameraRequestHandle& Request, EACFCameraTuningParameter Parameter,
		FACFCameraTuningValue& Value, int32 NodeIndex = -1);

	/** Sets a value (scalar parameters use X). Applied from the next camera update. Node Index -1 = the first node that has it. */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Tuning", meta = (DisplayName = "Set ACF Camera Tuning Value", AutoCreateRefTerm = "Request"))
	static EACFCameraTuningResult SetCameraTuningValue(APlayerController* PlayerController, const FACFCameraRequestHandle& Request, EACFCameraTuningParameter Parameter,
		FVector Value, int32 NodeIndex = -1);

	/** Back to the asset value for one parameter / for all tunable parameters (tuning continues). */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Tuning", meta = (DisplayName = "Reset ACF Camera Tuning Value", AutoCreateRefTerm = "Request"))
	static EACFCameraTuningResult ResetCameraTuningValue(APlayerController* PlayerController, const FACFCameraRequestHandle& Request, EACFCameraTuningParameter Parameter,
		int32 NodeIndex = -1);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Tuning", meta = (DisplayName = "Reset All ACF Camera Tuning Values", AutoCreateRefTerm = "Request"))
	static EACFCameraTuningResult ResetAllCameraTuningValues(APlayerController* PlayerController, const FACFCameraRequestHandle& Request);

	UFUNCTION(BlueprintPure, Category = "Action Camera|Tuning", meta = (DisplayName = "Get ACF Camera Tuning Status", AutoCreateRefTerm = "Request"))
	static FACFCameraTuningStatus GetCameraTuningStatus(APlayerController* PlayerController, const FACFCameraRequestHandle& Request);
};
