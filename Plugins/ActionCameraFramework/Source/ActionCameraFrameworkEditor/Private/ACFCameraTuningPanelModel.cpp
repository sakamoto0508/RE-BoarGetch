#include "ACFCameraTuningPanelModel.h"
#include "ACFCameraDirector.h"
#include "ACFCameraSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Editor.h"

namespace ACFTuningPanelImpl
{
	static bool IsLiveTunableWorld(const UWorld* World)
	{
		return World && FACFCameraTuningPanelModel::IsTunableWorldType(World->WorldType) && !World->bIsTearingDown;
	}

	static bool HasPlayer(const UWorld* World, const ULocalPlayer* Player)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return Player && GameInstance && GameInstance->GetLocalPlayers().Contains(Player);
	}

	static FString Explain(EACFCameraTuningResult Result)
	{
		switch (Result)
		{
		case EACFCameraTuningResult::Ok: return TEXT("done");
		case EACFCameraTuningResult::NotAvailable: return TEXT("runtime tuning is not compiled into this build");
		case EACFCameraTuningResult::NoPlayer: return TEXT("no PIE player selected (start PIE and pick a world / player)");
		case EACFCameraTuningResult::NoRequest: return TEXT("the request does not exist any more (select one again)");
		case EACFCameraTuningResult::NotTuning: return TEXT("press Start Tuning first (or switch back to the tuned values)");
		case EACFCameraTuningResult::NoNode: return TEXT("the selected node does not have this parameter");
		case EACFCameraTuningResult::InvalidValue: return TEXT("rejected: NaN / infinity or outside the valid range; nothing was changed");
		case EACFCameraTuningResult::InvalidParameter: return TEXT("unknown parameter");
		default: return TEXT("?");
		}
	}
}

void FACFCameraTuningPanelModel::GetPIEWorlds(TArray<FWorldEntry>& Out)
{
	Out.Reset();
	if (!GEngine)
	{
		return;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (World && IsTunableWorldType(Context.WorldType) && ACFTuningPanelImpl::IsLiveTunableWorld(World))
		{
			FWorldEntry& Entry = Out.AddDefaulted_GetRef();
			Entry.World = World;
			const TCHAR* Net = World->GetNetMode() == NM_Client ? TEXT("client") : World->GetNetMode() == NM_ListenServer ? TEXT("listen server")
				: World->GetNetMode() == NM_DedicatedServer ? TEXT("dedicated server") : TEXT("standalone");
			Entry.Label = FString::Printf(TEXT("PIE %d: %s (%s)"), Context.PIEInstance, *World->GetMapName(), Net);
		}
	}
}

void FACFCameraTuningPanelModel::GetPlayers(UWorld* InWorld, TArray<FPlayerEntry>& Out)
{
	Out.Reset();
	const UGameInstance* GameInstance = ACFTuningPanelImpl::IsLiveTunableWorld(InWorld) ? InWorld->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		return;
	}
	const TArray<ULocalPlayer*>& Players = GameInstance->GetLocalPlayers();
	for (int32 Index = 0; Index < Players.Num(); ++Index)
	{
		if (ULocalPlayer* LocalPlayer = Players[Index])
		{
			FPlayerEntry& Entry = Out.AddDefaulted_GetRef();
			Entry.Player = LocalPlayer;
			Entry.Index = Index;
			const APlayerController* Controller = LocalPlayer->GetPlayerController(InWorld);
			Entry.Label = FString::Printf(TEXT("Player %d (%s)"), Index, Controller ? *Controller->GetName() : TEXT("no controller"));
		}
	}
}

void FACFCameraTuningPanelModel::SelectWorld(UWorld* InWorld)
{
	if (bShowingOriginal)
	{
		ToggleCompare(); // give the previous request its tuned values back before leaving it
	}
	World = InWorld;
	Player.Reset();
	Request.Invalidate();
	NodeIndex = INDEX_NONE;
	NodeClass = NAME_None;
	ClearCompare();
}

void FACFCameraTuningPanelModel::SelectPlayer(ULocalPlayer* InPlayer)
{
	if (bShowingOriginal)
	{
		ToggleCompare();
	}
	Player = InPlayer;
	Request.Invalidate();
	NodeIndex = INDEX_NONE;
	NodeClass = NAME_None;
	ClearCompare();
}

void FACFCameraTuningPanelModel::SelectRequest(FACFCameraRequestHandle Handle)
{
	if (bShowingOriginal && Handle != Request)
	{
		ToggleCompare();
	}
	if (Handle != Request)
	{
		ClearCompare();
	}
	Request = Handle;
	NodeIndex = INDEX_NONE;
	NodeClass = NAME_None;
}

