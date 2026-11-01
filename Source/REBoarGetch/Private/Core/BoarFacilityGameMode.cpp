#include "Core/BoarFacilityGameMode.h"
#include "Core/BoarGadgetLoadoutInitializer.h"
#include "Player/BoarPlayerCharacter.h"
#include "Player/BoarPlayerController.h"
#include "EngineUtils.h"
#include "Engine/World.h"

void ABoarFacilityGameMode::StartPlay()
{
	// 施設はStageの勝敗管理へ依存せず、自分のModeでPlayerの準備を統括する。
	const auto Context = BoarGadgetLoadoutInitializer::ResolveContext(GetWorld());
	bool bReady = !bGadgetTest || TestGadgetClass != nullptr;
	for (TActorIterator<ABoarPlayerCharacter> It(GetWorld()); It; ++It)
		bReady &= It->InitializeForPlay(Context);
	Super::StartPlay();
	if (!bReady)
	{
		const FText Reason = NSLOCTEXT("WorldInit", "FacilityFailed", "プレイヤーの装備を準備できませんでした。");
		UE_LOG(LogTemp, Error, TEXT("[WorldInit] Facility player/loadout preparation failed: %s"), *Reason.ToString());
		if (auto* PC = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController()))
			PC->HandleStageInitializationFailed(Reason);
	}
}
