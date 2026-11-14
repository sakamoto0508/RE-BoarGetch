#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ACFSequencerTypes.h"
#include "ACFSequencerBlueprintLibrary.generated.h"

class ALevelSequenceActor;
class APlayerController;
class UACFSequencerCameraSubsystem;

/** Sequencer 連携の Blueprint ノード（PlayerController / Level Sequence Actor からサブシステムを解決する）。 */
UCLASS()
class ACTIONCAMERAFRAMEWORKSEQUENCER_API UACFSequencerBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** イベント（On Sequencer Camera Started / Ended）を Bind するときに使う。ゲームワールド以外では null。 */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Sequencer", meta = (WorldContext = "WorldContextObject", DisplayName = "Get ACF Sequencer Camera Subsystem"))
	static UACFSequencerCameraSubsystem* GetSequencerCameraSubsystem(const UObject* WorldContextObject);

	/** このプレイヤーの画面を Sequencer がどの段階で握っているか、ACF の出力がどうなっているか。 */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Sequencer", meta = (DisplayName = "Get ACF Sequencer Camera State"))
	static FACFSequencerCameraState GetSequencerCameraState(const APlayerController* PlayerController);

	/** Sequencer の Camera Cut がこのプレイヤーの画面を握っているか（ブレンド中を含む）。 */
	UFUNCTION(BlueprintPure, Category = "Action Camera|Sequencer", meta = (DisplayName = "Is Sequencer Controlling ACF Camera"))
	static bool IsSequencerControllingCamera(const APlayerController* PlayerController);

	/** プレイヤー単位で設定を上書きする（Clear するまで有効）。 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequencer", meta = (DisplayName = "Set ACF Sequencer Options For Player"))
	static void SetSequencerOptionsForPlayer(APlayerController* PlayerController, const FACFSequencerCameraOptions& Options);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequencer", meta = (DisplayName = "Clear ACF Sequencer Options For Player"))
	static void ClearSequencerOptionsForPlayer(APlayerController* PlayerController);

	/** Level Sequence Actor 単位で設定を上書きする（プレイヤー単位より優先。Play の前に呼ぶ）。 */
	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequencer", meta = (DisplayName = "Set ACF Sequencer Options For Sequence"))
	static void SetSequencerOptionsForSequence(ALevelSequenceActor* SequenceActor, const FACFSequencerCameraOptions& Options);

	UFUNCTION(BlueprintCallable, Category = "Action Camera|Sequencer", meta = (DisplayName = "Clear ACF Sequencer Options For Sequence"))
	static void ClearSequencerOptionsForSequence(ALevelSequenceActor* SequenceActor);
};
