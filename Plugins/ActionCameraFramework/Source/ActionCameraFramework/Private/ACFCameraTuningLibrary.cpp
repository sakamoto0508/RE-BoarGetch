#include "ACFCameraTuningLibrary.h"
#include "ACFCameraBlueprintLibrary.h"
#include "ACFCameraDirector.h"
#include "ACFCameraSubsystem.h"

namespace ACFTuningLibrary
{
	static UACFCameraDirector* FindDirector(APlayerController* PlayerController)
	{
		UACFCameraSubsystem* Subsystem = UACFCameraBlueprintLibrary::GetCameraSubsystem(PlayerController);
		return Subsystem ? Subsystem->GetDirector() : nullptr;
	}
}

EACFCameraTuningResult UACFCameraTuningLibrary::StartCameraTuning(APlayerController* PlayerController, const FACFCameraRequestHandle& Request)
{
	UACFCameraDirector* Director = ACFTuningLibrary::FindDirector(PlayerController);
	return Director ? Director->StartTuning(Request) : EACFCameraTuningResult::NoPlayer;
}

EACFCameraTuningResult UACFCameraTuningLibrary::StopCameraTuning(APlayerController* PlayerController, const FACFCameraRequestHandle& Request)
{
	UACFCameraDirector* Director = ACFTuningLibrary::FindDirector(PlayerController);
	return Director ? Director->StopTuning(Request) : EACFCameraTuningResult::NoPlayer;
}

EACFCameraTuningResult UACFCameraTuningLibrary::GetCameraTuningValue(APlayerController* PlayerController, const FACFCameraRequestHandle& Request,
	EACFCameraTuningParameter Parameter, FACFCameraTuningValue& Value, int32 NodeIndex)
{
	Value = FACFCameraTuningValue();
	UACFCameraDirector* Director = ACFTuningLibrary::FindDirector(PlayerController);
	return Director ? Director->GetTuningValue(Request, Parameter, NodeIndex, Value) : EACFCameraTuningResult::NoPlayer;
}

EACFCameraTuningResult UACFCameraTuningLibrary::SetCameraTuningValue(APlayerController* PlayerController, const FACFCameraRequestHandle& Request,
	EACFCameraTuningParameter Parameter, FVector Value, int32 NodeIndex)
{
	UACFCameraDirector* Director = ACFTuningLibrary::FindDirector(PlayerController);
	return Director ? Director->SetTuningValue(Request, Parameter, Value, NodeIndex) : EACFCameraTuningResult::NoPlayer;
}

EACFCameraTuningResult UACFCameraTuningLibrary::ResetCameraTuningValue(APlayerController* PlayerController, const FACFCameraRequestHandle& Request,
	EACFCameraTuningParameter Parameter, int32 NodeIndex)
{
	UACFCameraDirector* Director = ACFTuningLibrary::FindDirector(PlayerController);
	return Director ? Director->ResetTuningValue(Request, Parameter, NodeIndex) : EACFCameraTuningResult::NoPlayer;
}

EACFCameraTuningResult UACFCameraTuningLibrary::ResetAllCameraTuningValues(APlayerController* PlayerController, const FACFCameraRequestHandle& Request)
{
	UACFCameraDirector* Director = ACFTuningLibrary::FindDirector(PlayerController);
	return Director ? Director->ResetAllTuningValues(Request) : EACFCameraTuningResult::NoPlayer;
}

FACFCameraTuningStatus UACFCameraTuningLibrary::GetCameraTuningStatus(APlayerController* PlayerController, const FACFCameraRequestHandle& Request)
{
	UACFCameraDirector* Director = ACFTuningLibrary::FindDirector(PlayerController);
	return Director ? Director->GetTuningStatus(Request) : FACFCameraTuningStatus();
}

TArray<FACFCameraRequestInfo> UACFCameraTuningLibrary::GetCameraRequests(APlayerController* PlayerController)
{
	TArray<FACFCameraRequestInfo> Out;
	if (UACFCameraDirector* Director = ACFTuningLibrary::FindDirector(PlayerController))
	{
		Director->GetRequestInfos(Out);
	}
	return Out;
}

EACFCameraTuningResult UACFCameraTuningLibrary::GetCameraTuningNodes(APlayerController* PlayerController, const FACFCameraRequestHandle& Request, TArray<FACFCameraTuningNodeInfo>& Nodes)
{
	Nodes.Reset();
	UACFCameraDirector* Director = ACFTuningLibrary::FindDirector(PlayerController);
	return Director ? Director->GetRequestNodes(Request, Nodes) : EACFCameraTuningResult::NoPlayer;
}
