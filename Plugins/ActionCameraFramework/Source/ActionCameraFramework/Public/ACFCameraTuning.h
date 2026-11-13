#pragma once

#include "CoreMinimal.h"
#include "ACFCameraTypes.h"
#include "ACFCameraNode.h"
#include "ACFCameraTuning.generated.h"

class UACFCameraNode;
class UACFCameraPreset;

/**
 * Runtime camera tuning (Phase 7A-1, README 24 章): edit a few parameters of the camera a request is using while the game
 * runs, see the result at once, reset or stop. The shared preset asset is never written: tuning gives the request its own
 * transient copy of the preset (nodes included), and the request's rig keeps its runtime state when it switches to the
 * copy and back. Development builds only: in Shipping (or with ACF_WITH_RUNTIME_TUNING=0) every call returns NotAvailable.
 * Saving the tuned values back to the asset and an editor UI are later steps (7A-2).
 */
#ifndef ACF_WITH_RUNTIME_TUNING
#define ACF_WITH_RUNTIME_TUNING (!UE_BUILD_SHIPPING)
#endif

/** Tunable parameters. Each one lives on specific node classes (see README 24 章 for the table). */
UENUM(BlueprintType)
enum class EACFCameraTuningParameter : uint8
{
	/** Per-axis damping times (seconds, X / Y / Z, >= 0): Follow Damping, Orbital Follow / Lock-On Pivot Damping. */
	FollowDamping,
	/** Field of view (degrees, X only, above 0 and below 180): Fixed Pose / Shot Anchor Field Of View (their Override must be on). */
	FieldOfView,
	/** Camera offset (cm, X / Y / Z): Follow Offset, Orbital Follow / Lock-On Shoulder Offset, Shot Anchor Camera Offset. */
	CameraOffset,
	/** Collision probe radius (cm, X only, >= 0): Collision Probe Radius. */
	CollisionRadius,
};

/** Why a tuning call did (not) do anything. */
UENUM(BlueprintType)
enum class EACFCameraTuningResult : uint8
{
	Ok,
	/** Shipping build (or ACF_WITH_RUNTIME_TUNING = 0). */
	NotAvailable,
	/** No PlayerController / ACF subsystem. */
	NoPlayer,
	/** The handle is not a live request of this player (an invalid handle means "the active request": there is none). */
	NoRequest,
	/** The request is not being tuned (Set / Reset / Stop need Start Tuning first). */
	NotTuning,
	/** The request's preset has no node with this parameter (or Node Index is not such a node). */
	NoNode,
	/** NaN / Inf or outside the parameter's range; nothing was changed. */
	InvalidValue,
	InvalidParameter,
};

/** A parameter value. Scalar parameters (Field Of View, Collision Radius) use X; Y / Z are 0. */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraTuningValue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	FVector Value = FVector::ZeroVector;

	/** The same parameter in the shared preset asset (what Reset restores). */
	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	FVector OriginalValue = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	bool bScalar = false;

	/** Node that holds the parameter: index in the preset's Nodes, class and property. */
	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	int32 NodeIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	FName NodeClass;

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	FName Property;
};

USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraTuningStatus
{
	GENERATED_BODY()

	/** True while the request runs on its tuning copy. */
	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	bool bTuning = false;

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	FACFCameraRequestHandle Handle;

	/** The shared preset asset (never modified). */
	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	FName OriginalPreset;

	/** Tunable values that differ from the asset. */
	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	int32 NumChangedValues = 0;

	/** Number of requests of this player being tuned. */
	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	int32 NumTunedRequests = 0;
};

/** A live camera request of a player, for tools (Phase 7A-2: the tuning panel lists them). */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraRequestInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	FACFCameraRequestHandle Handle;

	/** The preset asset of the request (the shared asset also while it runs on a tuning copy). */
	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	FName Preset;

	/** Path of that asset (Phase 7A-3: the editor applies tuned values to it after PIE). Not a reference: it keeps nothing alive. */
	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	FSoftObjectPath PresetPath;

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	FName DebugName;

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	int32 Priority = 0;

	/** The request in control (on screen unless output is suppressed / bypassed). */
	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	bool bTuning = false;
};

