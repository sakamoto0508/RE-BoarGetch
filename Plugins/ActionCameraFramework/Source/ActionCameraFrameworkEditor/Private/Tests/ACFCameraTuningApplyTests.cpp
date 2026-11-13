#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraDirector.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSubsystem.h"
#include "ACFCameraTuningChangeSet.h"
#include "ACFCameraTuningPanelModel.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "PackageTools.h"
#include "Framework/Application/SlateApplication.h"
#include "SACFCameraTuningPanel.h"
#include "Serialization/ObjectWriter.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Linker.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 7A-3: applying tuned values to the preset assets (change set, conflicts, validation, one transaction, Undo / Redo,
 * save, reload, PIE). README 26 章. The assets are created in a temporary content root mounted on Saved/ACFTuningTest (never
 * a tracked asset), with the real editor transaction buffer and the real package save / reload.
 */
namespace ACFTuningApplyTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	using EParam = EACFCameraTuningParameter;
	using EResult = EACFCameraTuningResult;
	using EState = FACFCameraTuningChangeSet::EState;

	const TCHAR* const Root = TEXT("/ACFTuningTest/");

	inline FString RootDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ACFTuningTest/"));
	}

	/** Mounts the temporary content root for one test and deletes its files afterwards (also read-only ones). */
	struct FTestRoot
	{
		FTestRoot() { FPackageName::RegisterMountPoint(Root, RootDir()); }
		~FTestRoot()
		{
			// A loaded / reloaded package keeps its file open through its linker: release them first, or the delete fails.
			for (TObjectIterator<UPackage> It; It; ++It)
			{
				if (It->GetName().StartsWith(Root))
				{
					ResetLoaders(*It);
				}
			}
			IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
			File.IterateDirectoryRecursively(*RootDir(), [&File](const TCHAR* Path, bool bDirectory)
			{
				if (!bDirectory)
				{
					File.SetReadOnly(Path, false);
				}
				return true;
			});
			IFileManager::Get().DeleteDirectory(*RootDir(), false, true);
			FPackageName::UnRegisterMountPoint(Root, RootDir());
			if (IFileManager::Get().DirectoryExists(*RootDir()) && Test)
			{
				Test->AddError(FString::Printf(TEXT("Test assets left in %s (could not delete)."), *RootDir()));
			}
		}
		FAutomationTestBase* Test = nullptr;
	};

	enum class EKind { Basic, TwoFollows, InvalidLens, NoCollision };

	/**
	 * A preset asset in its own package: [0] Follow, [1] FOV-only Fixed Pose (90), [2] Collision (radius 20). TwoFollows adds a
	 * second Follow at [1] (two nodes write the location: a validation Warning), InvalidLens an extra Fixed Pose with FOV 0
	 * (a validation Error already in the asset), NoCollision drops the Collision (Info). The nodes are created like
	 * instanced subobjects without RF_Transactional: Apply must record them anyway.
	 */
	inline UACFCameraPreset* MakeAsset(const TCHAR* Name, EKind Kind = EKind::Basic)
	{
		const FString Short = FString::Printf(TEXT("%s_%s"), Name, *FGuid::NewGuid().ToString(EGuidFormats::Short));
		UPackage* Package = CreatePackage(*(FString(Root) + Short));
		UACFCameraPreset* Preset = NewObject<UACFCameraPreset>(Package, *Short, RF_Public | RF_Standalone | RF_Transactional);
		Preset->DefaultBlendIn = FACFCameraBlendSettings::MakeCut();
		Preset->DefaultBlendOut = FACFCameraBlendSettings::MakeCut();
		UACFCameraNode_Follow* Follow = NewObject<UACFCameraNode_Follow>(Preset);
		Follow->Offset = FVector(-300.0, 0.0, 150.0);
		Preset->Nodes.Add(Follow);
		if (Kind == EKind::TwoFollows)
		{
			UACFCameraNode_Follow* Second = NewObject<UACFCameraNode_Follow>(Preset);
			Second->Offset = FVector(-400.0, 50.0, 100.0);
			Preset->Nodes.Add(Second);
		}
		UACFCameraNode_FixedPose* Lens = NewObject<UACFCameraNode_FixedPose>(Preset);
		Lens->bOverrideLocation = false;
		Lens->bOverrideRotation = false;
		Lens->FieldOfView = 90.f;
		Preset->Nodes.Add(Lens);
		if (Kind != EKind::NoCollision)
		{
			UACFCameraNode_Collision* Collision = NewObject<UACFCameraNode_Collision>(Preset);
			Collision->ProbeRadius = 20.f;
			Preset->Nodes.Add(Collision);
		}
		if (Kind == EKind::InvalidLens)
		{
			UACFCameraNode_FixedPose* Broken = NewObject<UACFCameraNode_FixedPose>(Preset);
			Broken->bOverrideLocation = false;
			Broken->bOverrideRotation = false;
			Broken->FieldOfView = 0.f;
			Preset->Nodes.Add(Broken);
		}
		Package->SetDirtyFlag(false);
		return Preset;
	}

	inline void DropAsset(UACFCameraPreset* Preset)
	{
		if (Preset)
		{
			Preset->GetPackage()->SetDirtyFlag(false);
			Preset->ClearFlags(RF_Public | RF_Standalone);
		}
	}

	inline TArray<uint8> Snapshot(const UObject* Object)
	{
		TArray<uint8> Bytes;
		FObjectWriter Writer(const_cast<UObject*>(Object), Bytes);
		return Bytes;
	}

	template <typename T>
	T* NodeAt(const UACFCameraPreset* Preset, int32 Index)
	{
		return CastChecked<T>(Preset->Nodes[Index].Get());
	}

	struct FDriver
	{
		uint64 Frame = 7000000;
		const FACFCameraEvaluationResult& Step(UACFCameraDirector* Director)
		{
			FACFCameraEvaluationContext Context;
			Context.DeltaTime = 1.f / 60.f;
			Context.FrameNumber = ++Frame;
			return Director->Evaluate(Context);
		}
	};

	/** A standalone director with one request on Preset, tuned through a panel model (Start records the base values). */
	struct FTuned
	{
		TStrongObjectPtr<UACFCameraDirector> Director;
		TStrongObjectPtr<USceneComponent> Target;
		FACFCameraRequestHandle Handle;
		TUniquePtr<FACFCameraTuningPanelModel> Model;

		FTuned(UACFCameraPreset* Preset, bool bStart = true)
		{
			Director.Reset(NewObject<UACFCameraDirector>());
			Target.Reset(NewObject<USceneComponent>(GetTransientPackage(), NAME_None, RF_Transient));
			Target->SetMobility(EComponentMobility::Movable);
			FACFCameraRequestParams Params;
			Params.FollowTarget.Component = Target.Get();
			Handle = Director->AddRequest(Preset, Params);
			Model = MakeUnique<FACFCameraTuningPanelModel>();
			Model->SetDirectorForTesting(Director.Get());
			Model->SelectRequest(Handle);
			if (bStart)
			{
				Model->Start();
			}
		}

		EResult Set(int32 NodeIndex, FName NodeClass, EParam Parameter, const FVector& Value)
		{
			Model->SelectNode(NodeIndex, NodeClass);
			return Model->SetValue(Parameter, Value);
		}

		~FTuned()
		{
			if (Director.IsValid())
			{
				Director->StopAllTuning();
				Director->RemoveRequest(Handle, nullptr, false);
			}
		}
	};

	inline const FName FollowClass = UACFCameraNode_Follow::StaticClass()->GetFName();
	inline const FName LensClass = UACFCameraNode_FixedPose::StaticClass()->GetFName();
	inline const FName CollisionClass = UACFCameraNode_Collision::StaticClass()->GetFName();

	inline FString UndoTitle()
	{
		return GEditor && GEditor->Trans ? GEditor->Trans->GetUndoContext(false).Title.ToString() : FString();
	}
}

