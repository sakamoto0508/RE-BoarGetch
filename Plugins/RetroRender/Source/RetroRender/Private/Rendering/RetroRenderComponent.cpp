#include "Rendering/RetroRenderComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogRetroRender, Log, All);

namespace RetroRenderState
{
	// The component that drives each world. Weak pointers: stale entries are treated as free.
	TMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<URetroRenderComponent>>& Owners()
	{
		static TMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<URetroRenderComponent>> Map;
		return Map;
	}

	// CVars are process-wide. Every requester keeps its own value; the newest live request wins and the value from
	// before the first request is restored only when the last requester releases it.
	struct FCVarEntry
	{
		FString Original;
		TArray<TPair<TWeakObjectPtr<const UObject>, FString>> Requests;
	};

	TMap<FString, FCVarEntry>& CVars()
	{
		static TMap<FString, FCVarEntry> Map;
		return Map;
	}

	void ApplyTop(const FString& Name, FCVarEntry& Entry)
	{
		Entry.Requests.RemoveAll([](const TPair<TWeakObjectPtr<const UObject>, FString>& R) { return !R.Key.IsValid(); });
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name))
		{
			Var->Set(Entry.Requests.IsEmpty() ? *Entry.Original : *Entry.Requests.Last().Value, ECVF_SetByCode);
		}
	}

	void Push(const UObject* Owner, const TCHAR* Name, const FString& Value)
	{
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		if (!Var) return;
		FCVarEntry* Entry = CVars().Find(Name);
		if (!Entry)
		{
			Entry = &CVars().Add(Name);
			Entry->Original = Var->GetString();
		}
		Entry->Requests.RemoveAll([Owner](const TPair<TWeakObjectPtr<const UObject>, FString>& R) { return R.Key.Get() == Owner; });
		Entry->Requests.Emplace(Owner, Value);
		ApplyTop(Name, *Entry);
	}

	void Release(const UObject* Owner, const TCHAR* Name)
	{
		FCVarEntry* Entry = CVars().Find(Name);
		if (!Entry) return;
		const int32 Removed = Entry->Requests.RemoveAll([Owner](const TPair<TWeakObjectPtr<const UObject>, FString>& R) { return R.Key.Get() == Owner; });
		if (!Removed) return;
		ApplyTop(Name, *Entry);
		if (Entry->Requests.IsEmpty()) CVars().Remove(Name);
	}

	const TCHAR* const Managed[] = {TEXT("r.AntiAliasingMethod"), TEXT("r.ScreenPercentage"), TEXT("r.Upscale.Quality")};
}

URetroRenderComponent::URetroRenderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bTickInEditor = false;
	Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Art/Rendering/Retro/M_RetroPost.M_RetroPost")));
	PresetData = TSoftObjectPtr<URetroRenderPresetData>(FSoftObjectPath(TEXT("/Game/Art/Rendering/Retro/DA_RetroPresets.DA_RetroPresets")));
	ParameterCollection = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT("/Game/Art/Rendering/Retro/MPC_Retro.MPC_Retro")));
	// Optional (InportAssets, not in git): skipped silently when the LowPoly pack is not installed.
	SharedCollections.Add(TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT("/Game/InportAssets/LPRiverForest/Materials/Common/MPC_UnlitStrength.MPC_UnlitStrength"))));
}

bool URetroRenderComponent::IsGameWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld();
}

void URetroRenderComponent::OnRegister()
{
	Super::OnRegister();
	UWorld* World = GetWorld();
	if (!World || HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || IsRunningCommandlet()) return;
	if (World->GetNetMode() == NM_DedicatedServer) return;
	const bool bEditorPreview = World->WorldType == EWorldType::Editor;
	if (!IsGameWorld() && !(bEditorPreview && bPreviewInEditor)) return;
	Setup();
}

void URetroRenderComponent::OnUnregister()
{
	Teardown();
	Super::OnUnregister();
}

void URetroRenderComponent::BeginPlay()
{
	Super::BeginPlay();
	// Pipeline CVars need the game viewport, which exists by BeginPlay.
	Apply();
}

bool URetroRenderComponent::IsWorldOwner() const
{
	const TWeakObjectPtr<URetroRenderComponent>* Owner = RetroRenderState::Owners().Find(GetWorld());
	return Owner && Owner->Get() == this;
}

