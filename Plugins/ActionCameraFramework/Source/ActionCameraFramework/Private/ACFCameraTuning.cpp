#include "ACFCameraTuning.h"
#include "ACFCameraDirector.h"
#include "ACFCameraHealth.h"
#include "ACFCameraNode.h"
#include "ACFCameraPreset.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_LockOn.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "Nodes/ACFCameraNode_ShotAnchor.h"
#include "UObject/Package.h"

// Phase 7A-1: runtime tuning (README 24 章). The parameter table is explicit (node class -> member) on purpose: a few
// parameters, readable, no generic reflection layer. Add a parameter = one enum value + its cases in Access().

namespace ACFTuningImpl
{
	/** The member of Node that holds Parameter: a vector (OutVector) or a float (OutScalar). False if Node does not hold it. */
	static bool Access(UACFCameraNode& Node, EACFCameraTuningParameter Parameter, FVector*& OutVector, float*& OutScalar, FName& OutProperty)
	{
		OutVector = nullptr;
		OutScalar = nullptr;
		switch (Parameter)
		{
		case EACFCameraTuningParameter::FollowDamping:
			if (UACFCameraNode_Follow* Follow = Cast<UACFCameraNode_Follow>(&Node))
			{
				OutVector = &Follow->Damping.Time;
				OutProperty = TEXT("Damping.Time");
			}
			else if (UACFCameraNode_OrbitalFollow* Orbit = Cast<UACFCameraNode_OrbitalFollow>(&Node))
			{
				OutVector = &Orbit->PivotDamping.Time;
				OutProperty = TEXT("PivotDamping.Time");
			}
			else if (UACFCameraNode_LockOn* LockOn = Cast<UACFCameraNode_LockOn>(&Node))
			{
				OutVector = &LockOn->PivotDamping.Time;
				OutProperty = TEXT("PivotDamping.Time");
			}
			break;
		case EACFCameraTuningParameter::FieldOfView:
			// Only where the node actually sets the FOV (its Override toggle is on).
			if (UACFCameraNode_FixedPose* Fixed = Cast<UACFCameraNode_FixedPose>(&Node); Fixed && Fixed->bOverrideFieldOfView)
			{
				OutScalar = &Fixed->FieldOfView;
				OutProperty = TEXT("FieldOfView");
			}
			else if (UACFCameraNode_ShotAnchor* Shot = Cast<UACFCameraNode_ShotAnchor>(&Node); Shot && Shot->bOverrideFieldOfView)
			{
				OutScalar = &Shot->FieldOfView;
				OutProperty = TEXT("FieldOfView");
			}
			break;
		case EACFCameraTuningParameter::CameraOffset:
			if (UACFCameraNode_Follow* Follow = Cast<UACFCameraNode_Follow>(&Node))
			{
				OutVector = &Follow->Offset;
				OutProperty = TEXT("Offset");
			}
			else if (UACFCameraNode_OrbitalFollow* Orbit = Cast<UACFCameraNode_OrbitalFollow>(&Node))
			{
				OutVector = &Orbit->ShoulderOffset;
				OutProperty = TEXT("ShoulderOffset");
			}
			else if (UACFCameraNode_LockOn* LockOn = Cast<UACFCameraNode_LockOn>(&Node))
			{
				OutVector = &LockOn->ShoulderOffset;
				OutProperty = TEXT("ShoulderOffset");
			}
			else if (UACFCameraNode_ShotAnchor* Shot = Cast<UACFCameraNode_ShotAnchor>(&Node))
			{
				OutVector = &Shot->CameraOffset;
				OutProperty = TEXT("CameraOffset");
			}
			break;
		case EACFCameraTuningParameter::CollisionRadius:
			if (UACFCameraNode_Collision* Collision = Cast<UACFCameraNode_Collision>(&Node))
			{
				OutScalar = &Collision->ProbeRadius;
				OutProperty = TEXT("ProbeRadius");
			}
			break;
		default:
			break;
		}
		return OutVector || OutScalar;
	}

	static bool IsKnown(EACFCameraTuningParameter Parameter)
	{
		return Parameter == EACFCameraTuningParameter::FollowDamping || Parameter == EACFCameraTuningParameter::FieldOfView
			|| Parameter == EACFCameraTuningParameter::CameraOffset || Parameter == EACFCameraTuningParameter::CollisionRadius;
	}

	static constexpr EACFCameraTuningParameter AllParameters[] = { EACFCameraTuningParameter::FollowDamping, EACFCameraTuningParameter::FieldOfView,
		EACFCameraTuningParameter::CameraOffset, EACFCameraTuningParameter::CollisionRadius };
}

