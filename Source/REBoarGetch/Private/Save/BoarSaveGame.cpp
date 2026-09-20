#include "Save/BoarSaveGame.h"

void UBoarSaveGame::FreezeRunResult(FStageRunData& Run) const
{
	if (Run.bResultFrozen) return;
	Run.NewBoarUniqueIds.Reset();
	Run.NewSpecialCoinIds.Reset();
	for (FName Id : Run.CapturedBoarUniqueIds)
		if (!Id.IsNone() && !CapturedBoarUniqueIds.Contains(Id)) Run.NewBoarUniqueIds.AddUnique(Id);
	for (FName Id : Run.SpecialCoinIds)
		if (!Id.IsNone() && !SpecialCoinIds.Contains(Id)) Run.NewSpecialCoinIds.AddUnique(Id);
	Run.bResultFrozen = true;
}

bool UBoarSaveGame::MergeClearedRun(const FStageRunData& Run)
{
	if (!Run.bResultFrozen || Run.StageId.IsNone()) return false;
	for (FName Id : Run.CapturedBoarUniqueIds) if (!Id.IsNone()) CapturedBoarUniqueIds.Add(Id);
	for (FName Id : Run.SpecialCoinIds) if (!Id.IsNone()) SpecialCoinIds.Add(Id);
	ClearedStageIds.Add(Run.StageId);
	return true;
}
