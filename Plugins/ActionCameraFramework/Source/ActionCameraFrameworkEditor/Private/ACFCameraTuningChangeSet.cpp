#include "ACFCameraTuningChangeSet.h"

#include "ACFCameraDirector.h"
#include "ACFCameraNode.h"
#include "ACFCameraPreset.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ACFCameraTuningChangeSet"

namespace ACFTuningChangeSetImpl
{
	UACFCameraPreset* ResolvePreset(const FSoftObjectPath& Path)
	{
		UObject* Object = Path.ResolveObject();
		if (!Object && Path.IsValid())
		{
			Object = Path.TryLoad();
		}
		return Cast<UACFCameraPreset>(Object);
	}

	TArray<FName> GetLayout(const UACFCameraPreset& Preset)
	{
		TArray<FName> Layout;
		for (const UACFCameraNode* Node : Preset.Nodes)
		{
			Layout.Add(Node ? Node->GetClass()->GetFName() : NAME_None);
		}
		return Layout;
	}

	FString FormatValue(const FVector& Value, bool bScalar)
	{
		return bScalar ? FString::Printf(TEXT("%g"), Value.X) : FString::Printf(TEXT("(%g, %g, %g)"), Value.X, Value.Y, Value.Z);
	}

	FString Describe(const FACFCameraTuningChangeSet::FChange& Change)
	{
		return FString::Printf(TEXT("%s: %s of node %d (%s) in %s"), *Change.SourceLabel, *ACFCameraTuning::GetParameterName(Change.Parameter),
			Change.NodeIndex, *Change.NodeClass.ToString(), *Change.Preset.GetAssetName());
	}

	/** "Damping.Time" -> the Damping member property of the node class. */
	FProperty* FindMemberProperty(const UACFCameraNode& Node, FName Property)
	{
		FString Member = Property.ToString();
		Member.Split(TEXT("."), &Member, nullptr);
		return FindFProperty<FProperty>(Node.GetClass(), *Member);
	}
}

int32 FACFCameraTuningChangeSet::Capture(UACFCameraDirector& Director, FACFCameraRequestHandle Handle, const FString& SourceKey, const FString& SourceLabel)
{
	using namespace ACFTuningChangeSetImpl;
	TArray<FACFCameraRequestInfo> Infos;
	Director.GetRequestInfos(Infos);
	const FACFCameraRequestInfo* Info = Infos.FindByPredicate([Handle](const FACFCameraRequestInfo& Entry) { return Entry.Handle == Handle; });
	const UACFCameraPreset* Asset = Info && Info->bTuning ? Cast<UACFCameraPreset>(Info->PresetPath.ResolveObject()) : nullptr;
	if (!Asset)
	{
		return 0;
	}
	TArray<FACFCameraTuningNodeInfo> Nodes;
	if (Director.GetRequestNodes(Handle, Nodes) != EACFCameraTuningResult::Ok)
	{
		return 0;
	}
	const TArray<FName> Layout = GetLayout(*Asset);
	int32 NumChanged = 0;
	for (const FACFCameraTuningNodeInfo& Node : Nodes)
	{
		for (const EACFCameraTuningParameter Parameter : Node.Parameters)
		{
			FACFCameraTuningValue Value;
			if (Director.GetTuningValue(Handle, Parameter, Node.NodeIndex, Value) != EACFCameraTuningResult::Ok || !Asset->Nodes.IsValidIndex(Node.NodeIndex))
			{
				continue;
			}
			FChange* Change = Find(SourceKey, Info->PresetPath, Node.NodeIndex, Parameter);
			if (!Change)
			{
				Change = &Changes.AddDefaulted_GetRef();
				Change->SourceKey = SourceKey;
				Change->Preset = Info->PresetPath;
				Change->NodeIndex = Node.NodeIndex;
				Change->NodeClass = Value.NodeClass;
				Change->NodeName = Asset->Nodes[Node.NodeIndex] ? Asset->Nodes[Node.NodeIndex]->GetFName() : NAME_None;
				Change->Layout = Layout;
				Change->Parameter = Parameter;
				Change->Property = Value.Property;
				Change->bScalar = Value.bScalar;
				Change->BaseValue = Value.OriginalValue; // the asset when this request's tuning is first seen
			}
			Change->SourceLabel = SourceLabel;
			Change->Value = Value.Value;
			NumChanged += Change->IsChanged() ? 1 : 0;
		}
	}
	return NumChanged;
}