void FACFCameraTuningPanelModel::SelectNode(int32 InNodeIndex, FName InNodeClass)
{
	NodeIndex = InNodeIndex;
	NodeClass = InNodeClass;
}

void FACFCameraTuningPanelModel::SetDirectorForTesting(UACFCameraDirector* Director)
{
	TestDirector = Director;
}

UACFCameraDirector* FACFCameraTuningPanelModel::ResolveDirector() const
{
	if (TestDirector.IsValid())
	{
		return TestDirector.Get();
	}
	UWorld* W = World.Get();
	ULocalPlayer* LocalPlayer = Player.Get();
	if (!ACFTuningPanelImpl::IsLiveTunableWorld(W) || !ACFTuningPanelImpl::HasPlayer(W, LocalPlayer))
	{
		return nullptr;
	}
	UACFCameraSubsystem* Subsystem = LocalPlayer->GetSubsystem<UACFCameraSubsystem>();
	return Subsystem ? Subsystem->GetDirector() : nullptr;
}

bool FACFCameraTuningPanelModel::Validate()
{
	bool bChanged = false;
	if (!TestDirector.IsValid())
	{
		if ((World.IsValid() || !World.IsExplicitlyNull()) && !ACFTuningPanelImpl::IsLiveTunableWorld(World.Get()))
		{
			// PIE ended (or the World is going away): forget everything, the old objects are not touched again.
			World.Reset();
			Player.Reset();
			Request.Invalidate();
			NodeIndex = INDEX_NONE;
			CompareStash.Reset();
			bShowingOriginal = false;
			return true;
		}
		if (!Player.IsExplicitlyNull() && !ACFTuningPanelImpl::HasPlayer(World.Get(), Player.Get()))
		{
			Player.Reset();
			Request.Invalidate();
			NodeIndex = INDEX_NONE;
			ClearCompare();
			bChanged = true;
		}
	}
	UACFCameraDirector* Director = ResolveDirector();
	if (Request.IsValid())
	{
		TArray<FACFCameraRequestInfo> Requests;
		if (Director)
		{
			Director->GetRequestInfos(Requests);
		}
		const FACFCameraRequestInfo* Found = Requests.FindByPredicate([this](const FACFCameraRequestInfo& Info) { return Info.Handle == Request; });
		if (!Found)
		{
			Request.Invalidate();
			NodeIndex = INDEX_NONE;
			ClearCompare();
			bChanged = true;
		}
		else if (bShowingOriginal && !Found->bTuning)
		{
			ClearCompare(); // tuning was stopped elsewhere: nothing to switch back to
			bChanged = true;
		}
	}
	if (NodeIndex != INDEX_NONE && (!Director || !NodeMatches(*Director)))
	{
		NodeIndex = INDEX_NONE;
		NodeClass = NAME_None;
		bChanged = true;
	}
	return bChanged;
}

bool FACFCameraTuningPanelModel::NodeMatches(UACFCameraDirector& Director) const
{
	TArray<FACFCameraTuningNodeInfo> Nodes;
	Director.GetRequestNodes(Request, Nodes);
	return Nodes.ContainsByPredicate([this](const FACFCameraTuningNodeInfo& Node) { return Node.NodeIndex == NodeIndex && Node.NodeClass == NodeClass; });
}

void FACFCameraTuningPanelModel::GetRequests(TArray<FACFCameraRequestInfo>& Out) const
{
	Out.Reset();
	if (UACFCameraDirector* Director = ResolveDirector())
	{
		Director->GetRequestInfos(Out);
	}
}

void FACFCameraTuningPanelModel::GetNodes(TArray<FACFCameraTuningNodeInfo>& Out) const
{
	Out.Reset();
	UACFCameraDirector* Director = ResolveDirector();
	if (Director && Request.IsValid())
	{
		Director->GetRequestNodes(Request, Out);
	}
}