// 1, 2, 3, 4, 5, 10, 11, 12, 13: nothing to apply, one parameter, several parameters in one transaction, nothing else changes,
// one Undo reverts the whole Apply, Redo re-applies it, the instanced nodes are recorded, the package becomes dirty.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningApplyBasicsTest, "ActionCameraFramework.Editor.TuningApply.ApplyUndoRedo", ACFTuningApplyTests::Flags)
bool FACFTuningApplyBasicsTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningApplyTests;
	if (!TestNotNull(TEXT("Editor transaction buffer"), GEditor ? GEditor->Trans.Get() : nullptr))
	{
		return false;
	}
	FTestRoot TestRoot;
	TestRoot.Test = this;
	TStrongObjectPtr<UACFCameraPreset> Asset(MakeAsset(TEXT("DA_ApplyBasics")));
	UPackage* Package = Asset->GetPackage();
	const TArray<uint8> PresetBefore = Snapshot(Asset.Get());
	const FVector DampingBefore = NodeAt<UACFCameraNode_Follow>(Asset.Get(), 0)->Damping.Time;
	TArray<TArray<uint8>> NodesBefore;
	for (const UACFCameraNode* Node : Asset->Nodes)
	{
		NodesBefore.Add(Snapshot(Node));
	}
	FTuned Tuned(Asset.Get());
	FACFCameraTuningPanelModel& Model = *Tuned.Model;
	TestTrue(TEXT("Tuning started"), Model.IsTuning());

	// 1. Nothing changed: nothing to apply.
	FACFCameraTuningChangeSet::FReport Report = Model.ApplyChanges(false);
	TestFalse(TEXT("1. No change: Apply refused"), Report.bOk);
	TestEqual(TEXT("1. No change: nothing written"), Report.NumDone, 0);
	TestEqual(TEXT("1. No change: no pending change"), Model.GetChangeSet().CountState(EState::Pending, true), 0);
	TestFalse(TEXT("1. No change: package clean"), Package->IsDirty());

	// 2. One parameter.
	TestEqual(TEXT("Tune the offset"), Tuned.Set(0, FollowClass, EParam::CameraOffset, FVector(-500.0, 20.0, 200.0)), EResult::Ok);
	TestEqual(TEXT("Asset untouched while tuning"), NodeAt<UACFCameraNode_Follow>(Asset.Get(), 0)->Offset, FVector(-300.0, 0.0, 150.0));
	Model.CaptureAll();
	TestEqual(TEXT("2. One pending change"), Model.GetChangeSet().CountState(EState::Pending, true), 1);
	Report = Model.ApplyChanges(false);
	TestTrue(FString::Printf(TEXT("2. Applied (%s)"), *Report.Message), Report.bOk && Report.NumDone == 1);
	TestEqual(TEXT("2. Asset offset"), NodeAt<UACFCameraNode_Follow>(Asset.Get(), 0)->Offset, FVector(-500.0, 20.0, 200.0));
	TestEqual(TEXT("13. Applied: package dirty"), Package->IsDirty(), true);
	TestEqual(TEXT("One transaction named after the Apply"), UndoTitle(), FString(TEXT("Apply ACF Camera Tuning")));
	TestEqual(TEXT("4. Preset object itself unchanged"), Snapshot(Asset.Get()) == PresetBefore, true);
	for (int32 Index = 1; Index < Asset->Nodes.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("5. Node %d unchanged (byte for byte)"), Index), Snapshot(Asset->Nodes[Index]) == NodesBefore[Index]);
	}
	TestEqual(TEXT("4. The tuned node's other values unchanged (damping)"), NodeAt<UACFCameraNode_Follow>(Asset.Get(), 0)->Damping.Time, DampingBefore);
	TestTrue(TEXT("12. The instanced node is transactional now"), Asset->Nodes[0]->HasAnyFlags(RF_Transactional));
	TestEqual(TEXT("The change shows as applied"), Model.GetChangeSet().CountState(EState::Applied, true), 1);
	TestFalse(TEXT("Applying again: nothing to apply"), Model.ApplyChanges(false).bOk);

	// 10 / 11. Undo / Redo of one parameter.
	TestTrue(TEXT("10. Undo"), GEditor->UndoTransaction());
	TestTrue(TEXT("10. Undo: the node is byte for byte as before"), Snapshot(Asset->Nodes[0]) == NodesBefore[0]);
	TestEqual(TEXT("10. Undo: pending again"), Model.GetChangeSet().CountState(EState::Pending, true), 1);
	TestTrue(TEXT("11. Redo"), GEditor->RedoTransaction());
	TestEqual(TEXT("11. Redo: applied value"), NodeAt<UACFCameraNode_Follow>(Asset.Get(), 0)->Offset, FVector(-500.0, 20.0, 200.0));
	TestEqual(TEXT("11. Redo: applied again"), Model.GetChangeSet().CountState(EState::Applied, true), 1);

	// 3. Several parameters of several nodes, one transaction.
	TestEqual(TEXT("Tune the damping"), Tuned.Set(0, FollowClass, EParam::FollowDamping, FVector(0.25, 0.25, 0.5)), EResult::Ok);
	TestEqual(TEXT("Tune the FOV"), Tuned.Set(1, LensClass, EParam::FieldOfView, FVector(70.0, 0.0, 0.0)), EResult::Ok);
	TestEqual(TEXT("Tune the radius"), Tuned.Set(2, CollisionClass, EParam::CollisionRadius, FVector(35.0, 0.0, 0.0)), EResult::Ok);
	const TArray<uint8> FollowAfterFirstApply = Snapshot(Asset->Nodes[0]);
	Report = Model.ApplyChanges(false);
	TestTrue(FString::Printf(TEXT("3. Applied three (%s)"), *Report.Message), Report.bOk && Report.NumDone == 3);
	TestEqual(TEXT("3. Damping"), NodeAt<UACFCameraNode_Follow>(Asset.Get(), 0)->Damping.Time, FVector(0.25, 0.25, 0.5));
	TestEqual(TEXT("3. FOV"), NodeAt<UACFCameraNode_FixedPose>(Asset.Get(), 1)->FieldOfView, 70.f);
	TestEqual(TEXT("3. Radius"), NodeAt<UACFCameraNode_Collision>(Asset.Get(), 2)->ProbeRadius, 35.f);
	TestEqual(TEXT("3. Four applied"), Model.GetChangeSet().CountState(EState::Applied, true), 4);

	TestTrue(TEXT("10. One Undo reverts the three"), GEditor->UndoTransaction());
	TestTrue(TEXT("10. Follow back to the first Apply"), Snapshot(Asset->Nodes[0]) == FollowAfterFirstApply);
	TestTrue(TEXT("10. Lens back to the asset (byte for byte)"), Snapshot(Asset->Nodes[1]) == NodesBefore[1]);
	TestTrue(TEXT("10. Collision back to the asset (byte for byte)"), Snapshot(Asset->Nodes[2]) == NodesBefore[2]);
	TestEqual(TEXT("10. Three pending, one applied"), Model.GetChangeSet().CountState(EState::Pending, true), 3);
	TestTrue(TEXT("Second Undo reverts the first Apply"), GEditor->UndoTransaction());
	for (int32 Index = 0; Index < Asset->Nodes.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("Both undone: node %d as before"), Index), Snapshot(Asset->Nodes[Index]) == NodesBefore[Index]);
	}
	TestTrue(TEXT("Undo: the preset as before"), Snapshot(Asset.Get()) == PresetBefore);
	// UE records the package dirty flag in the transaction: undoing back to before the first Apply restores it (the package
	// was clean then). After a save, Undo leaves it dirty instead (SaveReloadAndFailure).
	TestFalse(TEXT("13. Undone to before the first Apply: the dirty flag of that moment (clean)"), Package->IsDirty());
	TestTrue(TEXT("11. Redo 1"), GEditor->RedoTransaction());
	TestTrue(TEXT("11. Redo 2"), GEditor->RedoTransaction());
	TestEqual(TEXT("11. Redo: four applied"), Model.GetChangeSet().CountState(EState::Applied, true), 4);
	TestEqual(TEXT("11. Redo: radius"), NodeAt<UACFCameraNode_Collision>(Asset.Get(), 2)->ProbeRadius, 35.f);

	// Repeated Undo / Redo keeps the values exact.
	for (int32 Round = 0; Round < 5; ++Round)
	{
		GEditor->UndoTransaction();
		GEditor->UndoTransaction();
		GEditor->RedoTransaction();
		GEditor->RedoTransaction();
	}
	TestEqual(TEXT("Undo / Redo x5: offset"), NodeAt<UACFCameraNode_Follow>(Asset.Get(), 0)->Offset, FVector(-500.0, 20.0, 200.0));
	TestEqual(TEXT("Undo / Redo x5: FOV"), NodeAt<UACFCameraNode_FixedPose>(Asset.Get(), 1)->FieldOfView, 70.f);
	TestTrue(TEXT("Undo / Redo x5: the tuning copy is not the asset"), Tuned.Director->GetRequestPreset(Tuned.Handle) != Asset.Get());
	Tuned.Director->StopAllTuning();
	GEditor->ResetTransaction(FText::FromString(TEXT("ACF tuning apply test"))); // the test assets leave the undo history
	DropAsset(Asset.Get());
	return true;
}

