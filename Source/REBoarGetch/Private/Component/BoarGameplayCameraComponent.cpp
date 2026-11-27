#include "Component/BoarGameplayCameraComponent.h"
#include "BoarGameplayCameraInternal.h"

#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraDirector.h"
#include "ACFCameraNode.h"
#include "ACFCameraPreset.h"
#include "ACFCameraSubsystem.h"
#include "ACFCameraTuning.h"
#include "ACFCameraTuningLibrary.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY(LogBoarCamera);

// Dump・Traceなどの診断機能の本体は BoarGameplayCameraDebug.cpp にあります。ここにはコンソール変数・コマンドの登録だけを置きます。
namespace BoarGameplayCamera
{
	static void RefreshAll()
	{
		for (TObjectIterator<UBoarGameplayCameraComponent> It; It; ++It)
		{
			const UWorld* World = It->GetWorld();
			if (World && World->IsGameWorld() && It->HasBegunPlay() && !It->IsBeingDestroyed())
			{
				It->RefreshCameraRequest();
			}
		}
	}

	static FAutoConsoleVariableRef CVarACF(
		TEXT("Boar.Camera.ACF"),
		ACFOverride,
		TEXT("通常TPSカメラの切替: -1 = Componentの既定値、0 = 従来のSpringArmカメラ、1 = Action Camera Framework"),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { RefreshAll(); }),
		ECVF_Default);

	template <typename FuncType>
	static void ForEachInWorld(UWorld* World, FuncType&& Func)
	{
		for (TObjectIterator<UBoarGameplayCameraComponent> It; It; ++It)
		{
			if (It->GetWorld() == World && It->HasBegunPlay())
			{
				Func(**It);
			}
		}
	}

	static FAutoConsoleCommandWithWorld CmdDump(
		TEXT("Boar.Camera.Dump"),
		TEXT("通常カメラの状態（ACFのRequest、出力Weight、ACF／SpringArm／描画カメラの姿勢、Pivot／回転の誤差）をLogへ出力します。"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			ForEachInWorld(World, [](UBoarGameplayCameraComponent& C) { C.DumpCameraState(); });
		}));

#if !UE_BUILD_SHIPPING
	static FAutoConsoleVariableRef CVarTestPivot(
		TEXT("Boar.Camera.Test.PivotDamping"),
		TestPivotDamping,
		TEXT("Damping検証: Orbital FollowのPivot Damping（秒）をACFのRuntime Tuningで変更します。負の値 = アセットの値。"),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { RefreshAll(); }),
		ECVF_Cheat);

	static FAutoConsoleVariableRef CVarTestRotation(
		TEXT("Boar.Camera.Test.RotationDamping"),
		TestRotationDamping,
		TEXT("Damping検証: Orbital FollowのRotation Damping（秒、Yaw・Pitch共通）をPresetの一時複製で変更します。負の値 = アセットの値。"),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { RefreshAll(); }),
		ECVF_Cheat);

	static FAutoConsoleVariableRef CVarTestReturn(
		TEXT("Boar.Camera.Test.ReturnDamping"),
		TestReturnDamping,
		TEXT("Damping検証: CollisionのReturn Damping Time（秒）をPresetの一時複製で変更します。Pull Inはアセットのまま。負の値 = アセットの値。"),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { RefreshAll(); }),
		ECVF_Cheat);

	static FAutoConsoleCommand CmdTestReset(
		TEXT("Boar.Camera.Test.Reset"),
		TEXT("Boar.Camera.Test.* のDamping検証値をすべてアセットの値（-1）へ戻します。"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const TCHAR* TestVariableNames[] = {
				TEXT("Boar.Camera.Test.PivotDamping"),
				TEXT("Boar.Camera.Test.RotationDamping"),
				TEXT("Boar.Camera.Test.ReturnDamping"),
			};

			for (const TCHAR* Name : TestVariableNames)
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
				{
					Var->Set(UseAssetValue, ECVF_SetByConsole);
				}
			}

			RefreshAll();
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdTraceStart(
		TEXT("Boar.Camera.Trace.Start"),
		TEXT("通常カメラの毎フレーム記録（Pivot／回転の誤差）を Saved/BoarCameraTrace へ開始します。引数でラベルを指定できます。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const FString Label = Args.Num() > 0 ? Args[0] : TEXT("Trace");
			ForEachInWorld(World, [&Label](UBoarGameplayCameraComponent& C) { C.StartTrace(Label); });
		}));

	static FAutoConsoleCommandWithWorld CmdTraceStop(
		TEXT("Boar.Camera.Trace.Stop"),
		TEXT("カメラの記録を停止し、CSVを書き出して要約をLogへ出力します。"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			ForEachInWorld(World, [](UBoarGameplayCameraComponent& C) { C.StopTrace(); });
		}));
#endif
}

UBoarGameplayCameraComponent::UBoarGameplayCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

APlayerController* UBoarGameplayCameraComponent::GetOwningController() const
{
	return Cast<APlayerController>(GetOwner());
}

