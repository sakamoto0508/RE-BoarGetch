#pragma once

#include "ACFTransitionCatalog.h"
#include "ACFTransitionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

/** Shared helpers of the Phase 7D transition tests (README 33 章「検証」). */
namespace ACFTransitionTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	inline UWorld* FindGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	inline UACFTransitionSubsystem* FindSubsystem()
	{
		UWorld* World = FindGameWorld();
		return World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UACFTransitionSubsystem>() : nullptr;
	}

	/** Records every event of the subsystem. */
	struct FRecorder
	{
		TWeakObjectPtr<UACFTransitionSubsystem> Subsystem;
		FDelegateHandle Binding;
		TArray<TPair<FACFTransitionHandle, EACFTransitionEvent>> Events;

		void Bind(UACFTransitionSubsystem* S)
		{
			Subsystem = S;
			Binding = S->OnTransitionEventNative.AddLambda([this](FACFTransitionHandle H, EACFTransitionEvent E) { Events.Emplace(H, E); });
		}
		~FRecorder()
		{
			if (UACFTransitionSubsystem* S = Subsystem.Get())
			{
				S->OnTransitionEventNative.Remove(Binding);
			}
		}
		FString Of(FACFTransitionHandle Handle) const
		{
			FString Out;
			for (const auto& Pair : Events)
			{
				if (Pair.Key == Handle)
				{
					Out += (Out.IsEmpty() ? TEXT("") : TEXT(",")) + FString(ACFTransition::LexEvent(Pair.Value));
				}
			}
			return Out;
		}
		bool Has(FACFTransitionHandle Handle, EACFTransitionEvent Event) const
		{
			return Events.ContainsByPredicate([&](const auto& P) { return P.Key == Handle && P.Value == Event; });
		}
	};

	inline FACFTransitionSettings Settings(EACFTransitionEffect Effect, float Duration, EACFTransitionPhase Phase = EACFTransitionPhase::Close)
	{
		FACFTransitionSettings S = ACFTransition::MakeDefaultSettings(Effect, Phase);
		S.Duration = Duration;
		S.Easing = EACFTransitionEasing::Linear;
		return S;
	}

	/** Runs steps (each returns true when done) one per frame, with a time limit. */
	class FSteps : public IAutomationLatentCommand
	{
	public:
		FSteps(FAutomationTestBase* InTest, TArray<TFunction<bool()>> InSteps, TFunction<void()> InCleanup, double InLimit = 30.0)
			: Test(InTest), Steps(MoveTemp(InSteps)), Cleanup(MoveTemp(InCleanup)), Limit(InLimit) {}

		virtual bool Update() override
		{
			if (GetCurrentRunTime() > Limit)
			{
				Test->AddError(FString::Printf(TEXT("Timed out in step %d of %d."), Index + 1, Steps.Num()));
				Finish();
				return true;
			}
			if (Index < Steps.Num() && Steps[Index]())
			{
				++Index;
			}
			if (Index >= Steps.Num())
			{
				Finish();
				return true;
			}
			return false;
		}

	private:
		void Finish()
		{
			if (Cleanup)
			{
				Cleanup();
				Cleanup = nullptr;
			}
		}

		FAutomationTestBase* Test;
		TArray<TFunction<bool()>> Steps;
		TFunction<void()> Cleanup;
		double Limit;
		int32 Index = 0;
	};
}