// 6, 7, 8, 9 (+ Info): validation errors and warnings, external edits of the asset, node layout changes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningApplyRefusalsTest, "ActionCameraFramework.Editor.TuningApply.ValidationAndConflicts", ACFTuningApplyTests::Flags)
bool FACFTuningApplyRefusalsTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningApplyTests;
	FTestRoot TestRoot;
	TestRoot.Test = this;

	// 7. Warning: shown, applied only after confirmation.
	{
		TStrongObjectPtr<UACFCameraPreset> Asset(MakeAsset(TEXT("DA_Warning"), EKind::TwoFollows));
		FTuned Tuned(Asset.Get());
		Tuned.Set(0, FollowClass, EParam::CameraOffset, FVector(-350.0, 0.0, 150.0));
		const FACFCameraTuningChangeSet::FReport Check = Tuned.Model->CheckChanges();
		TestTrue(TEXT("7. Check reports the warning"), Check.NumWarnings > 0 && Check.NumErrors == 0);
		TestTrue(TEXT("7. The warning names the conflict"), Check.Issues.ContainsByPredicate([](const FACFCameraPresetIssue& Issue) { return Issue.Id == FName(TEXT("ACF.Preset.BodyConflict")); }));
		const FACFCameraTuningChangeSet::FReport Refused = Tuned.Model->ApplyChanges(false);
		TestFalse(TEXT("7. Not confirmed: refused"), Refused.bOk);
		TestEqual(TEXT("7. Not confirmed: asset unchanged"), NodeAt<UACFCameraNode_Follow>(Asset.Get(), 0)->Offset, FVector(-300.0, 0.0, 150.0));
		TestFalse(TEXT("7. Not confirmed: package clean"), Asset->GetPackage()->IsDirty());
		const FACFCameraTuningChangeSet::FReport Applied = Tuned.Model->ApplyChanges(true);
		TestTrue(TEXT("7. Confirmed: applied"), Applied.bOk);
		TestEqual(TEXT("7. Confirmed: asset value"), NodeAt<UACFCameraNode_Follow>(Asset.Get(), 0)->Offset, FVector(-350.0, 0.0, 150.0));
		DropAsset(Asset.Get());
	}
	// 6. Error (already in the asset): refused even when warnings are confirmed.
	{
		TStrongObjectPtr<UACFCameraPreset> Asset(MakeAsset(TEXT("DA_Error"), EKind::InvalidLens));
		const TArray<uint8> Before = Snapshot(Asset->Nodes[0]);
		FTuned Tuned(Asset.Get());
		Tuned.Set(0, FollowClass, EParam::CameraOffset, FVector(-350.0, 0.0, 150.0));
		const FACFCameraTuningChangeSet::FReport Refused = Tuned.Model->ApplyChanges(true);
		TestFalse(TEXT("6. Error: refused"), Refused.bOk);
		TestTrue(TEXT("6. Error: reported"), Refused.NumErrors > 0);
		TestTrue(TEXT("6. Error: the asset is untouched"), Snapshot(Asset->Nodes[0]) == Before && !Asset->GetPackage()->IsDirty());
		DropAsset(Asset.Get());
	}
	// Info only: shown, applied without confirmation.
	{
		TStrongObjectPtr<UACFCameraPreset> Asset(MakeAsset(TEXT("DA_Info"), EKind::NoCollision));
		FTuned Tuned(Asset.Get());
		Tuned.Set(1, LensClass, EParam::FieldOfView, FVector(60.0, 0.0, 0.0));
		const FACFCameraTuningChangeSet::FReport Applied = Tuned.Model->ApplyChanges(false);
		TestTrue(FString::Printf(TEXT("Info only: applied (%d info)"), Applied.NumInfos), Applied.bOk && Applied.NumInfos > 0 && Applied.NumWarnings == 0);
		DropAsset(Asset.Get());
	}
	// 8 / 9. The asset changes after tuning started.
	{
		TStrongObjectPtr<UACFCameraPreset> Asset(MakeAsset(TEXT("DA_Conflict")));
		FTuned Tuned(Asset.Get());
		FACFCameraTuningPanelModel& Model = *Tuned.Model;
		Tuned.Set(0, FollowClass, EParam::CameraOffset, FVector(-500.0, 0.0, 150.0));
		Model.CaptureAll();
		UACFCameraNode_Follow* Follow = NodeAt<UACFCameraNode_Follow>(Asset.Get(), 0);

		// 8. Another editor operation changes the same parameter.
		Follow->Modify();
		Follow->Offset = FVector(-320.0, 0.0, 150.0);
		TestEqual(TEXT("8. External edit: conflict"), Model.GetChangeSet().CountState(EState::Conflict, true), 1);
		const FACFCameraTuningChangeSet::FReport Refused = Model.ApplyChanges(true);
		TestFalse(TEXT("8. External edit: refused"), Refused.bOk);
		TestTrue(TEXT("8. External edit: the conflict is described"), Refused.Conflicts.Num() == 1 && Refused.Conflicts[0].Contains(TEXT("changed since tuning started")));
		TestEqual(TEXT("8. External edit: not overwritten"), Follow->Offset, FVector(-320.0, 0.0, 150.0));
		Follow->Offset = FVector(-300.0, 0.0, 150.0);
		TestEqual(TEXT("8. Back to the base value: pending again"), Model.GetChangeSet().CountState(EState::Pending, true), 1);

		// 9. Node added, removed, reordered, replaced.
		UACFCameraNode* Added = NewObject<UACFCameraNode_Collision>(Asset.Get());
		Asset->Nodes.Add(Added);
		TestEqual(TEXT("9. Node added: conflict"), Model.GetChangeSet().CountState(EState::Conflict, true), 1);
		Asset->Nodes.Pop();
		TObjectPtr<UACFCameraNode> Removed = Asset->Nodes.Pop();
		TestEqual(TEXT("9. Node removed: conflict"), Model.GetChangeSet().CountState(EState::Conflict, true), 1);
		Asset->Nodes.Add(Removed);
		Asset->Nodes.Swap(0, 1);
		TestEqual(TEXT("9. Nodes reordered: conflict"), Model.GetChangeSet().CountState(EState::Conflict, true), 1);
		TestFalse(TEXT("9. Reordered: refused"), Model.ApplyChanges(true).bOk);
		Asset->Nodes.Swap(0, 1);
		TObjectPtr<UACFCameraNode> Original = Asset->Nodes[0];
		Asset->Nodes[0] = NewObject<UACFCameraNode_Follow>(Asset.Get());
		CastChecked<UACFCameraNode_Follow>(Asset->Nodes[0].Get())->Offset = FVector(-300.0, 0.0, 150.0);
		TestEqual(TEXT("9. Node replaced by another of the same class: conflict"), Model.GetChangeSet().CountState(EState::Conflict, true), 1);
		Asset->Nodes[0] = Original;
		TestEqual(TEXT("9. Layout restored: pending again"), Model.GetChangeSet().CountState(EState::Pending, true), 1);
		TestEqual(TEXT("9. Asset still untouched"), Follow->Offset, FVector(-300.0, 0.0, 150.0));

		// Discard (Stop Tuning) forgets the request's changes and never touches the asset.
		Model.Stop();
		TestEqual(TEXT("Stop (discard): no change left"), Model.GetChangeSet().GetChanges().Num(), 0);
		DropAsset(Asset.Get());
	}
	GEditor->ResetTransaction(FText::FromString(TEXT("ACF tuning apply test")));
	return true;
}