UACFCameraSubsystem* UBoarGameplayCameraComponent::GetCameraSubsystem() const
{
	const APlayerController* PC = GetOwningController();
	return PC && PC->IsLocalController() ? UACFCameraBlueprintLibrary::GetCameraSubsystem(PC) : nullptr;
}

bool UBoarGameplayCameraComponent::IsACFCameraEnabled() const
{
	return BoarGameplayCamera::ACFOverride < 0 ? bUseACFCameraByDefault : BoarGameplayCamera::ACFOverride != 0;
}

bool UBoarGameplayCameraComponent::HasActiveACFRequest() const
{
	const UACFCameraSubsystem* Cameras = GetCameraSubsystem();
	return Cameras && Cameras->IsCameraRequestAlive(RequestHandle);
}

void UBoarGameplayCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	bBegunPlay = true;

	if (APlayerController* PC = GetOwningController())
	{
		// Respawn・再Possessで追従対象を差し替える。BeginPlay前にPossess済みのPawnは下のRefreshで拾う。
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::HandlePossessedPawnChanged);
	}

	RefreshCameraRequest();
}

void UBoarGameplayCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopTrace();

	if (APlayerController* PC = GetOwningController())
	{
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandlePossessedPawnChanged);
	}

	// OpenLevel / Retry / PIE終了。ACFはLocalPlayerと共に残るため、次のLevelへブレンドを持ち越さないようカットで解除する。
	// Runtime Tuningの一時複製はRequestの終了とWorldの変化でACF側が自動で破棄する。
	ReleaseRequest(true);

	bBegunPlay = false;
	Super::EndPlay(EndPlayReason);
}

void UBoarGameplayCameraComponent::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	RefreshCameraRequest();
}

UACFCameraPreset* UBoarGameplayCameraComponent::GetEffectivePreset()
{
#if !UE_BUILD_SHIPPING
	using BoarGameplayCamera::UseAssetValue;

	const float Rot = BoarGameplayCamera::TestRotationDamping;
	const float Ret = BoarGameplayCamera::TestReturnDamping;
	const bool bTestRot = FMath::IsFinite(Rot) && Rot >= 0.f;
	const bool bTestRet = FMath::IsFinite(Ret) && Ret >= 0.f;

	if (NormalCameraPreset && (bTestRot || bTestRet))
	{
		const float WantRot = bTestRot ? Rot : UseAssetValue;
		const float WantRet = bTestRet ? Ret : UseAssetValue;

		if (!TestPreset || TestPresetRotationDamping != WantRot || TestPresetReturnDamping != WantRet)
		{
			// 共有Presetは変更せず、Transientの複製（Instanced Nodeも複製される）だけを書き換える。保存されない。
			UACFCameraPreset* Copy = DuplicateObject<UACFCameraPreset>(NormalCameraPreset, this,
				MakeUniqueObjectName(this, UACFCameraPreset::StaticClass(), TEXT("BoarCameraDampingTest")));
			Copy->SetFlags(RF_Transient);

			for (UACFCameraNode* Node : Copy->Nodes)
			{
				if (UACFCameraNode_OrbitalFollow* Orbit = Cast<UACFCameraNode_OrbitalFollow>(Node); Orbit && bTestRot)
				{
					Orbit->RotationDamping = FVector2D(Rot, Rot);
				}

				if (UACFCameraNode_Collision* Collision = Cast<UACFCameraNode_Collision>(Node); Collision && bTestRet)
				{
					Collision->ReturnDampingTime = Ret;
				}
			}

			TestPreset = Copy;
			TestPresetRotationDamping = WantRot;
			TestPresetReturnDamping = WantRet;

			UE_LOG(LogBoarCamera, Display,
				TEXT("[CameraTest] 検証用の一時Preset %s（%s の複製）を作成: RotationDamping=%s ReturnDamping=%s"),
				*GetNameSafe(Copy), *GetNameSafe(NormalCameraPreset),
				bTestRot ? *FString::SanitizeFloat(Rot) : TEXT("アセット値"),
				bTestRet ? *FString::SanitizeFloat(Ret) : TEXT("アセット値"));
		}

		return TestPreset;
	}

	TestPreset = nullptr;
	TestPresetRotationDamping = TestPresetReturnDamping = UseAssetValue;
#endif
	return NormalCameraPreset;
}

void UBoarGameplayCameraComponent::IssueRequest(UACFCameraSubsystem& Cameras, APawn& Pawn, UACFCameraPreset* Preset)
{
	FACFCameraRequestParams Params;
	Params.Priority = RequestPriority;
	Params.Instigator = GetOwningController(); // EndPlayを経ずにControllerが消えても自動解除される。
	Params.FollowTarget.Actor = &Pawn;

	RequestHandle = Cameras.RequestCamera(Preset, Params);
	FollowedPawn = RequestHandle.IsValid() ? &Pawn : nullptr;
	RequestedPreset = RequestHandle.IsValid() ? Preset : nullptr;

	UE_LOG(LogBoarCamera, Log, TEXT("[Camera] ACFの通常カメラをRequest: %s Preset=%s Pawn=%s"),
		*RequestHandle.ToString(), *GetNameSafe(Preset), *GetNameSafe(&Pawn));
}