void FACFCameraTuningChangeSet::Discard(const FString& SourceKey)
{
	Changes.RemoveAll([&SourceKey](const FChange& Change) { return Change.SourceKey == SourceKey; });
}

void FACFCameraTuningChangeSet::SetIncluded(int32 Index, bool bInclude)
{
	if (Changes.IsValidIndex(Index))
	{
		Changes[Index].bInclude = bInclude;
	}
}

FACFCameraTuningChangeSet::FChange* FACFCameraTuningChangeSet::Find(const FString& SourceKey, const FSoftObjectPath& Preset, int32 NodeIndex, EACFCameraTuningParameter Parameter)
{
	return Changes.FindByPredicate([&](const FChange& Change)
	{
		return Change.SourceKey == SourceKey && Change.Preset == Preset && Change.NodeIndex == NodeIndex && Change.Parameter == Parameter;
	});
}

FACFCameraTuningChangeSet::FStatus FACFCameraTuningChangeSet::Evaluate(const FChange& Change) const
{
	using namespace ACFTuningChangeSetImpl;
	FStatus Status;
	if (!Change.IsChanged())
	{
		return Status;
	}
	auto Conflict = [&Status](FString Reason)
	{
		Status.State = EState::Conflict;
		Status.Reason = MoveTemp(Reason);
		return Status;
	};
	const UACFCameraPreset* Asset = ResolvePreset(Change.Preset);
	if (!Asset)
	{
		return Conflict(FString::Printf(TEXT("the preset asset %s is missing (deleted, renamed or not loadable)"), *Change.Preset.ToString()));
	}
	if (GetLayout(*Asset) != Change.Layout)
	{
		return Conflict(TEXT("the preset's nodes changed since tuning (added, removed, reordered or replaced)"));
	}
	const UACFCameraNode* Node = Asset->Nodes.IsValidIndex(Change.NodeIndex) ? Asset->Nodes[Change.NodeIndex].Get() : nullptr;
	if (!Node || Node->GetClass()->GetFName() != Change.NodeClass || Node->GetFName() != Change.NodeName)
	{
		return Conflict(FString::Printf(TEXT("node %d is no longer %s (%s)"), Change.NodeIndex, *Change.NodeClass.ToString(), *Change.NodeName.ToString()));
	}
	FACFCameraTuningValue AssetValue;
	if (!ACFCameraTuning::Read(*Node, Change.Parameter, AssetValue))
	{
		return Conflict(FString::Printf(TEXT("the node no longer holds %s (e.g. its Override was turned off)"), *ACFCameraTuning::GetParameterName(Change.Parameter)));
	}
	Status.AssetValue = AssetValue.Value;
	if (AssetValue.Value == Change.Value)
	{
		Status.State = EState::Applied;
		Status.bUnsaved = Asset->GetPackage()->IsDirty();
		return Status;
	}
	if (AssetValue.Value != Change.BaseValue)
	{
		return Conflict(FString::Printf(TEXT("the asset value changed since tuning started (tuned from %s, asset now %s)"),
			*FormatValue(Change.BaseValue, Change.bScalar), *FormatValue(AssetValue.Value, Change.bScalar)));
	}
	for (const FChange& Other : Changes)
	{
		if (&Other != &Change && Other.SourceKey != Change.SourceKey && Other.bInclude && Other.IsChanged() && Other.Preset == Change.Preset
			&& Other.NodeIndex == Change.NodeIndex && Other.Parameter == Change.Parameter && Other.Value != Change.Value)
		{
			return Conflict(FString::Printf(TEXT("also tuned by %s to %s"), *Other.SourceLabel, *FormatValue(Other.Value, Other.bScalar)));
		}
	}
	Status.State = EState::Pending;
	return Status;
}