const TCHAR* ACFCameraTuning::LexToString(EACFCameraTuningResult Result)
{
	switch (Result)
	{
	case EACFCameraTuningResult::Ok: return TEXT("Ok");
	case EACFCameraTuningResult::NotAvailable: return TEXT("NotAvailable");
	case EACFCameraTuningResult::NoPlayer: return TEXT("NoPlayer");
	case EACFCameraTuningResult::NoRequest: return TEXT("NoRequest");
	case EACFCameraTuningResult::NotTuning: return TEXT("NotTuning");
	case EACFCameraTuningResult::NoNode: return TEXT("NoNode");
	case EACFCameraTuningResult::InvalidValue: return TEXT("InvalidValue");
	case EACFCameraTuningResult::InvalidParameter: return TEXT("InvalidParameter");
	default: return TEXT("?");
	}
}

bool ACFCameraTuning::HasParameter(const UACFCameraNode& Node, EACFCameraTuningParameter Parameter)
{
	FVector* Vector;
	float* Scalar;
	FName Property;
	return ACFTuningImpl::Access(const_cast<UACFCameraNode&>(Node), Parameter, Vector, Scalar, Property);
}

int32 ACFCameraTuning::FindNode(const UACFCameraPreset& Preset, EACFCameraTuningParameter Parameter, int32 NodeIndex)
{
	if (NodeIndex != INDEX_NONE)
	{
		return Preset.Nodes.IsValidIndex(NodeIndex) && Preset.Nodes[NodeIndex] && HasParameter(*Preset.Nodes[NodeIndex], Parameter) ? NodeIndex : INDEX_NONE;
	}
	int32 FirstDisabled = INDEX_NONE;
	for (int32 Index = 0; Index < Preset.Nodes.Num(); ++Index)
	{
		const UACFCameraNode* Node = Preset.Nodes[Index];
		if (Node && HasParameter(*Node, Parameter))
		{
			if (Node->bEnabled)
			{
				return Index;
			}
			FirstDisabled = FirstDisabled == INDEX_NONE ? Index : FirstDisabled;
		}
	}
	return FirstDisabled;
}

bool ACFCameraTuning::Read(const UACFCameraNode& Node, EACFCameraTuningParameter Parameter, FACFCameraTuningValue& Out)
{
	FVector* Vector;
	float* Scalar;
	FName Property;
	if (!ACFTuningImpl::Access(const_cast<UACFCameraNode&>(Node), Parameter, Vector, Scalar, Property))
	{
		return false;
	}
	Out.Value = Vector ? *Vector : FVector(*Scalar, 0.0, 0.0);
	Out.bScalar = Scalar != nullptr;
	Out.NodeClass = Node.GetClass()->GetFName();
	Out.Property = Property;
	return true;
}

bool ACFCameraTuning::IsValidValue(EACFCameraTuningParameter Parameter, const FVector& Value)
{
	if (!ACFCameraValidation::IsFinite(Value))
	{
		return false;
	}
	switch (Parameter)
	{
	case EACFCameraTuningParameter::FollowDamping: return Value.X >= 0.0 && Value.Y >= 0.0 && Value.Z >= 0.0; // B-8: negative damping is no damping
	case EACFCameraTuningParameter::FieldOfView: return ACFCameraValidation::IsValidFov(static_cast<float>(Value.X));
	case EACFCameraTuningParameter::CameraOffset: return true;
	case EACFCameraTuningParameter::CollisionRadius: return Value.X >= 0.0; // a negative radius would be a line trace
	default: return false;
	}
}

EACFCameraTuningResult ACFCameraTuning::Write(UACFCameraNode& Node, EACFCameraTuningParameter Parameter, const FVector& Value)
{
	FVector* Vector;
	float* Scalar;
	FName Property;
	if (!ACFTuningImpl::IsKnown(Parameter))
	{
		return EACFCameraTuningResult::InvalidParameter;
	}
	if (!ACFTuningImpl::Access(Node, Parameter, Vector, Scalar, Property))
	{
		return EACFCameraTuningResult::NoNode;
	}
	if (!IsValidValue(Parameter, Value))
	{
		return EACFCameraTuningResult::InvalidValue;
	}
	if (Vector)
	{
		*Vector = Value;
	}
	else
	{
		*Scalar = static_cast<float>(Value.X);
	}
	return EACFCameraTuningResult::Ok;
}

