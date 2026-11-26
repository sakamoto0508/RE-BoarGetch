#include "Save/BoarSaveGame.h"
#include "REBoarGetch.h"

void UBoarSaveGame::FreezeRunResult(FStageRunData& Run) const
{
	if (Run.bResultFrozen)
		return;

	Run.NewBoarUniqueIds.Reset();
	Run.NewSpecialCoinIds.Reset();

	// 登録対象はClear確定時の収容個体だけ。捕獲履歴（解放された個体を含む）からは判定しない。
	if (!Run.bHousedSnapshotTaken)
		UE_LOG(LogREBoarGetch, Warning, TEXT("[Save] Run %s に収容済みBoarの記録が無いため、Boarを登録しません。"), *Run.StageId.ToString());

	const TArray<FName> Housed = Run.bHousedSnapshotTaken ? Run.GetHousedIdsOldestFirst() : TArray<FName>();

	// Result表示用に新しい順（同時捕獲はID昇順）で並べる。
	for (int32 I = Housed.Num() - 1; I >= 0; --I)
	{
		const FName Id = Housed[I];
		if (!Id.IsNone() && !CapturedBoarUniqueIds.Contains(Id))
			Run.NewBoarUniqueIds.AddUnique(Id);
	}

	for (FName Id : Run.SpecialCoinIds)
	{
		if (!Id.IsNone() && !SpecialCoinIds.Contains(Id))
			Run.NewSpecialCoinIds.AddUnique(Id);
	}

	Run.bResultFrozen = true;
}

bool UBoarSaveGame::MergeClearedRun(const FStageRunData& Run)
{
	if (!Run.bResultFrozen || Run.StageId.IsNone())
		return false;
	// Clear時の収容個体だけを登録し、最近捕獲順の末尾（最新）へ移す。未収容の個体の登録・順序は変えない。
	if (Run.bHousedSnapshotTaken)
		for (FName Id : Run.GetHousedIdsOldestFirst())
			if (!Id.IsNone())
			{
				CapturedBoarUniqueIds.Add(Id);
				CapturedBoarHistory.Remove(Id);
				CapturedBoarHistory.Add(Id);
			}
	for (FName Id : Run.SpecialCoinIds)
	{
		if (!Id.IsNone())
			SpecialCoinIds.Add(Id);
	}

	ClearedStageIds.Add(Run.StageId);
	return true;
}

bool UBoarSaveGame::SetArchiveDisplays(const TArray<FName>& Ids)
{
	if (Ids.Num() != ArchiveDisplaySlotCount)
		return false;

	TSet<FName> Seen;

	for (FName Id : Ids)
	{
		if (!Id.IsNone() && !CapturedBoarUniqueIds.Contains(Id))
			return false;
	}

	for (FName Id : Ids)
		if (!Id.IsNone())
	{
		if (Seen.Contains(Id))
			return false;

		Seen.Add(Id);
	}

	ArchiveDisplayBoarIds = Ids;
	bHasSavedArchiveDisplays = true;
	return true;
}

bool UBoarSaveGame::AssignArchiveDisplay(int32 Slot, FName Id)
{
	// 重複展示をWidget側で調整せず、保存候補側でSwapと検証を一括して行う。
	if (Slot < 0 || Slot >= ArchiveDisplaySlotCount || (!Id.IsNone() && !CapturedBoarUniqueIds.Contains(Id))) return
		false;
	TArray<FName> Ids = ResolveArchiveDisplays();
	const int32 Other = Id.IsNone() ? INDEX_NONE : Ids.Find(Id);
	if (Other != INDEX_NONE && Other != Slot)
		Swap(Ids[Slot], Ids[Other]);
	else
		Ids[Slot] = Id;

	return SetArchiveDisplays(Ids);
}

TArray<FName> UBoarSaveGame::ResolveArchiveDisplays() const
{
	TArray<FName> Result;
	Result.SetNum(ArchiveDisplaySlotCount);

	if (bHasSavedArchiveDisplays)
	{
		for (int32 I = 0; I < ArchiveDisplaySlotCount; ++I)
			if (ArchiveDisplayBoarIds.IsValidIndex(I) && CapturedBoarUniqueIds.Contains(ArchiveDisplayBoarIds[I]))
				Result[I] = ArchiveDisplayBoarIds[I];

		return Result;
	}

	const TArray<FName> Recent = GetRecentCapturedBoarIds();
	for (int32 Slot = 0; Slot < ArchiveDisplaySlotCount && Recent.IsValidIndex(Slot); ++Slot)
		Result[Slot] = Recent[Slot];

	return Result;
}

TArray<FName> UBoarSaveGame::GetRecentCapturedBoarIds() const
{
	TArray<FName> Result;

	for (int32 I = CapturedBoarHistory.Num() - 1; I >= 0; --I)
		if (!CapturedBoarHistory[I].IsNone() && CapturedBoarUniqueIds.Contains(CapturedBoarHistory[I]))
			Result.AddUnique(CapturedBoarHistory[I]);
	// 順序情報のない旧セーブの登録個体は、消さずに古い履歴としてID昇順で後ろへ並べる。
	TArray<FName> Unordered;

	for (const FName Id : CapturedBoarUniqueIds)
		if (!Id.IsNone() && !Result.Contains(Id))
			Unordered.Add(Id);

	Unordered.Sort([](const FName A, const FName B) { return A.LexicalLess(B); });
	Result.Append(Unordered);
	return Result;
}
