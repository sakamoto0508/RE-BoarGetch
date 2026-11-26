#include "Core/BoarFacilityGameMode.h"
#include "REBoarGetch.h"
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
		UE_LOG(LogREBoarGetch, Error, TEXT("[WorldInit] 施設のPlayer・装備構成の準備に失敗しました: %s"), *Reason.ToString());

		if (auto* PC = Cast<ABoarPlayerController>(GetWorld()->GetFirstPlayerController()))
			PC->HandleStageInitializationFailed(Reason);
	}
}
