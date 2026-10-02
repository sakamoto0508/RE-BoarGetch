#include "UI/BoarLoadoutPresenter.h"
#include "Component/GadgetComponent.h"
#include "Core/BoarGameInstance.h"
#include "Gadget/BoarGadgetSubsystem.h"
#include "Player/BoarPlayerCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Gadget/GadgetBase.h"
#include "GadgetDataAsset.h"
namespace
{
const UGadgetDataAsset *Definition(TSubclassOf<AGadgetBase> Class)
{
	return Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
}
FText GadgetName(TSubclassOf<AGadgetBase> Class)
{
	if (!Class)
		return FText::FromString(TEXT("空き"));
	const auto *Def = Definition(Class);
	return Def && !Def->DisplayName.IsEmpty() ? Def->DisplayName : FText::FromString(TEXT("名称未設定"));
}
} // namespace

bool UBoarLoadoutPresenter::Initialize(APlayerController *Owner, bool bRequestedLabContext)
{
	Shutdown();
	const auto *Player = Owner ? Cast<ABoarPlayerCharacter>(Owner->GetPawn()) : nullptr;
	Gadgets = Player ? Player->GetGadgetComponent() : nullptr;
	const auto *Instance = Owner && Owner->GetWorld() ? Owner->GetWorld()->GetGameInstance<UBoarGameInstance>() : nullptr;
	Progress = Instance ? Instance->GetSubsystem<UBoarGadgetSubsystem>() : nullptr;
	bLabContext = bRequestedLabContext && Progress && Progress->IsInGadgetLab();
	LabMode = 0;
	SelectedSlot = 0;
	if (!Gadgets || !Progress)
		return false;
	Gadgets->OnGadgetLoadoutChanged.AddUniqueDynamic(this, &UBoarLoadoutPresenter::HandleSlotsChanged);
	Progress->OnGadgetsUnlocked.AddUniqueDynamic(this, &UBoarLoadoutPresenter::HandleCandidatesChanged);
	RebuildCatalog();
	return true;
}
void UBoarLoadoutPresenter::Shutdown()
{
	if (Gadgets)
		Gadgets->OnGadgetLoadoutChanged.RemoveDynamic(this, &UBoarLoadoutPresenter::HandleSlotsChanged);
	if (Progress)
		Progress->OnGadgetsUnlocked.RemoveDynamic(this, &UBoarLoadoutPresenter::HandleCandidatesChanged);
	Gadgets = nullptr;
	Progress = nullptr;
	Candidates.Reset();
}
void UBoarLoadoutPresenter::HandleSlotsChanged()
{
	OnSlotsChanged.Broadcast();
}
void UBoarLoadoutPresenter::HandleCandidatesChanged()
{
	RebuildCatalog();
	OnCandidatesChanged.Broadcast();
}
int32 UBoarLoadoutPresenter::GetSlotCount() const
{
	return UGadgetComponent::GetGadgetSlotCount();
}
void UBoarLoadoutPresenter::RebuildCatalog()
{
	Candidates.Reset();
	Candidates.Add(nullptr);
	if (!Progress)
		return;
	TSet<FName> Seen;
	for (const auto &Class : Progress->GetCatalog())
	{
		const auto *Def = Definition(Class);
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract) || !Def || Def->GadgetId.IsNone() || Seen.Contains(Def->GadgetId))
			continue;
		Seen.Add(Def->GadgetId);
		Candidates.Add(Class);
	}
}
FBoarLoadoutEntryViewData UBoarLoadoutPresenter::BuildDetails(TSubclassOf<AGadgetBase> Class) const
{
	FBoarLoadoutEntryViewData Data;
	const auto *Def = Definition(Class);
	Data.Name = GadgetName(Class);
	Data.Role = Def ? Def->RoleText : FText::GetEmpty();
	Data.Description = Def ? Def->Description : FText::FromString(TEXT("このスロットには何も装備しません。"));
	Data.Icon = Def ? Def->DisplayIcon.Get() : nullptr;
	return Data;
}
FBoarLoadoutEntryViewData UBoarLoadoutPresenter::GetSlot(int32 Index) const
{
	auto Data = BuildDetails(Gadgets ? Gadgets->GetGadgetSlotClass(Index) : nullptr);
	const TCHAR *Directions[] = {TEXT("↑"), TEXT("←"), TEXT("→"), TEXT("↓")};
	if (Index >= 0 && Index < GetSlotCount())
		Data.Label = FText::Format(FText::FromString(TEXT("Slot {0} {1}  /  {2}")), FText::AsNumber(Index + 1),
								   FText::FromString(Directions[Index]), Data.Name);
	Data.bSelected = Index == SelectedSlot;
	return Data;
}
FBoarLoadoutEntryViewData UBoarLoadoutPresenter::GetCandidate(int32 Index) const
{
	if (!Progress || !Candidates.IsValidIndex(Index))
		return {};
	auto Data = BuildDetails(Candidates[Index]);
	const auto *Def = Definition(Candidates[Index]);
	FText Label = Index == 0 ? FText::FromString(LabMode == 2	? TEXT("Test対象を解除")
												 : LabMode == 1 ? TEXT("解放したいGadgetを選択")
																: TEXT("スロットを空にする"))
							 : GadgetName(Candidates[Index]);
	if (Def && !Progress->IsGadgetUnlocked(Def->GadgetId))
		Label = FText::Format(FText::FromString(TEXT("LOCKED  {0}  /  Coin {1} / {2}")), Label,
							  FText::AsNumber(Progress->GetSpecialCoinCount()), FText::AsNumber(Def->RequiredSpecialCoinCount));
	Data.Label = Label;
	Data.bSelected = Def && LabMode == 2 && Def->GadgetId == Progress->GetSelectedTestGadget();
	return Data;
}
int32 UBoarLoadoutPresenter::ChooseSlot(int32 Index)
{
	if (Index < 0 || Index >= GetSlotCount())
		return INDEX_NONE;
	SelectedSlot = Index;
	int32 CandidateIndex = Gadgets ? Candidates.IndexOfByKey(Gadgets->GetGadgetSlotClass(Index)) : 0;
	return CandidateIndex == INDEX_NONE ? 0 : CandidateIndex;
}
bool UBoarLoadoutPresenter::ChooseLabMode(int32 Index)
{
	if (!bLabContext || Index < 0 || Index > 2)
		return false;
	LabMode = Index;
	return true;
}
FText UBoarLoadoutPresenter::GetLabModeStatus() const
{
	return FText::FromString(LabMode == 1	? TEXT("累計Coin条件を満たすGadgetを選択すると解放します。Coinは消費しません。")
							 : LabMode == 2 ? TEXT("装備Slotとは別にTest対象を選択します。")
											: TEXT("Slotを選び、装備候補を決定してください。"));
}
FBoarLoadoutActionResult UBoarLoadoutPresenter::ChooseCandidate(int32 Index)
{
	FBoarLoadoutActionResult Result;
	auto SetStatus = [&Result](const FText &Text, bool bError = false)
	{
		Result.Status = Text;
		Result.bError = bError;
	};
	if (!Gadgets || !Progress || !Candidates.IsValidIndex(Index))
		return Result;
	const auto *Def = Definition(Candidates[Index]);
	if (bLabContext && LabMode == 1)
	{
		const bool bOK = Def && Progress->UnlockGadgetInLab(Def->GadgetId);
		SetStatus(FText::FromString(bOK ? TEXT("解放・保存しました。Coinは消費しません。")
										: TEXT("解放済み、必要Coin不足、または保存失敗です。")),
				  !bOK);
		return Result;
	}
	if (bLabContext && LabMode == 2)
	{
		const bool bOK = Progress->SelectTestGadget(Def ? Def->GadgetId : NAME_None);
		Result.bRefreshCandidates = true;
		SetStatus(FText::FromString(!bOK					  ? TEXT("未解放のGadgetはTestできません。")
									: !Def					  ? TEXT("Test対象を解除しました。")
									: Def->TestLevel.IsNull() ? TEXT("Test対象を選択しました。専用Levelは未登録です。")
															  : TEXT("Test対象を選択しました。画面を閉じてTest Portalへ進んでください。")),
				  !bOK);
		return Result;
	}
	if (Candidates[Index] && (!Def || !Progress->IsGadgetUnlocked(Def->GadgetId)))
		return Result;
	if (!Gadgets->SetGadgetSlot(SelectedSlot, Candidates[Index]))
	{
		SetStatus(FText::FromString(TEXT("装備を変更できませんでした。")), true);
		return Result;
	}
	const bool bSaved = Gadgets->WasLastLoadoutSaveSuccessful();

	SetStatus(FText::FromString(Gadgets->IsTemporaryLoadout() ? TEXT("Test用の装備を変更しました。通常の装備設定は維持されます。")
								: bSaved					  ? TEXT("装備を変更・保存しました。")
										 : TEXT("装備は変更されましたが保存に失敗しました。同じ候補を選ぶと再試行します。")),
			  !bSaved);
	Result.bFocusSlots = true;
	return Result;
}