// 16: two local players (two directors) and two requests: their changes stay apart; the same parameter tuned differently
// is a conflict (nothing is merged silently), excluding one side resolves it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningApplyPlayersTest, "ActionCameraFramework.Editor.TuningApply.PlayersDoNotMix", ACFTuningApplyTests::Flags)
bool FACFTuningApplyPlayersTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningApplyTests;
	FTestRoot TestRoot;
	TestRoot.Test = this;
	TStrongObjectPtr<UACFCameraPreset> Asset(MakeAsset(TEXT("DA_Players")));
	FTuned Player1(Asset.Get(), false);
	FTuned Player2(Asset.Get(), false);
	Player1.Director->StartTuning(Player1.Handle);
	Player2.Director->StartTuning(Player2.Handle);
	FACFCameraTuningChangeSet Set;
	Player1.Director->SetTuningValue(Player1.Handle, EParam::FieldOfView, FVector(70.0, 0.0, 0.0), 1);
	Player2.Director->SetTuningValue(Player2.Handle, EParam::FieldOfView, FVector(60.0, 0.0, 0.0), 1);
	Player2.Director->SetTuningValue(Player2.Handle, EParam::CollisionRadius, FVector(40.0, 0.0, 0.0), 2);
	TestEqual(TEXT("Player 1: one change"), Set.Capture(*Player1.Director, Player1.Handle, TEXT("P1"), TEXT("PIE / Player 0 / request")), 1);
	TestEqual(TEXT("Player 2: two changes"), Set.Capture(*Player2.Director, Player2.Handle, TEXT("P2"), TEXT("PIE / Player 1 / request")), 2);
	TestEqual(TEXT("Same FOV, different values: both conflict"), Set.CountState(EState::Conflict, true), 2);
	TestEqual(TEXT("The radius (only player 2) is pending"), Set.CountState(EState::Pending, true), 1);
	FACFCameraTuningChangeSet::FReport Report = Set.Apply(false);
	TestFalse(TEXT("Conflicts: refused"), Report.bOk);
	TestTrue(TEXT("The conflict names the other player"), Report.Conflicts.Num() == 2 && Report.Conflicts[0].Contains(TEXT("also tuned by")));
	TestEqual(TEXT("Refused: asset FOV"), NodeAt<UACFCameraNode_FixedPose>(Asset.Get(), 1)->FieldOfView, 90.f);
	for (int32 Index = 0; Index < Set.GetChanges().Num(); ++Index)
	{
		if (Set.GetChanges()[Index].SourceKey == TEXT("P2") && Set.GetChanges()[Index].Parameter == EParam::FieldOfView)
		{
			Set.SetIncluded(Index, false); // leave player 2's FOV out
		}
	}
	Report = Set.Apply(false);
	TestTrue(FString::Printf(TEXT("Player 1 FOV + player 2 radius applied (%s)"), *Report.Message), Report.bOk && Report.NumDone == 2);
	TestEqual(TEXT("FOV of player 1"), NodeAt<UACFCameraNode_FixedPose>(Asset.Get(), 1)->FieldOfView, 70.f);
	TestEqual(TEXT("Radius of player 2"), NodeAt<UACFCameraNode_Collision>(Asset.Get(), 2)->ProbeRadius, 40.f);
	for (int32 Index = 0; Index < Set.GetChanges().Num(); ++Index)
	{
		const FACFCameraTuningChangeSet::FChange& Change = Set.GetChanges()[Index];
		if (Change.SourceKey == TEXT("P2") && Change.Parameter == EParam::FieldOfView)
		{
			Set.SetIncluded(Index, true);
			TestEqual(TEXT("Player 2's FOV now conflicts with the applied asset (not overwritten)"), Set.GetStatus(Index).State, EState::Conflict);
		}
	}
	TestFalse(TEXT("Applying player 2's FOV is refused"), Set.Apply(false).bOk);
	TestEqual(TEXT("Still player 1's FOV"), NodeAt<UACFCameraNode_FixedPose>(Asset.Get(), 1)->FieldOfView, 70.f);
	Player1.Director->StopAllTuning();
	Player2.Director->StopAllTuning();
	GEditor->ResetTransaction(FText::FromString(TEXT("ACF tuning apply test")));
	DropAsset(Asset.Get());
	return true;
}

