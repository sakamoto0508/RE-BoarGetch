#include "Core/BoarFacilityGameMode.h"
#include "Core/BoarGadgetLoadoutInitializer.h"
#include "Player/BoarPlayerCharacter.h"
#include "Player/BoarPlayerController.h"
#include "EngineUtils.h"
#include "Engine/World.h"

void ABoarFacilityGameMode::StartPlay()
{
	// Facilities have no stage progression; only their player loadout policy differs.
	const auto Context = BoarGadgetLoadoutInitializer::ResolveContext(GetWorld());
	bool bReady = !bGadgetTest || TestGadgetClass != nullptr;
	for (TActorIterator<ABoarPlayerCharacter> It(GetWorld()); It; ++It)
		bReady &= It->InitializeForPlay(Context);
	Super::StartPlay();
	if (!bReady)
	{
		UE_LOG(LogTemp, Error, TEXT("[WorldInit] Facility player/loadout preparation failed."));
		if (auto* PC = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController()))
			PC->HandleStageInitializationFailed(NSLOCTEXT("WorldInit", "FacilityFailed", "プレイヤーの装備を準備できませんでした。"));
	}
}
