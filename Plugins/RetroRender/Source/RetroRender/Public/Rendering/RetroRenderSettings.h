#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Rendering/RetroRenderTypes.h"
#include "Materials/MaterialParameterCollection.h"
#include "RetroRenderSettings.generated.h"

// Project Settings > Plugins > Retro Render. Stored in Config/DefaultGame.ini.
UCLASS(config=Game, defaultconfig, meta=(DisplayName="Retro Render"))
class RETRORENDER_API URetroRenderSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	URetroRenderSettings();
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	// Every game world (PIE and packaged game) gets a Retro Render actor at BeginPlay unless the level already has one
	// (a level's own Retro Render Settings / Compare actor always wins) or the map is excluded below.
	UPROPERTY(config, EditAnywhere, Category="Auto Apply") bool bAutoApplyInGame = true;
	UPROPERTY(config, EditAnywhere, Category="Auto Apply", meta=(EditCondition="bAutoApplyInGame")) ERetroRenderPreset DefaultPreset = ERetroRenderPreset::PS1;
	UPROPERTY(config, EditAnywhere, Category="Auto Apply", meta=(EditCondition="bAutoApplyInGame")) bool bDefaultCRTOverlay = true;
	// Maps that stay untouched (e.g. a title screen rendered as a plain menu).
	UPROPERTY(config, EditAnywhere, Category="Auto Apply", meta=(AllowedClasses="/Script/Engine.World")) TArray<FSoftObjectPath> ExcludedMaps;
	// MPC_Retro に加えて、ジオメトリ用スカラー（RetroVertexSnap など）を書き込む MPC。
	// マテリアルが既に UE の上限（2 個）まで MPC を使っていて MPC_Retro を足せない場合に、
	// そのマテリアルが使っている MPC に同名スカラーを追加して、ここに登録する。存在しないものは無視される。
	UPROPERTY(config, EditAnywhere, Category="Parameter Collections") TArray<TSoftObjectPtr<UMaterialParameterCollection>> AdditionalParameterCollections;
	// Editor: when a level is opened, notify if it uses project materials without the retro geometry patch.
	UPROPERTY(config, EditAnywhere, Category="Editor") bool bNotifyUnpatchedMaterials = true;
};