void URetroRenderComponent::Setup()
{
	if (MID) return;
	UWorld* World = GetWorld();
	TWeakObjectPtr<URetroRenderComponent>& Owner = RetroRenderState::Owners().FindOrAdd(World);
	if (Owner.IsValid() && Owner.Get() != this && Owner->IsRegistered())
	{
		UE_LOG(LogRetroRender, Warning, TEXT("%s: %s already drives this world; this component stays inactive (one per world)."),
			*GetPathName(), *Owner->GetPathName());
		return;
	}
	Owner = this;
	const URetroRenderPresetData* Data = PresetData.LoadSynchronous();
	Defaults = (Data && Data->Presets.Num() == 5) ? Data->Presets : GetDefault<URetroRenderPresetData>()->Presets;
	if (Working.Num() != 5) Working = Defaults;
	Collection = ParameterCollection.LoadSynchronous();
	Collections.Reset();
	if (Collection) Collections.Add(Collection);
	for (const TSoftObjectPtr<UMaterialParameterCollection>& Shared : SharedCollections)
	{
		if (Shared.IsNull() || !FPackageName::DoesPackageExist(Shared.ToSoftObjectPath().GetLongPackageName())) continue;
		if (UMaterialParameterCollection* Loaded = Shared.LoadSynchronous()) Collections.AddUnique(Loaded);
	}
	UMaterialInterface* Base = Material.LoadSynchronous();
	if (!Base || Base->GetMaterial()->MaterialDomain != MD_PostProcess)
	{
		UE_LOG(LogRetroRender, Error, TEXT("Post process material missing/invalid. Run Plugins/RetroRender/Scripts/SetupRetroAssets.py."));
		RetroRenderState::Owners().Remove(World);
		return;
	}
	if (!Collection) UE_LOG(LogRetroRender, Warning, TEXT("MPC_Retro missing: geometry features (vertex snap, affine, texel snap) disabled."));
	CurrentPreset = InitialPreset;
	// One MID and one isolated blendable for the whole registration lifetime.
	MID = UMaterialInstanceDynamic::Create(Base, this);
	PostProcess = NewObject<UPostProcessComponent>(GetOwner(), NAME_None, RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
	PostProcess->bUnbound = true;
	PostProcess->Priority = 1000;
	PostProcess->BlendWeight = 0;
	PostProcess->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, MID));
	PostProcess->RegisterComponentWithWorld(GetWorld());
	Apply();
}

void URetroRenderComponent::Teardown()
{
	RestorePipeline();
	// Back to the MPC defaults (0): patched materials render exactly as before.
	WriteCollections(nullptr);
	if (PostProcess) PostProcess->DestroyComponent();
	PostProcess = nullptr;
	MID = nullptr;
	Collections.Reset();
	if (IsWorldOwner()) RetroRenderState::Owners().Remove(GetWorld());
}

void URetroRenderComponent::WriteCollections(const FRetroRenderParameters* Values)
{
	UWorld* World = GetWorld();
	if (!World) return;
	for (UMaterialParameterCollection* C : Collections)
	{
		UMaterialParameterCollectionInstance* Instance = C ? World->GetParameterCollectionInstance(C) : nullptr;
		if (!Instance) continue;
		for (const FRetroParameterDescriptor& D : GetRetroParameterDescriptors())
		{
			if (D.Target != ERetroParameterTarget::Collection) continue;
			const FCollectionScalarParameter* Param = C->GetScalarParameterByName(D.Name);
			if (Param) Instance->SetScalarParameterValue(D.Name, Values ? Values->*(D.Member) : Param->DefaultValue);
		}
	}
}

FRetroRenderParameters URetroRenderComponent::GetParameters() const
{
	return Working.IsValidIndex(int32(CurrentPreset)) ? Working[int32(CurrentPreset)] : FRetroRenderParameters();
}

FRetroRenderParameters URetroRenderComponent::GetEffectiveParameters() const
{
	FRetroRenderParameters P = GetParameters();
	if (bCRTOverlay && CurrentPreset != ERetroRenderPreset::CRT && Working.IsValidIndex(int32(ERetroRenderPreset::CRT)))
	{
		ApplyRetroCRTLayer(P, Working[int32(ERetroRenderPreset::CRT)]);
	}
	return P;
}

