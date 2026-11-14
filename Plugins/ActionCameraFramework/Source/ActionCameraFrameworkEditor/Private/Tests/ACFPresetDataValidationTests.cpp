#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraPreset.h"
#include "ACFCameraPresetValidation.h"
#include "ACFCameraSequence.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "DataValidationSettings.h"
#include "Editor.h"
#include "EditorValidatorSubsystem.h"
#include "Misc/DataValidation.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/** Phase 6B / B-8: preset validation through the editor's Data Validation (the path used on save and by "Validate Assets"). */
namespace ACFPresetEditorTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	template <typename T>
	T* AddNode(UACFCameraPreset* Preset)
	{
		T* Node = NewObject<T>(Preset);
		Preset->Nodes.Add(Node);
		return Node;
	}

	inline UACFCameraPreset* MakePreset()
	{
		return NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
	}

	inline FString Text(const FDataValidationContext::FIssue& Issue)
	{
		return (Issue.TokenizedMessage ? Issue.TokenizedMessage->ToText() : Issue.Message).ToString();
	}

	inline bool HasIssue(const FDataValidationContext& Context, EMessageSeverity::Type Severity, const TCHAR* Id)
	{
		return Context.GetIssues().ContainsByPredicate([Severity, Id](const FDataValidationContext::FIssue& Issue)
		{
			return Issue.Severity == Severity && Text(Issue).Contains(Id);
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFPresetDataValidationPathTest, "ActionCameraFramework.Editor.PresetDataValidationPath", ACFPresetEditorTests::Flags)
bool FACFPresetDataValidationPathTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetEditorTests;
	UEditorValidatorSubsystem* Validator = GEditor ? GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("Editor Validator Subsystem (DataValidation plugin)"), Validator))
	{
		return false;
	}
	TestTrue(TEXT("Validate On Save is enabled for this project"), GetDefault<UDataValidationSettings>()->bValidateOnSave);

	TStrongObjectPtr<UACFCameraPreset> Broken(MakePreset());
	AddNode<UACFCameraNode_FixedPose>(Broken.Get())->FieldOfView = 200.f;
	TStrongObjectPtr<UACFCameraPreset> Suspicious(MakePreset());
	UACFCameraNode_OrbitalFollow* Orbit = AddNode<UACFCameraNode_OrbitalFollow>(Suspicious.Get());
	Orbit->PitchMin = 40.f;
	Orbit->PitchMax = -40.f;
	TStrongObjectPtr<UACFCameraPreset> Good(MakePreset());
	AddNode<UACFCameraNode_OrbitalFollow>(Good.Get());
	AddNode<UACFCameraNode_Collision>(Good.Get());

	for (const EDataValidationUsecase Usecase : { EDataValidationUsecase::Save, EDataValidationUsecase::Manual })
	{
		const TCHAR* Name = Usecase == EDataValidationUsecase::Save ? TEXT("Save") : TEXT("Manual");
		{
			FDataValidationContext Context(false, Usecase, {});
			TestEqual(FString::Printf(TEXT("%s: broken preset is Invalid"), Name), Validator->IsObjectValidWithContext(Broken.Get(), Context), EDataValidationResult::Invalid);
			TestTrue(FString::Printf(TEXT("%s: the FOV error arrives with its id"), Name), HasIssue(Context, EMessageSeverity::Error, TEXT("[ACF.Param.InvalidFov]")));
		}
		{
			FDataValidationContext Context(false, Usecase, {});
			TestEqual(FString::Printf(TEXT("%s: warnings do not fail validation"), Name), Validator->IsObjectValidWithContext(Suspicious.Get(), Context), EDataValidationResult::Valid);
			TestTrue(FString::Printf(TEXT("%s: inverted pitch limits are a warning"), Name), HasIssue(Context, EMessageSeverity::Warning, TEXT("[ACF.Param.MinMaxInverted]")));
			TestTrue(FString::Printf(TEXT("%s: missing Collision is an info note"), Name), HasIssue(Context, EMessageSeverity::Info, TEXT("[ACF.Collision.None]")));
			TestEqual(FString::Printf(TEXT("%s: no errors"), Name), static_cast<int32>(Context.GetNumErrors()), 0);
		}
		{
			FDataValidationContext Context(false, Usecase, {});
			TestEqual(FString::Printf(TEXT("%s: good preset is Valid"), Name), Validator->IsObjectValidWithContext(Good.Get(), Context), EDataValidationResult::Valid);
			TestEqual(FString::Printf(TEXT("%s: good preset has no warnings"), Name), static_cast<int32>(Context.GetNumWarnings()), 0);
			TestEqual(FString::Printf(TEXT("%s: good preset has no errors"), Name), static_cast<int32>(Context.GetNumErrors()), 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFProjectPresetsValidateTest, "ActionCameraFramework.Editor.ProjectPresetsValidate", ACFPresetEditorTests::Flags)
bool FACFProjectPresetsValidateTest::RunTest(const FString& Parameters)
{
	using namespace ACFPresetEditorTests;
	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	Registry.ScanPathsSynchronous({ TEXT("/ActionCameraFramework/"), TEXT("/Game/") }, false);

	TArray<FAssetData> Presets;
	Registry.GetAssetsByClass(UACFCameraPreset::StaticClass()->GetClassPathName(), Presets, true);
	TestTrue(TEXT("Project / plugin presets found"), Presets.Num() > 0);
	int32 Counts[3] = { 0, 0, 0 };
	for (const FAssetData& Asset : Presets)
	{
		const UACFCameraPreset* Preset = Cast<UACFCameraPreset>(Asset.GetAsset());
		if (!TestNotNull(*FString::Printf(TEXT("Load %s"), *Asset.GetObjectPathString()), Preset))
		{
			continue;
		}
		TArray<FACFCameraPresetIssue> Issues;
		ACFCameraPresetValidation::ValidatePreset(*Preset, Issues);
		for (const FACFCameraPresetIssue& Issue : Issues)
		{
			++Counts[static_cast<int32>(Issue.Severity)];
			const FString Line = FString::Printf(TEXT("%s: %s"), *Preset->GetName(), *Issue.ToText().ToString());
			if (Issue.Severity == EACFCameraPresetIssueSeverity::Error)
			{
				AddError(Line);
			}
			else
			{
				AddInfo(Line);
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d presets: %d errors, %d warnings, %d infos"), Presets.Num(), Counts[2], Counts[1], Counts[0]));

	// Sequences: their own checks plus the transition notes between blended shots.
	TArray<FAssetData> Sequences;
	Registry.GetAssetsByClass(UACFCameraSequence::StaticClass()->GetClassPathName(), Sequences, true);
	for (const FAssetData& Asset : Sequences)
	{
		if (const UACFCameraSequence* Sequence = Cast<UACFCameraSequence>(Asset.GetAsset()))
		{
			FDataValidationContext Context;
			TestNotEqual(*FString::Printf(TEXT("%s is not Invalid"), *Sequence->GetName()), Sequence->IsDataValid(Context), EDataValidationResult::Invalid);
			for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
			{
				AddInfo(FString::Printf(TEXT("%s: %s"), *Sequence->GetName(), *Text(Issue)));
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