FACFCameraTuningChangeSet::FStatus FACFCameraTuningChangeSet::GetStatus(int32 Index) const
{
	return Changes.IsValidIndex(Index) ? Evaluate(Changes[Index]) : FStatus();
}

int32 FACFCameraTuningChangeSet::CountState(EState State, bool bIncludedOnly) const
{
	int32 Num = 0;
	for (const FChange& Change : Changes)
	{
		Num += (!bIncludedOnly || Change.bInclude) && Evaluate(Change).State == State ? 1 : 0;
	}
	return Num;
}

bool FACFCameraTuningChangeSet::HasUnsavedApplied() const
{
	for (const FChange& Change : Changes)
	{
		const FStatus Status = Evaluate(Change);
		if (Status.State == EState::Applied && Status.bUnsaved)
		{
			return true;
		}
	}
	return false;
}

bool FACFCameraTuningChangeSet::IsPlaySessionRunning()
{
	return GEditor && (GEditor->PlayWorld != nullptr || GEditor->IsPlaySessionInProgress());
}

FACFCameraTuningChangeSet::FReport FACFCameraTuningChangeSet::Check() const
{
	using namespace ACFTuningChangeSetImpl;
	FReport Report;
	TMap<FSoftObjectPath, TArray<const FChange*>> PendingByAsset;
	int32 NumPending = 0;
	for (const FChange& Change : Changes)
	{
		if (!Change.bInclude || !Change.IsChanged())
		{
			continue;
		}
		const FStatus Status = Evaluate(Change);
		if (Status.State == EState::Conflict)
		{
			Report.Conflicts.Add(FString::Printf(TEXT("%s: %s"), *Describe(Change), *Status.Reason));
		}
		else if (Status.State == EState::Pending)
		{
			PendingByAsset.FindOrAdd(Change.Preset).Add(&Change);
			++NumPending;
		}
	}

	// Validation of the candidates: a transient copy of each asset with its pending values written, owned by this function
	// only (destroyed before it returns). The asset itself is not touched.
	for (const TPair<FSoftObjectPath, TArray<const FChange*>>& Entry : PendingByAsset)
	{
		const UACFCameraPreset* Asset = ResolvePreset(Entry.Key);
		if (!Asset)
		{
			continue;
		}
		UPackage* Transient = GetTransientPackage();
		UACFCameraPreset* Candidate = DuplicateObject<UACFCameraPreset>(Asset, Transient,
			MakeUniqueObjectName(Transient, UACFCameraPreset::StaticClass(), *FString::Printf(TEXT("%s_TuningCandidate"), *Asset->GetName())));
		Candidate->ClearFlags(RF_Public | RF_Standalone | RF_Transactional);
		Candidate->SetFlags(RF_Transient);
		for (const FChange* Change : Entry.Value)
		{
			if (UACFCameraNode* Node = Candidate->Nodes.IsValidIndex(Change->NodeIndex) ? Candidate->Nodes[Change->NodeIndex].Get() : nullptr)
			{
				ACFCameraTuning::Write(*Node, Change->Parameter, Change->Value);
			}
		}
		TArray<FACFCameraPresetIssue> Issues;
		ACFCameraPresetValidation::ValidatePreset(*Candidate, Issues);
		for (FACFCameraPresetIssue& Issue : Issues)
		{
			Issue.Message = FText::Format(LOCTEXT("IssueOfAsset", "{0}: {1}"), FText::FromString(Asset->GetName()), Issue.ToText());
			Issue.Preset = Asset; // the candidate and its nodes are gone after this function
			Issue.Node = nullptr;
			Report.NumErrors += Issue.Severity == EACFCameraPresetIssueSeverity::Error ? 1 : 0;
			Report.NumWarnings += Issue.Severity == EACFCameraPresetIssueSeverity::Warning ? 1 : 0;
			Report.NumInfos += Issue.Severity == EACFCameraPresetIssueSeverity::Info ? 1 : 0;
			Report.Issues.Add(MoveTemp(Issue));
		}
		Candidate->MarkAsGarbage();
	}

	Report.NumDone = NumPending;
	Report.bOk = NumPending > 0 && Report.Conflicts.IsEmpty() && Report.NumErrors == 0;
	if (NumPending == 0)
	{
		Report.Message = Report.Conflicts.IsEmpty() ? TEXT("Nothing to apply: no included change differs from its asset.") : TEXT("Nothing can be applied: every included change conflicts.");
	}
	else
	{
		Report.Message = FString::Printf(TEXT("%d change(s) to apply; %d conflict(s); validation: %d error(s), %d warning(s), %d info."),
			NumPending, Report.Conflicts.Num(), Report.NumErrors, Report.NumWarnings, Report.NumInfos);
	}
	return Report;
}

