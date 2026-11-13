#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "Camera/PlayerCameraManager.h"
#include "ACFSequencerTestProbeModifier.generated.h"

/**
 * テスト専用: ACF の Output Modifier（Priority 64）の直後（65）に入り、PCM が Modifier を呼ぶたびに
 * 「どの ViewTarget の POV か（ViewTarget / PendingViewTarget）」と「ACF を通った後の POV」を記録する。
 * PCM の実際の呼び出し経路（UpdateViewTarget → ApplyCameraModifiers）を外から観察するためのもの。
 */
UCLASS(NotBlueprintable, HideDropdown, Transient)
class UACFSequencerTestProbeModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	UACFSequencerTestProbeModifier()
	{
		Priority = 65;
	}

	struct FCall
	{
		uint64 Frame = 0;
		TWeakObjectPtr<AActor> Target;
		bool bPending = false;
		bool bKnown = false;
		FVector Location = FVector::ZeroVector;
		float FOV = 0.f;
	};

	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override
	{
		// Super は呼ばない（Alpha の補間や BP イベントは不要。POV も変えない）。
		FCall& Call = Calls.AddDefaulted_GetRef();
		Call.Frame = GFrameCounter;
		Call.Location = InOutPOV.Location;
		Call.FOV = InOutPOV.FOV;
		if (CameraOwner)
		{
			if (&InOutPOV == &CameraOwner->PendingViewTarget.POV)
			{
				Call.Target = CameraOwner->PendingViewTarget.Target;
				Call.bPending = true;
				Call.bKnown = true;
			}
			else if (&InOutPOV == &CameraOwner->ViewTarget.POV)
			{
				Call.Target = CameraOwner->ViewTarget.Target;
				Call.bKnown = true;
			}
		}
		return false;
	}

	/** 最後に記録したフレームの呼び出し。 */
	void GetLastFrameCalls(TArray<FCall>& Out) const
	{
		Out.Reset();
		if (Calls.IsEmpty())
		{
			return;
		}
		const uint64 Last = Calls.Last().Frame;
		for (const FCall& Call : Calls)
		{
			if (Call.Frame == Last)
			{
				Out.Add(Call);
			}
		}
	}

	TArray<FCall> Calls;
};
