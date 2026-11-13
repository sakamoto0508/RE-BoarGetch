#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ACFCameraTestUtils.h"
#include "ACFCameraSequence.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Phase 5A: 画面の所有権（出力抑制の重複、出力バイパス中の裏評価、外部カメラからの復帰ブレンド）を
 * Director 単体（ワールドなし）で確認する。Sequencer 実機との結合は ActionCameraFrameworkSequencer 側のテスト。
 */
namespace ACFAuthorityTests
{
	constexpr float StepTime = 0.125f; // float で誤差なく積める刻み
	static const FVector GameplayLoc(100.f, 0.f, 0.f);
	static const FVector ExternalLoc(0.f, 1000.f, 500.f);
	static const FVector OtherLoc(-400.f, 0.f, 200.f);

	struct FRig
	{
		TStrongObjectPtr<UACFCameraDirector> Director;
		ACFTests::FFrameDriver Driver;
		TArray<TStrongObjectPtr<UObject>> Keep;

		FRig() : Director(NewObject<UACFCameraDirector>()) {}

		UACFCameraPreset* Fixed(const FVector& Location)
		{
			UACFCameraPreset* Preset = ACFTests::MakeFixedPreset(Location, 70.f);
			Keep.Emplace(Preset);
			return Preset;
		}

		UObject* Source()
		{
			UObject* Object = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
			Keep.Emplace(Object);
			return Object;
		}

