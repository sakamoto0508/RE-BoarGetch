#pragma once

#include "CoreMinimal.h"
#include "ACFTransitionTypes.h"
#include "Engine/DeveloperSettings.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BoarLevelTransitionLibrary.generated.h"

class UACFTransitionPreset;

/** ゲーム内のLevel遷移の種類。種類ごとの演出はProject Settings > Game > Boar Level Transitionsで決めます。 */
UENUM(BlueprintType)
enum class EBoarLevelTransitionStyle : uint8
{
	/** ポータルに入ったとき。 */
	Portal,
	/** タイトル→ロビー、Retryなど、その他のメニューからの遷移。 */
	Menu,
	/** ロビーのステージ選択からステージへ出発するとき。 */
	StageDeparture,
	/** ステージ（Result / GameOver / Pause）からロビーへ戻るとき。 */
	ReturnToLobby
};

/** 1種類の遷移の演出。Presetがあればそちらを使い、無ければEffectの既定の見た目を使います。 */
USTRUCT(BlueprintType)
struct FBoarLevelTransitionEntry
{
	GENERATED_BODY()

	/** ACFの演出（番号つき一覧）。Close / Openの両方に使います。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition")
	EACFTransitionEffect Effect = EACFTransitionEffect::CRTPowerOff;

	/** 任意。長さや色まで調整したいときにACF Transition Preset（DataAsset）を指定します。指定時はEffectより優先します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition")
	TSoftObjectPtr<UACFTransitionPreset> Preset;
};

/** Level遷移の種類ごとの演出（Project Settings > Game > Boar Level Transitions。DefaultGame.iniに保存）。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Boar Level Transitions"))
class REBOARGETCH_API UBoarLevelTransitionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBoarLevelTransitionSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	UPROPERTY(Config, EditAnywhere, Category = "Transitions")
	FBoarLevelTransitionEntry Portal;

	UPROPERTY(Config, EditAnywhere, Category = "Transitions")
	FBoarLevelTransitionEntry Menu;

	UPROPERTY(Config, EditAnywhere, Category = "Transitions")
	FBoarLevelTransitionEntry StageDeparture;

	UPROPERTY(Config, EditAnywhere, Category = "Transitions")
	FBoarLevelTransitionEntry ReturnToLobby;

	const FBoarLevelTransitionEntry& GetEntry(EBoarLevelTransitionStyle Style) const;
};

/**
 * OpenLevelの代わりに、ACFのLevel Transition（Close → OpenLevel → Open）で遷移します。
 * ACFのSubsystemが無い・要求が通らないときは、演出なしのOpenLevelで必ず遷移します。
 */
UCLASS()
class REBOARGETCH_API UBoarLevelTransitionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** UGameplayStatics::OpenLevelと同じ引数名（Blueprintのノード差し替えでピンの接続を保つため）。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Level", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "bAbsolute,Options"))
	static void OpenLevelWithTransition(const UObject* WorldContextObject, FName LevelName, EBoarLevelTransitionStyle Style = EBoarLevelTransitionStyle::Menu,
	                                    bool bAbsolute = true, FString Options = FString(TEXT("")));

	/** UGameplayStatics::OpenLevelBySoftObjectPtrと同じ引数名。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Level", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "bAbsolute,Options"))
	static void OpenLevelBySoftObjectPtrWithTransition(const UObject* WorldContextObject, const TSoftObjectPtr<UWorld> Level,
	                                                   EBoarLevelTransitionStyle Style = EBoarLevelTransitionStyle::Menu,
	                                                   bool bAbsolute = true, FString Options = FString(TEXT("")));

	/** 演出を直接指定して遷移します（ポータルなど、配置したActorごとに演出を変えるとき）。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Level", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "bAbsolute,Options"))
	static void OpenLevelWithTransitionEntry(const UObject* WorldContextObject, FName LevelName, const FBoarLevelTransitionEntry& Transition,
	                                         bool bAbsolute = true, FString Options = FString(TEXT("")));

	/** 設定上の演出（Presetを指定していても、Effectの値を返します）。 */
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Level")
	static EACFTransitionEffect GetTransitionEffect(EBoarLevelTransitionStyle Style);
};
