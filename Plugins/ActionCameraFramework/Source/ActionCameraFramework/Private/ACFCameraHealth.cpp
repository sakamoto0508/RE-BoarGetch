#include "ACFCameraHealth.h"

namespace ACFHealthImpl
{
	static FACFCameraHealth* GActive = nullptr;

	/** Logged at the 1st, 10th, 100th, ... occurrence of an issue: visible without flooding the log every frame. */
	static bool ShouldLog(int32 Count)
	{
		while (Count >= 10 && Count % 10 == 0)
		{
			Count /= 10;
		}
		return Count == 1;
	}
}

const TCHAR* LexToString(EACFCameraHealthIssue Issue)
{
	switch (Issue)
	{
	case EACFCameraHealthIssue::InvalidTarget: return TEXT("invalid target transform");
	case EACFCameraHealthIssue::InvalidDeltaTime: return TEXT("invalid delta time");
	case EACFCameraHealthIssue::InvalidUpstream: return TEXT("invalid UE camera (upstream) pose");
	case EACFCameraHealthIssue::InvalidControlRotation: return TEXT("invalid control rotation");
	case EACFCameraHealthIssue::RejectedInput: return TEXT("rejected runtime input");
	case EACFCameraHealthIssue::InvalidNodeOutput: return TEXT("invalid node output");
	case EACFCameraHealthIssue::InvalidEffectOutput: return TEXT("invalid effect output");
	case EACFCameraHealthIssue::InvalidBlend: return TEXT("invalid blend");
	case EACFCameraHealthIssue::InvalidFinalPose: return TEXT("invalid final pose");
	case EACFCameraHealthIssue::InvalidOutputUpstream: return TEXT("invalid PCM pose under ACF");
	default: return TEXT("?");
	}
}

void FACFCameraHealth::Report(EACFCameraHealthIssue Issue, FName Node, const TCHAR* Parameter, const TCHAR* Fallback)
{
	const int32 Count = ++Counts[static_cast<int32>(Issue)];
	++Total;
	LastIssueFrame = GFrameCounter;
	LastIssue = FString::Printf(TEXT("%s: node %s, request %s, parameter %s, fallback %s (frame %llu)"), LexToString(Issue),
		Node.IsNone() ? TEXT("-") : *Node.ToString(), CurrentRequest.IsValid() ? *CurrentRequest.ToString() : TEXT("-"), Parameter, Fallback, GFrameCounter);
	if (ACFHealthImpl::ShouldLog(Count))
	{
		UE_LOG(LogActionCamera, Warning, TEXT("ACF camera health [%s]: %s. Occurrence %d of this issue (logged at 1, 10, 100, ...; counts: Get ACF Camera Health Diagnostics / ACF.Debug.Health)."),
			*OwnerName, *LastIssue, Count);
	}
}

void FACFCameraHealth::ReportActive(EACFCameraHealthIssue Issue, FName Node, const TCHAR* Parameter, const TCHAR* Fallback)
{
	if (ACFHealthImpl::GActive && IsInGameThread())
	{
		ACFHealthImpl::GActive->Report(Issue, Node, Parameter, Fallback);
	}
}

void FACFCameraHealth::Reset()
{
	FMemory::Memzero(Counts);
	Total = 0;
	LastIssue.Reset();
	LastIssueFrame = 0;
}

FACFCameraHealthDiagnostics FACFCameraHealth::GetDiagnostics() const
{
	FACFCameraHealthDiagnostics Out;
	Out.TotalIssues = Total;
	Out.InvalidTargets = GetCount(EACFCameraHealthIssue::InvalidTarget);
	Out.InvalidDeltaTimes = GetCount(EACFCameraHealthIssue::InvalidDeltaTime);
	Out.InvalidUpstreamPoses = GetCount(EACFCameraHealthIssue::InvalidUpstream) + GetCount(EACFCameraHealthIssue::InvalidControlRotation)
		+ GetCount(EACFCameraHealthIssue::InvalidOutputUpstream);
	Out.RejectedInputs = GetCount(EACFCameraHealthIssue::RejectedInput) + GetCount(EACFCameraHealthIssue::InvalidBlend);
	Out.InvalidNodeOutputs = GetCount(EACFCameraHealthIssue::InvalidNodeOutput);
	Out.InvalidEffectOutputs = GetCount(EACFCameraHealthIssue::InvalidEffectOutput);
	Out.InvalidFinalPoses = GetCount(EACFCameraHealthIssue::InvalidFinalPose);
	Out.LastIssue = LastIssue;
	Out.LastIssueFrame = static_cast<int64>(LastIssueFrame);
	return Out;
}

FACFCameraHealth::FActiveScope::FActiveScope(FACFCameraHealth* Health)
	: Previous(ACFHealthImpl::GActive)
{
	check(IsInGameThread());
	ACFHealthImpl::GActive = Health;
}

FACFCameraHealth::FActiveScope::~FActiveScope()
{
	ACFHealthImpl::GActive = Previous;
}