// 14, 15: Save writes only the package of the applied changes; reloading it from disk gives the applied values; a
// read-only file is reported as not saved and the package stays dirty; Undo after a save does not touch the file.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningApplySaveTest, "ActionCameraFramework.Editor.TuningApply.SaveReloadAndFailure", ACFTuningApplyTests::Flags)
bool FACFTuningApplySaveTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningApplyTests;
	FTestRoot TestRoot;
	TestRoot.Test = this;
	UACFCameraPreset* Asset = MakeAsset(TEXT("DA_Save"));
	UPackage* Package = Asset->GetPackage();
	const FSoftObjectPath AssetPath(Asset);
	UPackage* Bystander = MakeAsset(TEXT("DA_Bystander"))->GetPackage();
	Bystander->SetDirtyFlag(true); // an unrelated dirty package: Save must leave it alone
	FString Error;
	if (!TestTrue(TEXT("Initial save of the test asset"), FACFCameraTuningChangeSet::SavePackage(*Package, Error)))
	{
		AddError(Error);
		return false;
	}
	FString Filename;
	FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename, FPackageName::GetAssetPackageExtension());
	TestTrue(TEXT("The file exists"), IFileManager::Get().FileExists(*Filename));
	{
		FTuned Tuned(Asset);
		Tuned.Set(1, LensClass, EParam::FieldOfView, FVector(65.0, 0.0, 0.0));
		FACFCameraTuningPanelModel& Model = *Tuned.Model;
		TestTrue(TEXT("Applied"), Model.ApplyChanges(false).bOk);
		TestTrue(TEXT("Applied: unsaved"), Model.GetChangeSet().HasUnsavedApplied());
		const FACFCameraTuningChangeSet::FReport Saved = Model.SaveChanges();
		TestTrue(FString::Printf(TEXT("14. Saved (%s)"), *Saved.Message), Saved.bOk && Saved.NumDone == 1);
		TestFalse(TEXT("14. Saved: package clean"), Package->IsDirty());
		TestFalse(TEXT("14. Saved: nothing unsaved"), Model.GetChangeSet().HasUnsavedApplied());
		TestTrue(TEXT("Save did not save the unrelated dirty package"), Bystander->IsDirty());

		// Undo after the save: memory goes back, the file does not; the package is dirty again (unsaved).
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Filename);
		TestTrue(TEXT("Undo after save"), GEditor->UndoTransaction());
		TestEqual(TEXT("Undo: memory back to the base"), NodeAt<UACFCameraNode_FixedPose>(Asset, 1)->FieldOfView, 90.f);
		TestTrue(TEXT("Undo: package dirty (memory differs from disk)"), Package->IsDirty());
		TestEqual(TEXT("Undo: the file was not rewritten"), IFileManager::Get().GetTimeStamp(*Filename), Stamp);
		TestTrue(TEXT("Redo"), GEditor->RedoTransaction());
		Tuned.Director->StopAllTuning();
	}
	// 14. Reload from disk (the editor's own reload): the saved value comes back.
	GEditor->ResetTransaction(FText::FromString(TEXT("ACF tuning apply test")));
	Package->SetDirtyFlag(false); // the redo restored the saved value; reload without a prompt
	FText ReloadError;
	TestTrue(TEXT("14. Reload the package"), UPackageTools::ReloadPackages({ Package }, ReloadError, EReloadPackagesInteractionMode::AssumePositive));
	const UACFCameraPreset* Reloaded = Cast<UACFCameraPreset>(AssetPath.TryLoad());
	if (TestNotNull(TEXT("14. Reloaded asset"), Reloaded))
	{
		TestEqual(TEXT("14. Reloaded: the saved FOV"), NodeAt<UACFCameraNode_FixedPose>(Reloaded, 1)->FieldOfView, 65.f);
		TestEqual(TEXT("14. Reloaded: the other values"), NodeAt<UACFCameraNode_Follow>(Reloaded, 0)->Offset, FVector(-300.0, 0.0, 150.0));
	}

	// 15. Read-only file: not saved, reported, still dirty, file untouched.
	UACFCameraPreset* Current = const_cast<UACFCameraPreset*>(Reloaded);
	if (Current)
	{
		FTuned Tuned(Current);
		Tuned.Set(2, CollisionClass, EParam::CollisionRadius, FVector(45.0, 0.0, 0.0));
		TestTrue(TEXT("15. Applied"), Tuned.Model->ApplyChanges(false).bOk);
		FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, true);
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Filename);
		const FACFCameraTuningChangeSet::FReport Failed = Tuned.Model->SaveChanges();
		TestFalse(TEXT("15. Read-only: not reported as saved"), Failed.bOk);
		TestTrue(FString::Printf(TEXT("15. Read-only: the reason is given (%s)"), *Failed.Message), Failed.Message.Contains(TEXT("read-only")) && Failed.Message.Contains(TEXT("NOT saved")));
		TestTrue(TEXT("15. Read-only: package still dirty"), Current->GetPackage()->IsDirty());
		TestTrue(TEXT("15. Read-only: shown as unsaved"), Tuned.Model->GetChangeSet().HasUnsavedApplied());
		TestEqual(TEXT("15. Read-only: file untouched"), IFileManager::Get().GetTimeStamp(*Filename), Stamp);
		FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, false);
		Tuned.Director->StopAllTuning();
		DropAsset(Current);
	}
	// Not an asset on disk: refused with a reason.
	TStrongObjectPtr<UACFCameraPreset> TransientPreset(NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient));
	TestFalse(TEXT("A transient preset cannot be saved"), FACFCameraTuningChangeSet::SavePackage(*TransientPreset->GetPackage(), Error));
	Bystander->SetDirtyFlag(false);
	GEditor->ResetTransaction(FText::FromString(TEXT("ACF tuning apply test")));
	return true;
}