int32 ACFCameraTuning::CountChanged(const UACFCameraPreset& Original, const UACFCameraPreset& Copy)
{
	int32 Changed = 0;
	for (int32 Index = 0; Index < Original.Nodes.Num() && Index < Copy.Nodes.Num(); ++Index)
	{
		const UACFCameraNode* From = Original.Nodes[Index];
		const UACFCameraNode* To = Copy.Nodes[Index];
		for (const EACFCameraTuningParameter Parameter : ACFTuningImpl::AllParameters)
		{
			FACFCameraTuningValue A;
			FACFCameraTuningValue B;
			if (From && To && Read(*From, Parameter, A) && Read(*To, Parameter, B) && A.Value != B.Value)
			{
				++Changed;
			}
		}
	}
	return Changed;
}

// -------------------------------------------------------------------------------------------------
// Director side (sessions are per director = per local player)

#if ACF_WITH_RUNTIME_TUNING

FACFCameraTuningSession* UACFCameraDirector::FindTuningSession(FACFCameraRequestHandle Handle)
{
	return Handle.IsValid() ? TuningSessions.FindByPredicate([Handle](const FACFCameraTuningSession& Session) { return Session.Handle == Handle; }) : nullptr;
}

EACFCameraTuningResult UACFCameraDirector::StartTuning(FACFCameraRequestHandle Handle)
{
	const FACFCameraRequestHandle Target = ResolveTuningHandle(Handle);
	FACFCameraRequestEntry* Entry = FindLiveEntry(Target);
	const UACFCameraPreset* Original = Entry ? Entry->Rig.GetPreset() : nullptr;
	if (!Original)
	{
		return EACFCameraTuningResult::NoRequest;
	}
	if (FindTuningSession(Target))
	{
		return EACFCameraTuningResult::Ok; // already tuned: keep the current values
	}
	// Deep copy: DuplicateObject also duplicates the Instanced node subobjects (outer = the copy), so no node is shared
	// with the asset. Transient, owned by this director: never saved, gone with the player / when the session ends.
	const FName Name = MakeUniqueObjectName(this, UACFCameraPreset::StaticClass(), *FString::Printf(TEXT("%s_Tuning"), *Original->GetName()));
	UACFCameraPreset* Copy = DuplicateObject<UACFCameraPreset>(Original, this, Name);
	Copy->SetFlags(RF_Transient);
	for (UACFCameraNode* Node : Copy->Nodes)
	{
		if (Node)
		{
			Node->SetFlags(RF_Transient);
		}
	}
	const bool bStateKept = Entry->Rig.RebindPreset(Copy);
	FACFCameraTuningSession& Session = TuningSessions.AddDefaulted_GetRef();
	Session.Handle = Target;
	Session.Original = Original;
	Session.Copy = Copy;
	Session.World = EffectsWorld; // the World of the last evaluation (set on the first update otherwise)
	UE_LOG(LogActionCamera, Log, TEXT("ACF tuning started: request %s, preset %s (copy %s, rig state %s)."), *Target.ToString(), *Original->GetName(),
		*Copy->GetName(), bStateKept ? TEXT("kept") : TEXT("partly re-created"));
	return EACFCameraTuningResult::Ok;
}

EACFCameraTuningResult UACFCameraDirector::StopTuning(FACFCameraRequestHandle Handle)
{
	const FACFCameraRequestHandle Target = ResolveTuningHandle(Handle);
	const int32 Index = Target.IsValid() ? TuningSessions.IndexOfByPredicate([Target](const FACFCameraTuningSession& S) { return S.Handle == Target; }) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return EACFCameraTuningResult::NotTuning;
	}
	if (FACFCameraRequestEntry* Entry = FindLiveEntry(Target))
	{
		Entry->Rig.RebindPreset(TuningSessions[Index].Original); // back to the asset, rig state kept
	}
	UE_LOG(LogActionCamera, Log, TEXT("ACF tuning stopped: request %s (%d value(s) discarded)."), *Target.ToString(),
		TuningSessions[Index].Original && TuningSessions[Index].Copy ? ACFCameraTuning::CountChanged(*TuningSessions[Index].Original, *TuningSessions[Index].Copy) : 0);
	TuningSessions.RemoveAt(Index); // the copy is unreferenced now (GC)
	return EACFCameraTuningResult::Ok;
}

void UACFCameraDirector::StopAllTuning()
{
	while (TuningSessions.Num() > 0)
	{
		StopTuning(TuningSessions.Last().Handle);
	}
}