		float Weight(float DeltaTime = StepTime) { return Driver.Step(Director.Get(), DeltaTime).Weight; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAuthoritySuppressionOverlapTest, "ActionCameraFramework.Authority.SuppressionOverlapAndReleaseOrder", ACFTests::Flags)
bool FACFAuthoritySuppressionOverlapTest::RunTest(const FString& Parameters)
{
	using namespace ACFAuthorityTests;

	// 解除の順番（先入れ先出し / 後入れ先出し）のどちらでも、最後の 1 つが外れるまで出力は戻らない。
	for (const bool bReleaseFirstFirst : { true, false })
	{
		const TCHAR* Name = bReleaseFirstFirst ? TEXT("release A then B") : TEXT("release B then A");
		FRig R;
		R.Director->AddRequest(R.Fixed(GameplayLoc), FACFCameraRequestParams());
		TestEqual(TEXT("ACF in control"), R.Weight(), 1.f);
		UObject* A = R.Source();
		UObject* B = R.Source();
		R.Director->SuppressOutput(A, 0.f);
		R.Director->SuppressOutput(B, 0.f);
		R.Director->SuppressOutput(A, 0.f); // 同じ Source の重複は数えない
		TestEqual(*FString::Printf(TEXT("%s: two sources"), Name), R.Director->GetNumOutputSuppressions(), 2);
		TestEqual(*FString::Printf(TEXT("%s: suppressed"), Name), R.Weight(), 0.f);

		UObject* First = bReleaseFirstFirst ? A : B;
		UObject* Second = bReleaseFirstFirst ? B : A;
		TestTrue(*FString::Printf(TEXT("%s: first release accepted"), Name), R.Director->ReleaseOutputSuppression(First, 0.f));
		TestEqual(*FString::Printf(TEXT("%s: still suppressed by the other source"), Name), R.Weight(), 0.f);
		TestTrue(*FString::Printf(TEXT("%s: still reported suppressed"), Name), R.Director->GetLastResult().bOutputSuppressed);
		TestFalse(*FString::Printf(TEXT("%s: releasing the same source twice is rejected"), Name), R.Director->ReleaseOutputSuppression(First, 0.f));
		TestEqual(*FString::Printf(TEXT("%s: a second release of the same source does not free the other"), Name), R.Weight(), 0.f);
		TestFalse(*FString::Printf(TEXT("%s: unknown source"), Name), R.Director->ReleaseOutputSuppression(R.Source(), 0.f));
		TestTrue(*FString::Printf(TEXT("%s: last release accepted"), Name), R.Director->ReleaseOutputSuppression(Second, 0.f));
		TestEqual(*FString::Printf(TEXT("%s: output back"), Name), R.Weight(), 1.f);
		TestEqual(*FString::Printf(TEXT("%s: no source left"), Name), R.Director->GetNumOutputSuppressions(), 0);
	}

	// フェード中に要求が重なっても Weight は跳ばない。
	{
		FRig R;
		R.Director->AddRequest(R.Fixed(GameplayLoc), FACFCameraRequestParams());
		R.Weight();
		UObject* A = R.Source();
		UObject* B = R.Source();
		R.Director->SuppressOutput(A, 1.f);
		const float W1 = R.Weight(0.25f);
		TestTrue(TEXT("fade out started"), W1 > 0.f && W1 < 1.f);
		R.Director->SuppressOutput(B, 0.f); // 2 つ目はフェードをやり直さない（即時 0 にもならない）
		const float W2 = R.Weight(0.25f);
		TestTrue(TEXT("second source keeps the running fade"), W2 > 0.f && W2 < W1);
		R.Director->ReleaseOutputSuppression(A, 0.f); // 1 つ目を即時解除しても、まだ B が抑制している
		const float W3 = R.Weight(0.25f);
		TestTrue(TEXT("releasing one of two keeps fading out"), W3 < W2);
		TestEqual(TEXT("fully suppressed"), R.Weight(1.f), 0.f);
		R.Director->ReleaseOutputSuppression(B, 1.f);
		const float W4 = R.Weight(0.5f);
		TestTrue(TEXT("fade in after the last release"), W4 > 0.f && W4 < 1.f);
		R.Director->SuppressOutput(A, 1.f); // フェードインの途中で再び抑制: 現在値から下がる
		const float W5 = R.Weight(0.125f);
		TestTrue(TEXT("re-suppress continues from the current weight (no pop)"), W5 < W4 && W4 - W5 < 0.25f);
		R.Director->ReleaseOutputSuppression(A, 0.f);
		TestEqual(TEXT("back"), R.Weight(), 1.f);
	}

	// 破棄された Source は自動で外れるが、生きている Source の抑制は残る。
	{
		FRig R;
		R.Director->AddRequest(R.Fixed(GameplayLoc), FACFCameraRequestParams());
		UObject* Alive = R.Source();
		UObject* Doomed = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		R.Director->SuppressOutput(Doomed, 0.f);
		R.Director->SuppressOutput(Alive, 0.f);
		TestEqual(TEXT("suppressed by two"), R.Weight(), 0.f);
		Doomed->MarkAsGarbage();
		TestEqual(TEXT("lost source removed, the live one still suppresses"), R.Weight(), 0.f);
		TestEqual(TEXT("one source left"), R.Director->GetNumOutputSuppressions(), 1);
		R.Director->ReleaseOutputSuppression(Alive, 0.f);
		TestEqual(TEXT("back after the live source released"), R.Weight(), 1.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAuthorityBlendFromPoseTest, "ActionCameraFramework.Authority.BlendFromPoseReturn", ACFTests::Flags)
bool FACFAuthorityBlendFromPoseTest::RunTest(const FString& Parameters)
{
	using namespace ACFAuthorityTests;
	FACFCameraPose External;
	External.Location = ExternalLoc;
	External.Rotation = FRotator(0.f, 90.f, 0.f);
	External.FieldOfView = 30.f;

	FRig R;
	TestFalse(TEXT("no request: nothing to return to"), R.Director->BlendFromPose(External, ACFTests::Linear(1.f)));
	const FACFCameraRequestHandle Gameplay = R.Director->AddRequest(R.Fixed(GameplayLoc), FACFCameraRequestParams());
	R.Weight();
	TestFalse(TEXT("cut: rejected (the caller keeps the cut)"), R.Director->BlendFromPose(External, FACFCameraBlendSettings::MakeCut()));

	// 外部カメラ中（出力バイパス）: Weight は 1 のまま、評価は進む。
	R.Driver.bOutputBypassed = true;
	{
		const FACFCameraEvaluationResult& Bypassed = R.Driver.Step(R.Director.Get(), StepTime);
		TestTrue(TEXT("bypass reported"), Bypassed.bOutputBypassed);
		TestEqual(TEXT("bypass does not fade the weight (the PCM blend is the only blend)"), Bypassed.Weight, 1.f);
		TestTrue(TEXT("view owned externally while bypassed"), R.Director->IsViewOwnedExternally());
		TestTrue(TEXT("rig evaluated while bypassed"), Bypassed.Pose.Location.Equals(GameplayLoc, 0.01));
	}
	R.Driver.bOutputBypassed = false;

	// 復帰: 外部の最終姿勢 → ゲームプレイカメラ。要求・有効な Handle は変わらない。
	TestTrue(TEXT("return blend accepted"), R.Director->BlendFromPose(External, ACFTests::Linear(1.f)));
	FVector Previous = ExternalLoc;
	for (int32 I = 1; I <= 8; ++I)
	{
		const FACFCameraEvaluationResult& Result = R.Driver.Step(R.Director.Get(), StepTime);
		const float Alpha = FMath::Min(I * StepTime, 1.f);
		const FVector Expected = FMath::Lerp(ExternalLoc, GameplayLoc, static_cast<double>(Alpha));
		TestTrue(*FString::Printf(TEXT("frame %d: on the line external -> gameplay (%s vs %s)"), I, *Result.Pose.Location.ToString(), *Expected.ToString()),
			Result.Pose.Location.Equals(Expected, 0.01));
		TestEqual(*FString::Printf(TEXT("frame %d: weight stays 1"), I), Result.Weight, 1.f);
		TestTrue(*FString::Printf(TEXT("frame %d: no jump"), I), FVector::Dist(Previous, Result.Pose.Location) <= FVector::Dist(ExternalLoc, GameplayLoc) * StepTime + 0.01);
		TestEqual(*FString::Printf(TEXT("frame %d: FOV blends"), I), Result.Pose.FieldOfView, FMath::Lerp(30.f, 70.f, Alpha), 0.01f);
		Previous = Result.Pose.Location;
	}
	TestFalse(TEXT("blend finished"), R.Director->GetLastResult().bIsBlending);
	TestEqual(TEXT("request untouched"), R.Director->GetNumRequests(), 1);
	TestTrue(TEXT("same request in control"), R.Director->GetActiveRequest() == Gameplay);

	// 復帰と同じフレームに有効な要求が替わった場合: 外部の姿勢 → 新しい要求。
	FACFCameraRequestParams High;
	High.Priority = 10;
	const FACFCameraRequestHandle Other = R.Director->AddRequest(R.Fixed(OtherLoc), High);
	R.Director->BlendFromPose(External, ACFTests::Linear(1.f));
	{
		const FACFCameraEvaluationResult& Half = R.Driver.Step(R.Director.Get(), 0.5f);
		TestTrue(TEXT("blends from the external pose to the newly active request"),
			Half.Pose.Location.Equals(FMath::Lerp(ExternalLoc, OtherLoc, 0.5), 0.01));
		TestTrue(TEXT("new request active"), Half.ActiveRequest == Other);
	}
	R.Driver.Step(R.Director.Get(), 0.5f);

	// 予約したまま全解除したら、予約も消える（次の要求が古い姿勢からブレンドしない）。
	R.Director->BlendFromPose(External, ACFTests::Linear(1.f));
	R.Director->RemoveAllRequests(true);
	R.Director->AddRequest(R.Fixed(GameplayLoc), FACFCameraRequestParams());
	TestTrue(TEXT("stale return blend dropped"), R.Driver.Step(R.Director.Get(), StepTime).Pose.Location.Equals(GameplayLoc, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACFAuthorityBypassSequenceTest, "ActionCameraFramework.Authority.ExternalViewInterruptsSequences", ACFTests::Flags)
bool FACFAuthorityBypassSequenceTest::RunTest(const FString& Parameters)
{
	using namespace ACFAuthorityTests;

	// 外部（Sequencer の Camera Cut = バイパス、または出力抑制）に画面を奪われた Finisher は、Interrupt Policy に従う。
	for (const bool bUseSuppression : { false, true })
	{
		for (const EACFCameraSequenceInterruptPolicy Policy : { EACFCameraSequenceInterruptPolicy::Continue, EACFCameraSequenceInterruptPolicy::Pause, EACFCameraSequenceInterruptPolicy::Cancel })
		{
			const FString Name = FString::Printf(TEXT("%s / %s"), bUseSuppression ? TEXT("suppressed") : TEXT("bypassed"), *UEnum::GetValueAsString(Policy));
			FRig R;
			const FACFCameraRequestHandle Gameplay = R.Director->AddRequest(R.Fixed(GameplayLoc), FACFCameraRequestParams());
			UACFCameraSequence* Sequence = NewObject<UACFCameraSequence>(GetTransientPackage(), NAME_None, RF_Transient);
			R.Keep.Emplace(Sequence);
			FACFCameraShot& Shot = Sequence->Shots.AddDefaulted_GetRef();
			Shot.Name = TEXT("A");
			Shot.Preset = R.Fixed(OtherLoc);
			Shot.Duration = 2.f;
			Shot.AdvanceMode = EACFCameraShotAdvanceMode::Duration;
			Sequence->InterruptPolicy = Policy;
			const FACFCameraSequenceHandle Handle = R.Director->PlaySequence(Sequence, FACFCameraSequenceParams());
			R.Weight();
			R.Weight();
			FACFCameraSequenceState State;
			R.Director->GetSequenceState(Handle, State);
			TestTrue(*(Name + TEXT(": in control before")), State.bInControl);
			const float Before = State.ShotTime;

			UObject* Source = R.Source();
			if (bUseSuppression)
			{
				R.Director->SuppressOutput(Source, 0.f);
			}
			else
			{
				R.Driver.bOutputBypassed = true;
			}
			for (int32 I = 0; I < 4; ++I)
			{
				R.Weight();
			}
			if (Policy == EACFCameraSequenceInterruptPolicy::Cancel)
			{
				TestFalse(*(Name + TEXT(": cancelled")), R.Director->IsSequencePlaying(Handle));
				TestEqual(*(Name + TEXT(": only the gameplay request is left")), R.Director->GetNumRequests(), 1);
				continue;
			}
			R.Director->GetSequenceState(Handle, State);
			TestTrue(*(Name + TEXT(": still playing")), State.bIsPlaying);
			TestFalse(*(Name + TEXT(": not in control while the view is owned externally")), State.bInControl);
			const float Expected = Policy == EACFCameraSequenceInterruptPolicy::Pause ? Before : Before + 4.f * StepTime;
			TestEqual(*(Name + TEXT(": shot time")), State.ShotTime, Expected, 1e-4f);

			if (bUseSuppression)
			{
				R.Director->ReleaseOutputSuppression(Source, 0.f);
			}
			R.Driver.bOutputBypassed = false;
			TestTrue(*(Name + TEXT(": the shot shows again")), R.Driver.Step(R.Director.Get(), StepTime).Pose.Location.Equals(OtherLoc, 0.01));
			R.Director->GetSequenceState(Handle, State);
			TestTrue(*(Name + TEXT(": in control again")), State.bInControl);
			TestTrue(*(Name + TEXT(": gameplay request kept underneath")), R.Director->HasRequest(Gameplay));
		}
	}

	// バイパス中も要求の自動解除（Instigator の破棄）は進む: 復帰時に古い要求が残らない。
	{
		FRig R;
		R.Director->AddRequest(R.Fixed(GameplayLoc), FACFCameraRequestParams());
		UObject* Owner = NewObject<UACFCameraPreset>(GetTransientPackage(), NAME_None, RF_Transient);
		FACFCameraRequestParams Params;
		Params.Priority = 10;
		Params.Instigator = Owner;
		R.Director->AddRequest(R.Fixed(OtherLoc), Params);
		R.Weight();
		R.Driver.bOutputBypassed = true;
		Owner->MarkAsGarbage();
		R.Weight();
		TestEqual(TEXT("instigator lost while bypassed: request released in the background"), R.Director->GetNumRequests(), 1);
		R.Driver.bOutputBypassed = false;
		TestTrue(TEXT("returns to the remaining camera"), R.Driver.Step(R.Director.Get(), StepTime).Pose.Location.Equals(GameplayLoc, 0.01));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