void FACFCameraTuningPanelModel::GetParameterRows(TArray<FParameterRow>& Out) const
{
	Out.Reset();
	UACFCameraDirector* Director = ResolveDirector();
	if (!Director || !Request.IsValid())
	{
		return;
	}
	TArray<FACFCameraTuningNodeInfo> Nodes;
	Director->GetRequestNodes(Request, Nodes);
	const FACFCameraTuningNodeInfo* Node = Nodes.FindByPredicate([this](const FACFCameraTuningNodeInfo& Info) { return Info.NodeIndex == NodeIndex && Info.NodeClass == NodeClass; });
	if (Node)
	{
		for (const EACFCameraTuningParameter Parameter : Node->Parameters)
		{
			FParameterRow& Row = Out.AddDefaulted_GetRef();
			Row.Parameter = Parameter;
			Row.bAvailable = Director->GetTuningValue(Request, Parameter, NodeIndex, Row.Value) == EACFCameraTuningResult::Ok;
			ACFCameraTuning::GetValueRange(Parameter, Row.Min, Row.Max);
		}
	}
	for (const EACFCameraTuningParameter Parameter : { EACFCameraTuningParameter::FollowDamping, EACFCameraTuningParameter::FieldOfView,
		EACFCameraTuningParameter::CameraOffset, EACFCameraTuningParameter::CollisionRadius })
	{
		const FString Reason = Director->GetTuningUnavailableReason(Request, Parameter);
		if (!Reason.IsEmpty())
		{
			FParameterRow& Row = Out.AddDefaulted_GetRef();
			Row.Parameter = Parameter;
			Row.Reason = Reason;
		}
	}
}

FACFCameraTuningStatus FACFCameraTuningPanelModel::GetStatus() const
{
	UACFCameraDirector* Director = ResolveDirector();
	return Director && Request.IsValid() ? Director->GetTuningStatus(Request) : FACFCameraTuningStatus();
}

EACFCameraTuningResult FACFCameraTuningPanelModel::Report(EACFCameraTuningResult Result, const FString& What)
{
	LastMessage = FString::Printf(TEXT("%s: %s"), *What, *ACFTuningPanelImpl::Explain(Result));
	return Result;
}

EACFCameraTuningResult FACFCameraTuningPanelModel::Start()
{
	UACFCameraDirector* Director = ResolveDirector();
	if (!Director)
	{
		return Report(EACFCameraTuningResult::NoPlayer, TEXT("Start Tuning"));
	}
	if (!Request.IsValid())
	{
		return Report(EACFCameraTuningResult::NoRequest, TEXT("Start Tuning"));
	}
	const EACFCameraTuningResult Result = Director->StartTuning(Request);
	if (Result == EACFCameraTuningResult::Ok)
	{
		// Phase 7A-3: record the asset values right away; they are the base the conflict check compares with.
		TArray<FACFCameraRequestInfo> Infos;
		Director->GetRequestInfos(Infos);
		if (const FACFCameraRequestInfo* Info = Infos.FindByPredicate([this](const FACFCameraRequestInfo& Entry) { return Entry.Handle == Request; }))
		{
			ChangeSet.Capture(*Director, Request, MakeSourceKey(Request), MakeSourceLabel(*Info));
		}
	}
	return Report(Result, TEXT("Start Tuning"));
}

EACFCameraTuningResult FACFCameraTuningPanelModel::Stop()
{
	UACFCameraDirector* Director = ResolveDirector();
	if (!Director)
	{
		return Report(EACFCameraTuningResult::NoPlayer, TEXT("Stop Tuning"));
	}
	ClearCompare(); // stopping discards the tuned values anyway
	const EACFCameraTuningResult Result = Request.IsValid() ? Director->StopTuning(Request) : EACFCameraTuningResult::NoRequest;
	if (Result == EACFCameraTuningResult::Ok)
	{
		ChangeSet.Discard(MakeSourceKey(Request)); // Phase 7A-3: "discard" also means: nothing of it is offered for the asset
	}
	return Report(Result, TEXT("Stop Tuning"));
}

EACFCameraTuningResult FACFCameraTuningPanelModel::SetValue(EACFCameraTuningParameter Parameter, const FVector& Value)
{
	const FString What = FString::Printf(TEXT("Set %s"), *ACFCameraTuning::GetParameterName(Parameter));
	UACFCameraDirector* Director = ResolveDirector();
	if (!Director)
	{
		return Report(EACFCameraTuningResult::NoPlayer, What);
	}
	if (!Request.IsValid())
	{
		return Report(EACFCameraTuningResult::NoRequest, What);
	}
	if (NodeIndex == INDEX_NONE || !NodeMatches(*Director))
	{
		return Report(EACFCameraTuningResult::NoNode, What);
	}
	if (bShowingOriginal)
	{
		return Report(EACFCameraTuningResult::NotTuning, What);
	}
	return Report(Director->SetTuningValue(Request, Parameter, Value, NodeIndex), What);
}

EACFCameraTuningResult FACFCameraTuningPanelModel::SetComponent(EACFCameraTuningParameter Parameter, int32 Axis, double Value)
{
	UACFCameraDirector* Director = ResolveDirector();
	FACFCameraTuningValue Current;
	if (!Director || !Request.IsValid() || NodeIndex == INDEX_NONE || Director->GetTuningValue(Request, Parameter, NodeIndex, Current) != EACFCameraTuningResult::Ok)
	{
		return SetValue(Parameter, FVector::ZeroVector); // reports why (no player / request / node)
	}
	FVector NewValue = Current.Value;
	NewValue[FMath::Clamp(Axis, 0, 2)] = Value;
	return SetValue(Parameter, NewValue);
}