void UACFCameraDirector::UpdateTuningSessions(const UWorld* World)
{
	for (int32 Index = TuningSessions.Num() - 1; Index >= 0; --Index)
	{
		FACFCameraTuningSession& Session = TuningSessions[Index];
		if (!FindLiveEntry(Session.Handle))
		{
			TuningSessions.RemoveAt(Index); // the request ended (and its blend-out finished): the copy goes with it
			continue;
		}
		if (Session.World.IsExplicitlyNull())
		{
			Session.World = World; // started before the first evaluation
		}
		else if (Session.World.IsStale() || Session.World.Get() != World) // a destroyed World reads as null: stale counts as changed
		{
			UE_LOG(LogActionCamera, Log, TEXT("ACF tuning of request %s ended: the World changed."), *Session.Handle.ToString());
			StopTuning(Session.Handle);
		}
	}
}

EACFCameraTuningResult UACFCameraDirector::GetTuningValue(FACFCameraRequestHandle Handle, EACFCameraTuningParameter Parameter, int32 NodeIndex, FACFCameraTuningValue& Out)
{
	Out = FACFCameraTuningValue();
	const FACFCameraRequestHandle Target = ResolveTuningHandle(Handle);
	FACFCameraRequestEntry* Entry = FindLiveEntry(Target);
	const UACFCameraPreset* Current = Entry ? Entry->Rig.GetPreset() : nullptr;
	if (!Current)
	{
		return EACFCameraTuningResult::NoRequest;
	}
	if (!ACFTuningImpl::IsKnown(Parameter))
	{
		return EACFCameraTuningResult::InvalidParameter;
	}
	const int32 Index = ACFCameraTuning::FindNode(*Current, Parameter, NodeIndex);
	if (Index == INDEX_NONE)
	{
		return EACFCameraTuningResult::NoNode;
	}
	ACFCameraTuning::Read(*Current->Nodes[Index], Parameter, Out);
	Out.NodeIndex = Index;
	Out.OriginalValue = Out.Value;
	const FACFCameraTuningSession* Session = FindTuningSession(Target);
	FACFCameraTuningValue Asset;
	if (Session && Session->Original && Session->Original->Nodes.IsValidIndex(Index) && Session->Original->Nodes[Index]
		&& ACFCameraTuning::Read(*Session->Original->Nodes[Index], Parameter, Asset))
	{
		Out.OriginalValue = Asset.Value;
	}
	return EACFCameraTuningResult::Ok;
}

EACFCameraTuningResult UACFCameraDirector::SetTuningValue(FACFCameraRequestHandle Handle, EACFCameraTuningParameter Parameter, const FVector& Value, int32 NodeIndex)
{
	const FACFCameraRequestHandle Target = ResolveTuningHandle(Handle);
	if (!FindLiveEntry(Target))
	{
		return EACFCameraTuningResult::NoRequest;
	}
	FACFCameraTuningSession* Session = FindTuningSession(Target);
	if (!Session || !Session->Copy)
	{
		return EACFCameraTuningResult::NotTuning;
	}
	if (!ACFTuningImpl::IsKnown(Parameter))
	{
		return EACFCameraTuningResult::InvalidParameter;
	}
	const int32 Index = ACFCameraTuning::FindNode(*Session->Copy, Parameter, NodeIndex);
	if (Index == INDEX_NONE)
	{
		return EACFCameraTuningResult::NoNode;
	}
	// Invalid input is rejected here (nothing changes), so the camera health checks (B-4) never see it.
	return ACFCameraTuning::Write(*Session->Copy->Nodes[Index], Parameter, Value);
}

EACFCameraTuningResult UACFCameraDirector::ResetTuningValue(FACFCameraRequestHandle Handle, EACFCameraTuningParameter Parameter, int32 NodeIndex)
{
	const FACFCameraRequestHandle Target = ResolveTuningHandle(Handle);
	if (!FindLiveEntry(Target))
	{
		return EACFCameraTuningResult::NoRequest;
	}
	FACFCameraTuningSession* Session = FindTuningSession(Target);
	if (!Session || !Session->Copy || !Session->Original)
	{
		return EACFCameraTuningResult::NotTuning;
	}
	if (!ACFTuningImpl::IsKnown(Parameter))
	{
		return EACFCameraTuningResult::InvalidParameter;
	}
	const int32 Index = ACFCameraTuning::FindNode(*Session->Copy, Parameter, NodeIndex);
	FACFCameraTuningValue Asset;
	if (Index == INDEX_NONE || !Session->Original->Nodes.IsValidIndex(Index) || !Session->Original->Nodes[Index]
		|| !ACFCameraTuning::Read(*Session->Original->Nodes[Index], Parameter, Asset))
	{
		return EACFCameraTuningResult::NoNode;
	}
	return ACFCameraTuning::Write(*Session->Copy->Nodes[Index], Parameter, Asset.Value);
}