// 18 + the effect of Apply on running requests: capturing never disturbs the camera; Apply does not touch the tuned
// request (its copy) and acts on an untuned request of the same asset exactly like a plain property edit (no state reset).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningApplyRunningTest, "ActionCameraFramework.Editor.TuningApply.RunningRequestsKeepTheirState", ACFTuningApplyTests::Flags)
bool FACFTuningApplyRunningTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningApplyTests;
	FTestRoot TestRoot;
	TestRoot.Test = this;
	TStrongObjectPtr<UACFCameraPreset> Asset(MakeAsset(TEXT("DA_Running")));
	NodeAt<UACFCameraNode_Follow>(Asset.Get(), 0)->Damping = FACFCameraPositionDamping::Uniform(0.3f, EACFCameraDampingSpace::World);
	// A reference asset, identical, edited directly (what Apply should amount to for the untuned request).
	TStrongObjectPtr<UACFCameraPreset> Reference(DuplicateObject<UACFCameraPreset>(Asset.Get(), GetTransientPackage()));

	FTuned Tuned(Asset.Get());            // tuned, captured every frame
	FTuned TunedRef(Asset.Get(), false);  // tuned the same way, never captured
	TunedRef.Director->StartTuning(TunedRef.Handle);
	FTuned Untuned(Asset.Get(), false);   // the asset, untuned (another request / player using the shared preset)
	FTuned UntunedRef(Reference.Get(), false);
	for (FTuned* T : { &Tuned, &TunedRef })
	{
		T->Director->SetTuningValue(T->Handle, EParam::CameraOffset, FVector(-450.0, 30.0, 180.0), 0);
		T->Director->SetTuningValue(T->Handle, EParam::FollowDamping, FVector(0.1, 0.1, 0.1), 0);
	}
	FDriver Driver;
	bool bTunedSame = true;
	bool bUntunedSame = true;
	auto Run = [&](int32 From, int32 To, bool bCapture)
	{
		for (int32 Frame = From; Frame < To; ++Frame)
		{
			const FVector Location(Frame * 6.0, FMath::Sin(Frame * 0.07) * 120.0, 0.0);
			for (FTuned* T : { &Tuned, &TunedRef, &Untuned, &UntunedRef })
			{
				T->Target->SetWorldLocation(Location);
			}
			FDriver D1 = Driver, D2 = Driver, D3 = Driver, D4 = Driver;
			const FACFCameraPose A = D1.Step(Tuned.Director.Get()).Pose;
			const FACFCameraPose B = D2.Step(TunedRef.Director.Get()).Pose;
			const FACFCameraPose C = D3.Step(Untuned.Director.Get()).Pose;
			const FACFCameraPose D = D4.Step(UntunedRef.Director.Get()).Pose;
			Driver.Frame = D1.Frame;
			bTunedSame &= A.Location == B.Location && A.Rotation == B.Rotation && A.FieldOfView == B.FieldOfView;
			bUntunedSame &= C.Location == D.Location && C.Rotation == D.Rotation && C.FieldOfView == D.FieldOfView;
			if (bCapture)
			{
				Tuned.Model->CaptureAll();
			}
		}
	};
	Run(0, 60, true);
	TestTrue(TEXT("18. Capturing every frame: the tuned camera is bit for bit the uncaptured one"), bTunedSame);
	TestTrue(TEXT("Before Apply: the untuned cameras match"), bUntunedSame);

	const FACFCameraTuningChangeSet::FReport Report = Tuned.Model->ApplyChanges(false);
	TestTrue(FString::Printf(TEXT("Applied (%s)"), *Report.Message), Report.bOk && Report.NumDone == 2);
	// The same edit on the reference asset, at the same point in time.
	ACFCameraTuning::Write(*Reference->Nodes[0], EParam::CameraOffset, FVector(-450.0, 30.0, 180.0));
	ACFCameraTuning::Write(*Reference->Nodes[0], EParam::FollowDamping, FVector(0.1, 0.1, 0.1));
	Run(60, 120, true);
	TestTrue(TEXT("After Apply: the tuned request (on its copy) is unaffected"), bTunedSame);
	TestTrue(TEXT("After Apply: the untuned request behaves exactly like a direct edit of its asset (damping state kept)"), bUntunedSame);
	TestTrue(TEXT("The tuned request still runs on its copy"), Tuned.Director->GetRequestPreset(Tuned.Handle) != Asset.Get());

	for (FTuned* T : { &Tuned, &TunedRef })
	{
		T->Director->StopAllTuning();
	}
	GEditor->ResetTransaction(FText::FromString(TEXT("ACF tuning apply test")));
	DropAsset(Asset.Get());
	return true;
}

// -------------------------------------------------------------------------------------------------------------------
// 17 + the editor flow on the real PIE: tune in PIE, Apply is refused while PIE runs, PIE ends (the change set keeps the
// values, no reference to the PIE world remains), Apply / Undo / Redo / Save in the editor, PIE again uses the new asset.

namespace ACFTuningApplyTests
{
	struct FPieFlow
	{
		FAutomationTestBase* Test = nullptr;
		TUniquePtr<FTestRoot> TestRoot;
		TUniquePtr<FACFCameraTuningPanelModel> Model;
		TStrongObjectPtr<UACFCameraPreset> Asset;
		FACFCameraRequestHandle Handle;
		uint64 MarkFrame = 0;
	};
	using FFlowStep = TFunction<bool(FPieFlow&)>; // true = done, false = wait a frame

