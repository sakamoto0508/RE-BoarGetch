#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFTransitionPreset.h"
#include "ACFTransitionSubsystem.h"
#include "ACFTransitionView.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SACFTransitionPreviewPanel.h"
#include "Tests/AutomationEditorCommon.h"
#include "ToolMenus.h"
#include "UObject/Package.h"

/** Phase 7D: the Transition Preview tab and level transitions in PIE (README 33 章「検証」). Editor context. */
namespace ACFTransitionEditorTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionPreviewPanelTest, "ActionCameraFramework.Editor.Transitions.PreviewPanel", ACFTransitionEditorTests::Flags)
bool FACFTransitionPreviewPanelTest::RunTest(const FString&)
{
	UToolMenu* Toolbar = UToolMenus::Get()->FindMenu("AssetEditor.BlueprintEditor.ToolBar");
	const FToolMenuSection* Section = Toolbar ? Toolbar->FindSection("ACF") : nullptr;
	TestTrue(TEXT("Blueprint toolbar includes a transition preview entry"), Section && Section->FindEntry("ACFTransitionPreview"));
	TSharedRef<SACFTransitionPreviewPanel> Panel = SNew(SACFTransitionPreviewPanel);
	for (int32 Id = 1; Id <= ACFTransition::NumEffects; ++Id)
	{
		const EACFTransitionEffect Effect = ACFTransition::FromId(Id);
		Panel->SelectEffect(Effect);
		TestEqual(TEXT("Selected"), Panel->GetSelectedEffect(), Effect);
		TestEqual(TEXT("Settings follow the selection"), Panel->GetSettings()->Close.Effect, Effect);
		Panel->Play(true, true);
		float MaxCoverage = 0.f;
		for (int32 Frame = 0; Frame < 2000 && Panel->IsPlaying(); ++Frame)
		{
			Panel->Advance(1.f / 60.f);
			MaxCoverage = FMath::Max(MaxCoverage, Panel->GetCoverage());
		}
		TestFalse(*FString::Printf(TEXT("%02d preview ends"), Id), Panel->IsPlaying());
		TestEqual(*FString::Printf(TEXT("%02d preview reached full cover"), Id), MaxCoverage, 1.f);
		TestEqual(*FString::Printf(TEXT("%02d preview ends clear"), Id), Panel->GetCoverage(), 0.f);
		UMaterialInstanceDynamic* Mid = Panel->GetView() ? Panel->GetView()->GetMaterialInstance() : nullptr;
		if (TestNotNull(TEXT("Preview material instance"), Mid))
		{
			float Value = 0.f;
			Mid->GetScalarParameterValue(FName(TEXT("Effect")), Value);
			TestEqual(*FString::Printf(TEXT("%02d effect parameter"), Id), FMath::RoundToInt(Value), Id);
		}
	}
	// Changing a setting and playing again uses it.
	Panel->SelectEffect(EACFTransitionEffect::SpiralClose);
	Panel->GetSettings()->Close.Duration = 0.1f;
	Panel->Play(true, false);
	Panel->Advance(0.2f);
	TestEqual(TEXT("Close only: covered and stopped"), Panel->GetCoverage(), 1.f);
	TestFalse(TEXT("Close only stops"), Panel->IsPlaying());
	UACFTransitionPreset* Preset = NewObject<UACFTransitionPreset>();
	Preset->Close = ACFTransition::MakeDefaultSettings(EACFTransitionEffect::SpiralClose);
	Preset->Close.Duration = 0.25f;
	Preset->Open = ACFTransition::MakeDefaultSettings(EACFTransitionEffect::BlackFade, EACFTransitionPhase::Open);
	Panel->LoadPreset(*Preset);
	TestTrue(TEXT("Loading a preset starts the preview"), Panel->IsPlaying());
	TestEqual(TEXT("Custom close duration"), Panel->GetSettings()->Close.Duration, 0.25f);
	TestEqual(TEXT("Separate open effect"), Panel->GetSettings()->Open.Effect, EACFTransitionEffect::BlackFade);
	Panel->GetSettings()->Close.Duration = 0.5f;
	TestEqual(TEXT("Editing preview settings leaves the preset intact"), Preset->Close.Duration, 0.25f);
	TestFalse(TEXT("Play in PIE without PIE"), Panel->PlayInPie());
	return true;
}