void UBoarGameplayCameraComponent::RefreshCameraRequest()
{
	if (!bBegunPlay)
		return;

	APlayerController* PC = GetOwningController();
	UACFCameraSubsystem* Cameras = GetCameraSubsystem();
	if (!PC || !Cameras)
		return;

	if (RequestHandle.IsValid() && !Cameras->IsCameraRequestAlive(RequestHandle))
	{
		// 外部（Release All Cameras等）で解除済み。Handleだけ捨てて、必要なら下で出し直す。
		RequestHandle.Invalidate();
		FollowedPawn.Reset();
		RequestedPreset = nullptr;
	}

	const bool bWantACF = IsACFCameraEnabled();
	if (bWantACF && !NormalCameraPreset && !bReportedMissingPreset)
	{
		bReportedMissingPreset = true;
		UE_LOG(LogBoarCamera, Warning, TEXT("[Camera] %s に NormalCameraPreset が未設定です。SpringArmカメラで代用します。"),
			*GetPathNameSafe(this));
	}

	APawn* Pawn = PC->GetPawn();

	if (!bWantACF || !NormalCameraPreset)
	{
		// 無効化: PresetのDefault Blend OutでSpringArm（UEカメラ）へ戻す。
		ReleaseRequest(false);
		return;
	}

	UACFCameraPreset* Preset = GetEffectivePreset();

	if (RequestHandle.IsValid())
	{
		if (Pawn && RequestedPreset != Preset)
		{
			// Damping検証値の変更: 新しいRequestを先に出してから古いRequestを解除する（古い方はブレンド元としてだけ残る）。
			FACFCameraRequestHandle Old = RequestHandle;
			IssueRequest(*Cameras, *Pawn, Preset);
			Cameras->ReleaseCamera(Old);
		}
		else if (Pawn && FollowedPawn.Get() != Pawn)
		{
			// 二重Requestはせず、追従対象だけ差し替える（ACFはNodeの補間をリセットしてカットする）。
			FACFCameraTarget Follow;
			Follow.Actor = Pawn;
			Cameras->SetCameraTargets(RequestHandle, Follow, FACFCameraTarget());
			FollowedPawn = Pawn;
		}
	}
	else if (Pawn)
	{
		IssueRequest(*Cameras, *Pawn, Preset);
	}

	// Pawnが無ければ、来たときにOnPossessedPawnChangedで出し直す。
	ApplyPivotTuning();
}

void UBoarGameplayCameraComponent::ApplyPivotTuning()
{
#if !UE_BUILD_SHIPPING
	APlayerController* PC = GetOwningController();
	if (!PC || !RequestHandle.IsValid())
		return;

	const float Pivot = BoarGameplayCamera::TestPivotDamping;
	if (FMath::IsFinite(Pivot) && Pivot >= 0.f)
	{
		// ACF既存のRuntime Tuning: このRequestだけPresetの一時複製へ切り替え、Dampingの履歴は保ったまま値を変える。アセットは変更しない。
		const EACFCameraTuningResult Start = UACFCameraTuningLibrary::StartCameraTuning(PC, RequestHandle);
		const EACFCameraTuningResult Set = Start == EACFCameraTuningResult::Ok
			? UACFCameraTuningLibrary::SetCameraTuningValue(PC, RequestHandle, EACFCameraTuningParameter::FollowDamping, FVector(Pivot))
			: Start;

		UE_LOG(LogBoarCamera, Display, TEXT("[CameraTest] Runtime TuningでPivotDamping=%.3fを適用: %s"),
			Pivot, *UEnum::GetValueAsString(Set));
	}
	else if (UACFCameraTuningLibrary::GetCameraTuningStatus(PC, RequestHandle).bTuning)
	{
		// 調整を終了し、値を捨ててPresetの値へ戻す。
		UACFCameraTuningLibrary::StopCameraTuning(PC, RequestHandle);
		UE_LOG(LogBoarCamera, Display, TEXT("[CameraTest] PivotDampingをPresetの値へ戻しました（Runtime Tuning終了）。"));
	}
#endif
}

void UBoarGameplayCameraComponent::ReleaseRequest(bool bCut)
{
	if (!RequestHandle.IsValid())
		return;

	if (UACFCameraSubsystem* Cameras = GetCameraSubsystem())
	{
		const FString Name = RequestHandle.ToString();
		const bool bReleased = bCut
			? Cameras->ReleaseCameraWithBlend(RequestHandle, FACFCameraBlendSettings::MakeCut())
			: Cameras->ReleaseCamera(RequestHandle);

		UE_LOG(LogBoarCamera, Log, TEXT("[Camera] ACFの通常カメラを解除: %s Cut=%d Released=%d"),
			*Name, bCut ? 1 : 0, bReleased ? 1 : 0);
	}

	RequestHandle.Invalidate();
	FollowedPawn.Reset();
	RequestedPreset = nullptr;
}