FACFCameraTuningChangeSet::FReport FACFCameraTuningChangeSet::Apply(bool bConfirmWarnings)
{
	using namespace ACFTuningChangeSetImpl;
	if (IsPlaySessionRunning())
	{
		FReport Report;
		Report.Message = TEXT("Not applied: end the play session first (the shared preset is in use by the game; the changes are kept).");
		return Report;
	}
	FReport Report = Check();
	const int32 NumPending = Report.NumDone;
	Report.NumDone = 0;
	if (NumPending == 0 || !Report.Conflicts.IsEmpty() || Report.NumErrors > 0 || (Report.NumWarnings > 0 && !bConfirmWarnings))
	{
		Report.bOk = false;
		Report.Message = NumPending == 0 ? Report.Message
			: !Report.Conflicts.IsEmpty() ? FString::Printf(TEXT("Not applied: %d conflict(s). Nothing was written; leave the conflicting changes out or tune again."), Report.Conflicts.Num())
			: Report.NumErrors > 0 ? FString::Printf(TEXT("Not applied: the result would have %d validation error(s). Nothing was written."), Report.NumErrors)
			: FString::Printf(TEXT("Not applied: %d validation warning(s). Review them and confirm to apply anyway."), Report.NumWarnings);
		return Report;
	}

	// Collect what to write first (Evaluate reads the assets, which change below).
	TArray<const FChange*> ToWrite;
	for (const FChange& Change : Changes)
	{
		if (Change.bInclude && Evaluate(Change).State == EState::Pending && ACFCameraTuning::IsValidValue(Change.Parameter, Change.Value))
		{
			ToWrite.Add(&Change);
		}
	}

	TArray<UACFCameraPreset*> Touched;
	{
		const FScopedTransaction Transaction(LOCTEXT("ApplyTuning", "Apply ACF Camera Tuning"));
		for (const FChange* Change : ToWrite)
		{
			UACFCameraPreset* Asset = ResolvePreset(Change->Preset);
			UACFCameraNode* Node = Asset->Nodes[Change->NodeIndex];
			if (!Touched.Contains(Asset))
			{
				Asset->SetFlags(RF_Transactional);
				Asset->Modify();
				Touched.Add(Asset);
			}
			// The node is an instanced subobject: it is recorded on its own (Modify of the preset does not cover it).
			Node->SetFlags(RF_Transactional);
			FProperty* Member = FindMemberProperty(*Node, Change->Property);
			Node->PreEditChange(Member); // Modify()
			const EACFCameraTuningResult Result = ACFCameraTuning::Write(*Node, Change->Parameter, Change->Value);
			FPropertyChangedEvent Event(Member, EPropertyChangeType::ValueSet);
			Node->PostEditChangeProperty(Event);
			if (Result == EACFCameraTuningResult::Ok)
			{
				++Report.NumDone;
			}
			else
			{
				Report.Conflicts.Add(FString::Printf(TEXT("%s: write failed (%s)"), *Describe(*Change), ACFCameraTuning::LexToString(Result)));
			}
		}
		for (UACFCameraPreset* Asset : Touched)
		{
			Asset->PostEditChange();
			Asset->MarkPackageDirty();
		}
	}
	for (const UACFCameraPreset* Asset : Touched)
	{
		if (Asset->GetPackage()->IsDirty())
		{
			Report.UnsavedPackages.AddUnique(Asset->GetPackage()->GetName());
		}
	}
	Report.bOk = Report.NumDone == ToWrite.Num() && Report.NumDone > 0;
	Report.Message = FString::Printf(TEXT("Applied %d change(s) to %d preset(s) as one undoable step (Edit > Undo reverts it). Not saved yet: %s."),
		Report.NumDone, Touched.Num(), *FString::Join(Report.UnsavedPackages, TEXT(", ")));
	return Report;
}