	class FFlowCommand : public IAutomationLatentCommand
	{
	public:
		FFlowCommand(TSharedRef<FPieFlow> InFlow, TArray<FFlowStep> InSteps) : Flow(InFlow), Steps(MoveTemp(InSteps)) {}
		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			StepStart = StepStart == 0.0 ? Now : StepStart;
			if (Now - StepStart > 90.0)
			{
				Flow->Test->AddError(FString::Printf(TEXT("PIE apply flow: step %d timed out."), Index));
				return true;
			}
			if (Index < Steps.Num() && Steps[Index](*Flow))
			{
				++Index;
				StepStart = 0.0;
			}
			return Index >= Steps.Num();
		}
	private:
		TSharedRef<FPieFlow> Flow;
		TArray<FFlowStep> Steps;
		int32 Index = 0;
		double StepStart = 0.0;
	};

	inline APlayerCameraManager* FirstPiePcm(UWorld*& OutWorld, ULocalPlayer*& OutPlayer)
	{
		TArray<FACFCameraTuningPanelModel::FWorldEntry> Worlds;
		FACFCameraTuningPanelModel::GetPIEWorlds(Worlds);
		OutWorld = Worlds.Num() > 0 ? Worlds[0].World.Get() : nullptr;
		TArray<FACFCameraTuningPanelModel::FPlayerEntry> Players;
		FACFCameraTuningPanelModel::GetPlayers(OutWorld, Players);
		OutPlayer = Players.Num() > 0 ? Players[0].Player.Get() : nullptr;
		const APlayerController* PC = OutPlayer ? OutPlayer->GetPlayerController(OutWorld) : nullptr;
		return PC ? PC->PlayerCameraManager.Get() : nullptr;
	}

	inline bool RequestOnPie(FPieFlow& F)
	{
		UWorld* World = nullptr;
		ULocalPlayer* Player = nullptr;
		if (!FirstPiePcm(World, Player))
		{
			return false; // PIE still starting
		}
		F.Model->SelectWorld(World);
		F.Model->SelectPlayer(Player);
		F.Handle = Player->GetSubsystem<UACFCameraSubsystem>()->RequestCamera(F.Asset.Get(), FACFCameraRequestParams());
		F.MarkFrame = GFrameCounter;
		return true;
	}

	inline bool NoPie(FPieFlow& F)
	{
		TArray<FACFCameraTuningPanelModel::FWorldEntry> Worlds;
		FACFCameraTuningPanelModel::GetPIEWorlds(Worlds);
		return Worlds.IsEmpty() && !(GEditor && GEditor->PlayWorld) && !FACFCameraTuningChangeSet::IsPlaySessionRunning();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningApplyPieTest, "ActionCameraFramework.Editor.TuningApply.RealPIEApplyUndoSaveRestart", ACFTuningApplyTests::Flags)