void URetroRenderComponent::SetPreset(ERetroRenderPreset NewPreset)
{
	if (uint8(NewPreset) > 4) return;
	CurrentPreset = NewPreset;
	InitialPreset = NewPreset;
	Apply();
}

void URetroRenderComponent::SetCRTOverlay(bool bEnable)
{
	bCRTOverlay = bEnable;
	Apply();
}

void URetroRenderComponent::SetParameter(FName Name, float Value)
{
	if (!Working.IsValidIndex(int32(CurrentPreset)) || !FMath::IsFinite(Value)) return;
	for (const FRetroParameterDescriptor& D : GetRetroParameterDescriptors())
	{
		if (Name == D.Name)
		{
			Working[int32(CurrentPreset)].*(D.Member) = FMath::Clamp(Value, D.Min, D.Max);
			Apply();
			return;
		}
	}
}

void URetroRenderComponent::SetPresetParameters(const FRetroRenderParameters& Values)
{
	if (!Working.IsValidIndex(int32(CurrentPreset))) return;
	FRetroRenderParameters Clamped = Values;
	for (const FRetroParameterDescriptor& D : GetRetroParameterDescriptors())
	{
		float& V = Clamped.*(D.Member);
		V = FMath::IsFinite(V) ? FMath::Clamp(V, D.Min, D.Max) : D.Min;
	}
	Working[int32(CurrentPreset)] = Clamped;
	Apply();
}

void URetroRenderComponent::ResetCurrentPreset()
{
	if (Working.IsValidIndex(int32(CurrentPreset)) && Defaults.IsValidIndex(int32(CurrentPreset)))
	{
		Working[int32(CurrentPreset)] = Defaults[int32(CurrentPreset)];
	}
	Apply();
}

void URetroRenderComponent::Apply()
{
	if (!MID || !PostProcess) return;
	const bool bActive = IsRetroActive();
	const FRetroRenderParameters P = bActive ? GetEffectiveParameters() : FRetroRenderParameters();
	for (const FRetroParameterDescriptor& D : GetRetroParameterDescriptors())
	{
		if (D.Target == ERetroParameterTarget::Post) MID->SetScalarParameterValue(D.Name, P.*(D.Member));
	}
	WriteCollections(&P);
	MID->SetVectorParameterValue(TEXT("FogColor"), P.FogColor);

	// Modern-look overrides live in our own blendable, so the level's volumes keep their values.
	FPostProcessSettings& S = PostProcess->Settings;
	const FWeightedBlendables Blendables = S.WeightedBlendables;
	S = FPostProcessSettings();
	S.WeightedBlendables = Blendables;
	if (P.bSimplifyLighting)
	{
		S.bOverride_ReflectionMethod = true; S.ReflectionMethod = EReflectionMethod::None;
		S.bOverride_AmbientOcclusionIntensity = true; S.AmbientOcclusionIntensity = 0.f;
	}
	if (P.bDisableModernPostFX)
	{
		S.bOverride_BloomIntensity = true; S.BloomIntensity = 0.f;
		S.bOverride_MotionBlurAmount = true; S.MotionBlurAmount = 0.f;
		S.bOverride_FilmGrainIntensity = true; S.FilmGrainIntensity = 0.f;
		S.bOverride_SceneFringeIntensity = true; S.SceneFringeIntensity = 0.f;
		S.bOverride_VignetteIntensity = true; S.VignetteIntensity = 0.f;
		S.bOverride_LensFlareIntensity = true; S.LensFlareIntensity = 0.f;
		S.bOverride_LocalExposureHighlightContrastScale = true; S.LocalExposureHighlightContrastScale = 1.f;
		S.bOverride_LocalExposureShadowContrastScale = true; S.LocalExposureShadowContrastScale = 1.f;
	}
	// OFF removes the blendable from view evaluation: original PP/camera settings apply unchanged.
	PostProcess->BlendWeight = bActive ? 1.f : 0.f;
	PostProcess->bEnabled = bActive;
	ApplyPipeline(P, bActive);
}