/**
 * One node of a request's preset, for tools. NodeIndex is the index in the preset's Nodes and is the node identity used by
 * every tuning call (the tuning copy keeps the same layout, so the index is the same on the asset and on the copy).
 */
USTRUCT(BlueprintType)
struct ACTIONCAMERAFRAMEWORK_API FACFCameraTuningNodeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	int32 NodeIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	FName NodeClass;

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	EACFCameraNodeStage Stage = EACFCameraNodeStage::Body;

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	bool bEnabled = true;

	/** Parameters this node holds right now (e.g. Fixed Pose has Field Of View only while its Override is on). */
	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	TArray<EACFCameraTuningParameter> Parameters;
};

/** One tuned request (owned by the director of its local player). */
USTRUCT()
struct ACTIONCAMERAFRAMEWORK_API FACFCameraTuningSession
{
	GENERATED_BODY()

	FACFCameraRequestHandle Handle;

	/** The shared asset the request was using. Kept alive so Stop / Reset can return to it. */
	UPROPERTY()
	TObjectPtr<const UACFCameraPreset> Original = nullptr;

	/** Transient deep copy (its nodes are its own subobjects), outer = the director. */
	UPROPERTY()
	TObjectPtr<UACFCameraPreset> Copy = nullptr;

	/** World the tuning started in: a World change ends the session (nothing survives map travel / PIE end). */
	TWeakObjectPtr<const UWorld> World;
};

/** The parameter table (C++ helpers, also used by the tests). */
namespace ACFCameraTuning
{
	ACTIONCAMERAFRAMEWORK_API const TCHAR* LexToString(EACFCameraTuningResult Result);

	/** True if Node holds Parameter. */
	ACTIONCAMERAFRAMEWORK_API bool HasParameter(const UACFCameraNode& Node, EACFCameraTuningParameter Parameter);

	/** Index of the node that holds Parameter: NodeIndex if it does, else (NodeIndex = INDEX_NONE) the first enabled such node. */
	ACTIONCAMERAFRAMEWORK_API int32 FindNode(const UACFCameraPreset& Preset, EACFCameraTuningParameter Parameter, int32 NodeIndex);

	/** Reads / writes the parameter of Node (false if the node does not hold it). Write validates the value first. */
	ACTIONCAMERAFRAMEWORK_API bool Read(const UACFCameraNode& Node, EACFCameraTuningParameter Parameter, FACFCameraTuningValue& Out);
	ACTIONCAMERAFRAMEWORK_API EACFCameraTuningResult Write(UACFCameraNode& Node, EACFCameraTuningParameter Parameter, const FVector& Value);

	/** NaN / Inf and the parameter's range (damping and radius >= 0, FOV in (0, 180)). */
	ACTIONCAMERAFRAMEWORK_API bool IsValidValue(EACFCameraTuningParameter Parameter, const FVector& Value);

	/** Number of tunable values of Copy that differ from Original (same node layout). */
	ACTIONCAMERAFRAMEWORK_API int32 CountChanged(const UACFCameraPreset& Original, const UACFCameraPreset& Copy);

	/** Valid range of a parameter (per component). Max is +infinity when unbounded; FOV is open at both ends (0, 180). */
	ACTIONCAMERAFRAMEWORK_API void GetValueRange(EACFCameraTuningParameter Parameter, double& OutMin, double& OutMax);
	ACTIONCAMERAFRAMEWORK_API bool IsScalar(EACFCameraTuningParameter Parameter);
	ACTIONCAMERAFRAMEWORK_API FString GetParameterName(EACFCameraTuningParameter Parameter);

	/** Nodes of Preset with their tunable parameters (in Nodes order; empty slots are skipped). */
	ACTIONCAMERAFRAMEWORK_API void GetNodes(const UACFCameraPreset& Preset, TArray<FACFCameraTuningNodeInfo>& Out);

	/** Why Preset has no tunable Parameter (empty if it has one): which node / toggle is missing. */
	ACTIONCAMERAFRAMEWORK_API FString GetUnavailableReason(const UACFCameraPreset& Preset, EACFCameraTuningParameter Parameter);
}