bool FACFTuningApplyPieTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningApplyTests;
	TSharedRef<FPieFlow> Flow = MakeShared<FPieFlow>();
	Flow->Test = this;
	Flow->TestRoot = MakeUnique<FTestRoot>();
	Flow->TestRoot->Test = this;
	Flow->Model = MakeUnique<FACFCameraTuningPanelModel>();
	Flow->Asset.Reset(MakeAsset(TEXT("DA_Pie"), EKind::NoCollision)); // Follow (no target: stays put) + FOV-only Fixed Pose 90

	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/ActionCameraFramework/Samples/L_ACFCameraTest")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	{
		TArray<FFlowStep> Steps;
		Steps.Add([](FPieFlow& F) { return RequestOnPie(F); });
		Steps.Add([](FPieFlow& F) { return GFrameCounter > F.MarkFrame + 5; });
		Steps.Add([](FPieFlow& F)
		{
			F.Model->SelectRequest(F.Handle);
			F.Test->TestEqual(TEXT("PIE: Start"), F.Model->Start(), EACFCameraTuningResult::Ok);
			F.Model->SelectNode(1, LensClass);
			F.Test->TestEqual(TEXT("PIE: FOV 55"), F.Model->SetValue(EACFCameraTuningParameter::FieldOfView, FVector(55.0, 0.0, 0.0)), EACFCameraTuningResult::Ok);
			const FACFCameraTuningChangeSet::FReport Refused = F.Model->ApplyChanges(true);
			F.Test->TestFalse(TEXT("PIE running: Apply refused"), Refused.bOk);
			F.Test->TestTrue(FString::Printf(TEXT("PIE running: the reason is given (%s)"), *Refused.Message), Refused.Message.Contains(TEXT("play session")));
			F.Test->TestEqual(TEXT("PIE running: the asset is untouched"), NodeAt<UACFCameraNode_FixedPose>(F.Asset.Get(), 1)->FieldOfView, 90.f);
			F.MarkFrame = GFrameCounter;
			return true;
		});
		Steps.Add([](FPieFlow& F) { return GFrameCounter > F.MarkFrame + 5; });
		Steps.Add([](FPieFlow& F)
		{
			UWorld* World = nullptr;
			ULocalPlayer* Player = nullptr;
			APlayerCameraManager* PCM = FirstPiePcm(World, Player);
			F.Test->TestTrue(FString::Printf(TEXT("PIE: the tuned FOV on the real camera (%.2f)"), PCM ? PCM->GetFOVAngle() : -1.f), PCM && FMath::IsNearlyEqual(PCM->GetFOVAngle(), 55.f, 0.01f));
			return true;
		});
		ADD_LATENT_AUTOMATION_COMMAND(FFlowCommand(Flow, MoveTemp(Steps)));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	{
		TArray<FFlowStep> Steps;
		Steps.Add([](FPieFlow& F) { return NoPie(F); });
		Steps.Add([](FPieFlow& F)
		{
			FACFCameraTuningPanelModel& Model = *F.Model;
			Model.Validate();
			F.Test->TestFalse(TEXT("17. PIE ended: the target is dropped"), Model.GetWorld() != nullptr || Model.GetPlayer() != nullptr);
			F.Test->TestEqual(TEXT("17. PIE ended: the change is kept (captured before the world went away)"), Model.GetChangeSet().CountState(EState::Pending, true), 1);
			const FACFCameraTuningChangeSet::FChange* Change = Model.GetChangeSet().GetChanges().FindByPredicate([](const FACFCameraTuningChangeSet::FChange& C) { return C.IsChanged(); });
			F.Test->TestTrue(TEXT("17. The change refers to the asset by path only"), Change && Change->Preset == FSoftObjectPath(F.Asset.Get()) && Change->Value.X == 55.0);
			F.Test->TestNull(TEXT("17. Nothing resolves to the ended PIE"), Model.ResolveDirector());

			const FACFCameraTuningChangeSet::FReport Applied = Model.ApplyChanges(false);
			F.Test->TestTrue(FString::Printf(TEXT("After PIE: Apply (%s)"), *Applied.Message), Applied.bOk);
			F.Test->TestEqual(TEXT("After PIE: asset FOV"), NodeAt<UACFCameraNode_FixedPose>(F.Asset.Get(), 1)->FieldOfView, 55.f);
			F.Test->TestTrue(TEXT("After PIE: Undo"), GEditor->UndoTransaction());
			F.Test->TestEqual(TEXT("After PIE: Undo restores 90"), NodeAt<UACFCameraNode_FixedPose>(F.Asset.Get(), 1)->FieldOfView, 90.f);
			F.Test->TestTrue(TEXT("After PIE: Redo"), GEditor->RedoTransaction());
			F.Test->TestEqual(TEXT("After PIE: Redo gives 55"), NodeAt<UACFCameraNode_FixedPose>(F.Asset.Get(), 1)->FieldOfView, 55.f);
			const FACFCameraTuningChangeSet::FReport Saved = Model.SaveChanges();
			F.Test->TestTrue(FString::Printf(TEXT("After PIE: Save (%s)"), *Saved.Message), Saved.bOk && !F.Asset->GetPackage()->IsDirty());
			return true;
		});
		ADD_LATENT_AUTOMATION_COMMAND(FFlowCommand(Flow, MoveTemp(Steps)));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	{
		TArray<FFlowStep> Steps;
		Steps.Add([](FPieFlow& F) { return RequestOnPie(F); });
		Steps.Add([](FPieFlow& F) { return GFrameCounter > F.MarkFrame + 5; });
		Steps.Add([](FPieFlow& F)
		{
			UWorld* World = nullptr;
			ULocalPlayer* Player = nullptr;
			APlayerCameraManager* PCM = FirstPiePcm(World, Player);
			F.Test->TestTrue(FString::Printf(TEXT("PIE again: the applied asset FOV on the camera (%.2f)"), PCM ? PCM->GetFOVAngle() : -1.f), PCM && FMath::IsNearlyEqual(PCM->GetFOVAngle(), 55.f, 0.01f));
			F.Test->TestEqual(TEXT("PIE again: nothing is being tuned"), Player->GetSubsystem<UACFCameraSubsystem>()->GetDirector()->GetNumTunedRequests(), 0);
			return true;
		});
		ADD_LATENT_AUTOMATION_COMMAND(FFlowCommand(Flow, MoveTemp(Steps)));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	{
		TArray<FFlowStep> Steps;
		Steps.Add([](FPieFlow& F) { return NoPie(F); });
		Steps.Add([](FPieFlow& F)
		{
			GEditor->ResetTransaction(FText::FromString(TEXT("ACF tuning apply test")));
			DropAsset(F.Asset.Get());
			F.Asset.Reset();
			F.Model.Reset();
			F.TestRoot.Reset();
			return true;
		});
		ADD_LATENT_AUTOMATION_COMMAND(FFlowCommand(Flow, MoveTemp(Steps)));
	}
	return true;
}

// The real panel widget (Changes section) driven through its handlers: rows, states, Apply / Save / Clear, the summary.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTuningApplyWidgetTest, "ActionCameraFramework.Editor.TuningApply.WidgetChangesSection", ACFTuningApplyTests::Flags)
bool FACFTuningApplyWidgetTest::RunTest(const FString& Parameters)
{
	using namespace ACFTuningApplyTests;
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("Skipped: Slate is not initialized."));
		return true;
	}
	FTestRoot TestRoot;
	TestRoot.Test = this;
	UACFCameraPreset* Asset = MakeAsset(TEXT("DA_Widget"));
	TStrongObjectPtr<UACFCameraPreset> Keep(Asset);
	FString Error;
	TestTrue(TEXT("Initial save"), FACFCameraTuningChangeSet::SavePackage(*Asset->GetPackage(), Error));
	FTuned Rig(Asset, false);
	TSharedRef<SACFCameraTuningPanel> Panel = SNew(SACFCameraTuningPanel).DirectorForTesting(Rig.Director.Get());
	FACFCameraRequestInfo Info;
	Info.Handle = Rig.Handle;
	Panel->OnRequestSelected(MakeShared<FACFCameraRequestInfo>(Info), ESelectInfo::OnMouseClick);
	Panel->OnStart();
	Panel->Refresh();
	TestEqual(TEXT("No change: no row"), Panel->GetNumChangeRows(), 0);
	TestFalse(TEXT("No change: Apply disabled"), Panel->CanApply());

	Panel->GetModel().SelectNode(1, LensClass);
	Panel->OnComponentCommitted(60.0, ETextCommit::OnEnter, EACFCameraTuningParameter::FieldOfView, 0);
	Panel->Refresh();
	TestEqual(TEXT("One changed parameter listed"), Panel->GetNumChangeRows(), 1);
	TestTrue(TEXT("Apply enabled"), Panel->CanApply());
	const int32 Row = Panel->GetModel().GetChangeSet().GetChanges().IndexOfByPredicate([](const FACFCameraTuningChangeSet::FChange& C) { return C.IsChanged(); });
	TestEqual(TEXT("State: not applied"), Panel->GetChangeStateText(Row).ToString(), FString(TEXT("Not applied")));

	Panel->OnApplyChanges();
	TestEqual(TEXT("Applied through the button"), NodeAt<UACFCameraNode_FixedPose>(Asset, 1)->FieldOfView, 60.f);
	TestEqual(TEXT("State: applied, not saved"), Panel->GetChangeStateText(Row).ToString(), FString(TEXT("Applied, NOT saved")));
	TestTrue(TEXT("The summary warns about unsaved changes"), Panel->GetChangesSummary().ToString().Contains(TEXT("Unsaved")));
	TestTrue(TEXT("The result names the unsaved package"), Panel->GetValidationText().Contains(TEXT("UNSAVED")));
	TestFalse(TEXT("Nothing left to apply"), Panel->CanApply());

	Panel->OnSaveChanges();
	TestEqual(TEXT("State: saved"), Panel->GetChangeStateText(Row).ToString(), FString(TEXT("Applied (asset saved)")));
	TestTrue(TEXT("The last message reports the save"), Panel->GetModel().GetLastMessage().Contains(TEXT("Saved 1 package")));

	GEditor->UndoTransaction();
	TestEqual(TEXT("Undo: the row shows not applied again"), Panel->GetChangeStateText(Row).ToString(), FString(TEXT("Not applied")));
	GEditor->RedoTransaction();

	Panel->OnClearChanges();
	TestEqual(TEXT("Clear List: no row"), Panel->GetNumChangeRows(), 0);
	TestEqual(TEXT("Clear List: the asset keeps the applied value"), NodeAt<UACFCameraNode_FixedPose>(Asset, 1)->FieldOfView, 60.f);
	Rig.Director->StopAllTuning();
	GEditor->ResetTransaction(FText::FromString(TEXT("ACF tuning apply test")));
	DropAsset(Asset);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