namespace ACFTransitionPieTravel
{
	struct FState
	{
		FAutomationTestBase* Test = nullptr;
		FACFTransitionHandle Handle;
		TArray<TPair<FACFTransitionHandle, EACFTransitionEvent>> Events; // all handles: Started is sent before PlayLevelTransition returns
		FDelegateHandle Binding;
		TWeakObjectPtr<UACFTransitionSubsystem> S;
		int32 Step = 0;
		int32 Frames = 0;
		float CoverageAtLoad = -1.f;
		bool bOverlayAtLoad = false;
		FDelegateHandle Pre;
		~FState() { FCoreUObjectDelegates::PreLoadMap.Remove(Pre); }
	};

	class FCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCommand(TSharedRef<FState> InState) : State(InState) {}
		virtual bool Update() override
		{
			FState& St = *State;
			if (GetCurrentRunTime() > 60.0)
			{
				St.Test->AddError(TEXT("PIE travel timed out."));
				return true;
			}
			UWorld* World = GEditor ? GEditor->PlayWorld.Get() : nullptr;
			if (!World || !World->GetGameInstance())
			{
				return false;
			}
			UACFTransitionSubsystem* S = World->GetGameInstance()->GetSubsystem<UACFTransitionSubsystem>();
			if (!S)
			{
				return false;
			}
			switch (St.Step)
			{
			case 0:
				if (++St.Frames < 10) { return false; }
				St.S = S;
				St.Binding = S->OnTransitionEventNative.AddLambda([Raw = &St](FACFTransitionHandle H, EACFTransitionEvent E) { Raw->Events.Emplace(H, E); });
				St.Pre = FCoreUObjectDelegates::PreLoadMap.AddLambda([Raw = &St](const FString&)
				{
					if (UACFTransitionSubsystem* Sub = Raw->S.Get())
					{
						Raw->CoverageAtLoad = Sub->GetCoverage();
						Raw->bOverlayAtLoad = Sub->IsOverlayAttached();
					}
				});
				St.Handle = S->PlayLevelTransition(ACFTransition::MakeDefaultSettings(EACFTransitionEffect::SpiralClose), ACFTransition::MakeDefaultSettings(EACFTransitionEffect::SpiralOpen, EACFTransitionPhase::Open), FACFTransitionOptions(), FName(TEXT("/ActionCameraFramework/Transitions/Samples/L_ACFTransitionB")), FString());
				St.Step = 1;
				return false;
			case 1:
			{
				const FACFTransitionInfo Info = S->GetTransitionInfo(St.Handle);
				if (Info.bActive || !ACFTransition::IsTerminal(Info.LastEvent))
				{
					return false;
				}
				FString Seq;
				for (const auto& E : St.Events) { if (E.Key == St.Handle) { Seq += (Seq.IsEmpty() ? TEXT("") : TEXT(",")) + FString(ACFTransition::LexEvent(E.Value)); } }
				St.Test->TestEqual(TEXT("PIE travel events"), Seq, FString(TEXT("Started,Covered,LoadStarted,LoadCompleted,Finished")));
				St.Test->TestTrue(TEXT("PIE: arrived in L_ACFTransitionB"), World->GetOutermost()->GetName().Contains(TEXT("L_ACFTransitionB")));
				St.Test->TestEqual(TEXT("PIE: covered at the load"), St.CoverageAtLoad, 1.f);
				St.Test->TestTrue(TEXT("PIE: overlay in the viewport at the load"), St.bOverlayAtLoad);
				St.Test->TestEqual(TEXT("PIE: clear at the end"), S->GetScreenState(), EACFTransitionScreenState::Clear);
				S->OnTransitionEventNative.Remove(St.Binding);
				return true;
			}
			default:
				return true;
			}
		}
	private:
		TSharedRef<FState> State;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFTransitionPieTravelTest, "ActionCameraFramework.Editor.Transitions.PieLevelTravel", ACFTransitionEditorTests::Flags)
bool FACFTransitionPieTravelTest::RunTest(const FString&)
{
	using namespace ACFTransitionPieTravel;
	TSharedRef<FState> State = MakeShared<FState>();
	State->Test = this;
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/ActionCameraFramework/Transitions/Samples/L_ACFTransitionA")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