EACFCameraTuningResult FACFCameraTuningPanelModel::ResetParameter(EACFCameraTuningParameter Parameter)
{
	const FString What = FString::Printf(TEXT("Reset %s"), *ACFCameraTuning::GetParameterName(Parameter));
	UACFCameraDirector* Director = ResolveDirector();
	if (!Director)
	{
		return Report(EACFCameraTuningResult::NoPlayer, What);
	}
	if (NodeIndex == INDEX_NONE || !NodeMatches(*Director))
	{
		return Report(EACFCameraTuningResult::NoNode, What);
	}
	if (bShowingOriginal)
	{
		return Report(EACFCameraTuningResult::NotTuning, What);
	}
	return Report(Request.IsValid() ? Director->ResetTuningValue(Request, Parameter, NodeIndex) : EACFCameraTuningResult::NoRequest, What);
}

EACFCameraTuningResult FACFCameraTuningPanelModel::ResetAll()
{
	UACFCameraDirector* Director = ResolveDirector();
	if (!Director)
	{
		return Report(EACFCameraTuningResult::NoPlayer, TEXT("Reset All"));
	}
	const EACFCameraTuningResult Result = Request.IsValid() ? Director->ResetAllTuningValues(Request) : EACFCameraTuningResult::NoRequest;
	if (Result == EACFCameraTuningResult::Ok)
	{
		ClearCompare(); // everything is the original now: nothing to switch back to
	}
	return Report(Result, TEXT("Reset All"));
}

EACFCameraTuningResult FACFCameraTuningPanelModel::ToggleCompare()
{
	UACFCameraDirector* Director = ResolveDirector();
	if (!Director || !Request.IsValid())
	{
		ClearCompare();
		return Report(Director ? EACFCameraTuningResult::NoRequest : EACFCameraTuningResult::NoPlayer, TEXT("Compare"));
	}
	if (bShowingOriginal)
	{
		// Back to the tuned values (they are written again; the rig state is kept as for any Set).
		EACFCameraTuningResult Result = EACFCameraTuningResult::Ok;
		for (const FStashed& Stashed : CompareStash)
		{
			const EACFCameraTuningResult One = Director->SetTuningValue(Request, Stashed.Parameter, Stashed.Value, Stashed.NodeIndex);
			Result = One != EACFCameraTuningResult::Ok ? One : Result;
		}
		ClearCompare();
		return Report(Result, TEXT("Show tuned values"));
	}
	if (!Director->GetTuningStatus(Request).bTuning)
	{
		return Report(EACFCameraTuningResult::NotTuning, TEXT("Show original values"));
	}
	CompareStash.Reset();
	TArray<FACFCameraTuningNodeInfo> Nodes;
	Director->GetRequestNodes(Request, Nodes);
	for (const FACFCameraTuningNodeInfo& Node : Nodes)
	{
		for (const EACFCameraTuningParameter Parameter : Node.Parameters)
		{
			FACFCameraTuningValue Value;
			if (Director->GetTuningValue(Request, Parameter, Node.NodeIndex, Value) == EACFCameraTuningResult::Ok && Value.Value != Value.OriginalValue)
			{
				CompareStash.Add({ Node.NodeIndex, Parameter, Value.Value });
			}
		}
	}
	const EACFCameraTuningResult Result = Director->ResetAllTuningValues(Request);
	bShowingOriginal = Result == EACFCameraTuningResult::Ok;
	return Report(Result, FString::Printf(TEXT("Show original values (%d tuned value(s) kept aside)"), CompareStash.Num()));
}

void FACFCameraTuningPanelModel::ClearCompare()
{
	CompareStash.Reset();
	bShowingOriginal = false;
}

// --- Phase 7A-3 --------------------------------------------------------------------------------------------------------

FACFCameraTuningPanelModel::FACFCameraTuningPanelModel()
{
	PrePIEEndedHandle = FEditorDelegates::PrePIEEnded.AddRaw(this, &FACFCameraTuningPanelModel::OnPrePIEEnded);
}

FACFCameraTuningPanelModel::~FACFCameraTuningPanelModel()
{
	FEditorDelegates::PrePIEEnded.Remove(PrePIEEndedHandle);
}

void FACFCameraTuningPanelModel::OnPrePIEEnded(bool bIsSimulating)
{
	// The PIE worlds still exist here; right after, the tuning sessions end with them (only the change set remains).
	CaptureAll();
}