bool FACFCameraTuningChangeSet::SavePackage(UPackage& Package, FString& OutError)
{
	if (&Package == GetTransientPackage() || Package.HasAnyFlags(RF_Transient) || Package.HasAnyPackageFlags(PKG_InMemoryOnly))
	{
		OutError = FString::Printf(TEXT("%s is not an asset on disk"), *Package.GetName());
		return false;
	}
	FString Filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(Package.GetName(), Filename, FPackageName::GetAssetPackageExtension()))
	{
		OutError = FString::Printf(TEXT("%s has no file path (unmounted content root?)"), *Package.GetName());
		return false;
	}
	if (IFileManager::Get().FileExists(*Filename) && IFileManager::Get().IsReadOnly(*Filename))
	{
		OutError = FString::Printf(TEXT("%s is read-only (checked in or locked by source control?): not saved"), *Filename);
		return false;
	}
	UObject* Asset = Package.FindAssetInPackage();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	Args.Error = GWarn; // the default (GError) would be fatal on failure
	const FSavePackageResultStruct Result = UPackage::Save(&Package, Asset, *Filename, Args);
	if (Result.Result != ESavePackageResult::Success)
	{
		OutError = FString::Printf(TEXT("saving %s failed (result %d): not saved"), *Filename, static_cast<int32>(Result.Result));
		return false;
	}
	return true;
}

FACFCameraTuningChangeSet::FReport FACFCameraTuningChangeSet::Save()
{
	FReport Report;
	TArray<UPackage*> Packages;
	for (const FChange& Change : Changes)
	{
		const FStatus Status = Evaluate(Change);
		if (Status.State == EState::Applied && Status.bUnsaved)
		{
			if (const UACFCameraPreset* Asset = ACFTuningChangeSetImpl::ResolvePreset(Change.Preset))
			{
				Packages.AddUnique(Asset->GetPackage());
			}
		}
	}
	if (Packages.IsEmpty())
	{
		Report.Message = TEXT("Nothing to save: no applied change has unsaved edits.");
		return Report;
	}
	for (UPackage* Package : Packages)
	{
		FString Error;
		if (SavePackage(*Package, Error))
		{
			++Report.NumDone;
		}
		else
		{
			Report.Conflicts.Add(Error);
		}
		if (Package->IsDirty())
		{
			Report.UnsavedPackages.Add(Package->GetName());
		}
	}
	Report.bOk = Report.NumDone == Packages.Num();
	Report.Message = Report.bOk
		? FString::Printf(TEXT("Saved %d package(s) to disk."), Report.NumDone)
		: FString::Printf(TEXT("Saved %d of %d package(s). NOT saved (still unsaved in memory): %s"), Report.NumDone, Packages.Num(), *FString::Join(Report.Conflicts, TEXT("; ")));
	return Report;
}

#undef LOCTEXT_NAMESPACE