void URetroRenderComponent::ApplyPipeline(const FRetroRenderParameters& P, bool bActive)
{
	// Clouds: transient visibility only (no Modify(), nothing is saved).
	if (bActive && P.bHideVolumetricClouds)
	{
		if (HiddenClouds.IsEmpty())
		{
			for (TObjectIterator<UVolumetricCloudComponent> It; It; ++It)
			{
				if (It->GetWorld() == GetWorld() && It->IsVisible())
				{
					It->SetVisibility(false);
					HiddenClouds.Add(*It);
				}
			}
		}
	}
	else
	{
		for (const TWeakObjectPtr<USceneComponent>& Cloud : HiddenClouds) if (Cloud.IsValid()) Cloud->SetVisibility(true);
		HiddenClouds.Reset();
	}

	// Global CVars: only from a game world, through the shared request stack (restored when released).
	if (!IsGameWorld()) return;
	if (bActive && P.bDisableTemporalAA) SetCVar(TEXT("r.AntiAliasingMethod"), TEXT("0"));
	else ReleaseCVar(TEXT("r.AntiAliasingMethod"));

	FIntPoint ViewSize(0, 0);
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport()) if (Viewport->Viewport) ViewSize = Viewport->Viewport->GetSizeXY();
	const float LowResPercent = ViewSize.Y > 0 ? 100.f * FMath::Max(P.PixelResolutionY, 60.f) / ViewSize.Y : 100.f;
	if (bActive && P.bLowResolutionRender && LowResPercent < 100.f)
	{
		// Viewports already at or below the virtual resolution keep the project's own settings.
		SetCVar(TEXT("r.ScreenPercentage"), FString::SanitizeFloat(FMath::Max(LowResPercent, 10.f)));
		SetCVar(TEXT("r.Upscale.Quality"), TEXT("0"));
	}
	else
	{
		ReleaseCVar(TEXT("r.ScreenPercentage"));
		ReleaseCVar(TEXT("r.Upscale.Quality"));
	}
}

void URetroRenderComponent::SetCVar(const TCHAR* Name, const FString& Value)
{
	RetroRenderState::Push(this, Name, Value);
}

void URetroRenderComponent::ReleaseCVar(const TCHAR* Name)
{
	RetroRenderState::Release(this, Name);
}

void URetroRenderComponent::RestorePipeline()
{
	// Clouds back + every CVar request of this component released (other requesters keep theirs).
	FRetroRenderParameters Off;
	ApplyPipeline(Off, false);
	for (const TCHAR* Name : RetroRenderState::Managed) ReleaseCVar(Name);
}

#if WITH_EDITOR
void URetroRenderComponent::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	const FName Name = Event.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(URetroRenderComponent, InitialPreset)) CurrentPreset = InitialPreset;
	if (Name == GET_MEMBER_NAME_CHECKED(URetroRenderComponent, bPreviewInEditor))
	{
		UWorld* World = GetWorld();
		if (!bPreviewInEditor) Teardown();
		// Turning preview back on must rebuild the blendable; OnRegister is not guaranteed to run again here.
		else if (IsRegistered() && World && World->WorldType == EWorldType::Editor) Setup();
	}
	Apply();
}
#endif

// Runtime switching without UI: Retro.Preset PS1 / Retro.CRT 1 / Retro.Set VertexSnapStrength 0.5
namespace RetroConsole
{
	template <typename F> void ForEach(UWorld* World, F&& Fn)
	{
		for (TObjectIterator<URetroRenderComponent> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsRegistered()) Fn(**It);
		}
	}
	static FAutoConsoleCommandWithWorldAndArgs PresetCmd(TEXT("Retro.Preset"), TEXT("Retro.Preset OFF|PS1|N64|PS2|CRT"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ERetroRenderPreset Preset;
			if (Args.Num() == 1 && RetroPresetFromName(Args[0], Preset)) ForEach(World, [Preset](URetroRenderComponent& C) { C.SetPreset(Preset); });
		}));
	static FAutoConsoleCommandWithWorldAndArgs CRTCmd(TEXT("Retro.CRT"), TEXT("Retro.CRT 0|1 : overlay the CRT layer on the current preset"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() == 1) ForEach(World, [On = Args[0].ToBool()](URetroRenderComponent& C) { C.SetCRTOverlay(On); });
		}));
	static FAutoConsoleCommandWithWorldAndArgs SetCmd(TEXT("Retro.Set"), TEXT("Retro.Set <Parameter> <Value> : edit the current preset"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() == 2) ForEach(World, [&Args](URetroRenderComponent& C) { C.SetParameter(FName(*Args[0]), FCString::Atof(*Args[1])); });
		}));
}