FString FACFCameraTuningPanelModel::MakeSourceKey(FACFCameraRequestHandle Handle) const
{
	if (TestDirector.IsValid())
	{
		return FString::Printf(TEXT("Test:%s|%s"), *TestDirector->GetName(), *Handle.ToString());
	}
	const UWorld* W = World.Get();
	const ULocalPlayer* LocalPlayer = Player.Get();
	const UGameInstance* GameInstance = W ? W->GetGameInstance() : nullptr;
	const int32 PlayerIndex = GameInstance && LocalPlayer ? GameInstance->GetLocalPlayers().IndexOfByKey(LocalPlayer) : INDEX_NONE;
	return FString::Printf(TEXT("%s#%u|%d|%s"), W ? *W->GetName() : TEXT("-"), W ? W->GetUniqueID() : 0u, PlayerIndex, *Handle.ToString());
}

FString FACFCameraTuningPanelModel::MakeSourceLabel(const FACFCameraRequestInfo& Info) const
{
	FString Who = TEXT("Test director");
	if (!TestDirector.IsValid())
	{
		const UWorld* W = World.Get();
		const UGameInstance* GameInstance = W ? W->GetGameInstance() : nullptr;
		const int32 PlayerIndex = GameInstance && Player.IsValid() ? GameInstance->GetLocalPlayers().IndexOfByKey(Player.Get()) : INDEX_NONE;
		Who = FString::Printf(TEXT("%s / Player %d"), W ? *W->GetMapName() : TEXT("-"), PlayerIndex);
	}
	return FString::Printf(TEXT("%s / %s%s"), *Who, *Info.Handle.ToString(), Info.DebugName.IsNone() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Info.DebugName.ToString()));
}

int32 FACFCameraTuningPanelModel::CaptureAll()
{
	int32 NumChanged = 0;
	auto CaptureDirector = [this, &NumChanged](UACFCameraDirector& Director)
	{
		TArray<FACFCameraRequestInfo> Infos;
		Director.GetRequestInfos(Infos);
		for (const FACFCameraRequestInfo& Info : Infos)
		{
			if (Info.bTuning)
			{
				NumChanged += ChangeSet.Capture(Director, Info.Handle, MakeSourceKey(Info.Handle), MakeSourceLabel(Info));
			}
		}
	};
	if (UACFCameraDirector* Director = TestDirector.Get())
	{
		CaptureDirector(*Director);
		return NumChanged;
	}
	// Every PIE player, not only the selected one: the key / label are built from the selection, so select each in turn.
	const TWeakObjectPtr<UWorld> SavedWorld = World;
	const TWeakObjectPtr<ULocalPlayer> SavedPlayer = Player;
	TArray<FWorldEntry> Worlds;
	GetPIEWorlds(Worlds);
	for (const FWorldEntry& WorldEntry : Worlds)
	{
		TArray<FPlayerEntry> Players;
		GetPlayers(WorldEntry.World.Get(), Players);
		for (const FPlayerEntry& PlayerEntry : Players)
		{
			World = WorldEntry.World;
			Player = PlayerEntry.Player;
			UACFCameraSubsystem* Subsystem = PlayerEntry.Player.IsValid() ? PlayerEntry.Player->GetSubsystem<UACFCameraSubsystem>() : nullptr;
			if (UACFCameraDirector* Director = Subsystem ? Subsystem->GetDirector() : nullptr)
			{
				CaptureDirector(*Director);
			}
		}
	}
	World = SavedWorld;
	Player = SavedPlayer;
	return NumChanged;
}

FACFCameraTuningChangeSet::FReport FACFCameraTuningPanelModel::CheckChanges()
{
	CaptureAll();
	FACFCameraTuningChangeSet::FReport Result = ChangeSet.Check();
	LastMessage = FString::Printf(TEXT("Check: %s"), *Result.Message);
	return Result;
}

FACFCameraTuningChangeSet::FReport FACFCameraTuningPanelModel::ApplyChanges(bool bConfirmWarnings)
{
	CaptureAll();
	FACFCameraTuningChangeSet::FReport Result = ChangeSet.Apply(bConfirmWarnings);
	LastMessage = FString::Printf(TEXT("Apply to Asset: %s"), *Result.Message);
	return Result;
}

FACFCameraTuningChangeSet::FReport FACFCameraTuningPanelModel::SaveChanges()
{
	FACFCameraTuningChangeSet::FReport Result = ChangeSet.Save();
	LastMessage = FString::Printf(TEXT("Save Asset: %s"), *Result.Message);
	return Result;
}
