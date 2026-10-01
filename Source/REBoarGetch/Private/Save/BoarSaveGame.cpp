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
	for (FName Id : Run.CapturedBoarUniqueIds) if (!Id.IsNone())
	{
		CapturedBoarUniqueIds.Add(Id);
		CapturedBoarHistory.Remove(Id);
		CapturedBoarHistory.Add(Id);
	}
	for (FName Id : Run.SpecialCoinIds) if (!Id.IsNone()) SpecialCoinIds.Add(Id);
	ClearedStageIds.Add(Run.StageId);
	return true;
}

bool UBoarSaveGame::SetArchiveDisplays(const TArray<FName>& Ids)
{
	if (Ids.Num() != 3) return false;
	for (FName Id : Ids) if (!Id.IsNone() && !CapturedBoarUniqueIds.Contains(Id)) return false;
	ArchiveDisplayBoarIds = Ids;
	bHasSavedArchiveDisplays = true;
	return true;
}

TArray<FName> UBoarSaveGame::ResolveArchiveDisplays() const
{
	TArray<FName> Result;
	Result.SetNum(3);
	if (bHasSavedArchiveDisplays)
	{
		for (int32 I = 0; I < 3; ++I)
			if (ArchiveDisplayBoarIds.IsValidIndex(I) && CapturedBoarUniqueIds.Contains(ArchiveDisplayBoarIds[I]))
				Result[I] = ArchiveDisplayBoarIds[I];
		return Result;
	}
	int32 Slot = 0;
	for (int32 I = CapturedBoarHistory.Num() - 1; I >= 0 && Slot < 3; --I)
		if (CapturedBoarUniqueIds.Contains(CapturedBoarHistory[I]) && !Result.Contains(CapturedBoarHistory[I]))
			Result[Slot++] = CapturedBoarHistory[I];
	return Result;
}