EACFCameraTuningResult UACFCameraDirector::ResetAllTuningValues(FACFCameraRequestHandle Handle)
{
	const FACFCameraRequestHandle Target = ResolveTuningHandle(Handle);
	if (!FindLiveEntry(Target))
	{
		return EACFCameraTuningResult::NoRequest;
	}
	FACFCameraTuningSession* Session = FindTuningSession(Target);
	if (!Session || !Session->Copy || !Session->Original)
	{
		return EACFCameraTuningResult::NotTuning;
	}
	for (int32 Index = 0; Index < Session->Original->Nodes.Num() && Index < Session->Copy->Nodes.Num(); ++Index)
	{
		const UACFCameraNode* From = Session->Original->Nodes[Index];
		UACFCameraNode* To = Session->Copy->Nodes[Index];
		for (const EACFCameraTuningParameter Parameter : ACFTuningImpl::AllParameters)
		{
			FACFCameraTuningValue Asset;
			if (From && To && ACFCameraTuning::Read(*From, Parameter, Asset))
			{
				ACFCameraTuning::Write(*To, Parameter, Asset.Value);
			}
		}
	}
	return EACFCameraTuningResult::Ok;
}

FACFCameraTuningStatus UACFCameraDirector::GetTuningStatus(FACFCameraRequestHandle Handle)
{
	FACFCameraTuningStatus Status;
	Status.NumTunedRequests = TuningSessions.Num();
	const FACFCameraRequestHandle Target = ResolveTuningHandle(Handle);
	FACFCameraRequestEntry* Entry = FindLiveEntry(Target);
	if (!Entry)
	{
		return Status;
	}
	Status.Handle = Target;
	const FACFCameraTuningSession* Session = FindTuningSession(Target);
	Status.bTuning = Session != nullptr;
	const UACFCameraPreset* Asset = Session ? Session->Original.Get() : Entry->Rig.GetPreset();
	Status.OriginalPreset = Asset ? Asset->GetFName() : NAME_None;
	Status.NumChangedValues = Session && Session->Original && Session->Copy ? ACFCameraTuning::CountChanged(*Session->Original, *Session->Copy) : 0;
	return Status;
}

#else // ACF_WITH_RUNTIME_TUNING

FACFCameraTuningSession* UACFCameraDirector::FindTuningSession(FACFCameraRequestHandle) { return nullptr; }
EACFCameraTuningResult UACFCameraDirector::StartTuning(FACFCameraRequestHandle) { return EACFCameraTuningResult::NotAvailable; }
EACFCameraTuningResult UACFCameraDirector::StopTuning(FACFCameraRequestHandle) { return EACFCameraTuningResult::NotAvailable; }
void UACFCameraDirector::StopAllTuning() {}
void UACFCameraDirector::UpdateTuningSessions(const UWorld*) {}
EACFCameraTuningResult UACFCameraDirector::GetTuningValue(FACFCameraRequestHandle, EACFCameraTuningParameter, int32, FACFCameraTuningValue& Out) { Out = FACFCameraTuningValue(); return EACFCameraTuningResult::NotAvailable; }
EACFCameraTuningResult UACFCameraDirector::SetTuningValue(FACFCameraRequestHandle, EACFCameraTuningParameter, const FVector&, int32) { return EACFCameraTuningResult::NotAvailable; }
EACFCameraTuningResult UACFCameraDirector::ResetTuningValue(FACFCameraRequestHandle, EACFCameraTuningParameter, int32) { return EACFCameraTuningResult::NotAvailable; }
EACFCameraTuningResult UACFCameraDirector::ResetAllTuningValues(FACFCameraRequestHandle) { return EACFCameraTuningResult::NotAvailable; }
FACFCameraTuningStatus UACFCameraDirector::GetTuningStatus(FACFCameraRequestHandle) { return FACFCameraTuningStatus(); }

#endif // ACF_WITH_RUNTIME_TUNING

const UACFCameraPreset* UACFCameraDirector::GetRequestPreset(FACFCameraRequestHandle Handle)
{
	const FACFCameraRequestEntry* Entry = FindLiveEntry(ResolveTuningHandle(Handle));
	return Entry ? Entry->Rig.GetPreset() : nullptr;
}
