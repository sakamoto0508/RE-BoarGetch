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
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogRetroRender, Log, All);

URetroRenderComponent::URetroRenderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bTickInEditor = false;
	Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Art/Rendering/Retro/M_RetroPost.M_RetroPost")));
	PresetData = TSoftObjectPtr<URetroRenderPresetData>(FSoftObjectPath(TEXT("/Game/Art/Rendering/Retro/DA_RetroPresets.DA_RetroPresets")));
	ParameterCollection = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT("/Game/Art/Rendering/Retro/MPC_Retro.MPC_Retro")));
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

void URetroRenderComponent::Setup()
{
	if (MID) return;
	const URetroRenderPresetData* Data = PresetData.LoadSynchronous();
	Defaults = (Data && Data->Presets.Num() == 5) ? Data->Presets : GetDefault<URetroRenderPresetData>()->Presets;
	if (Working.Num() != 5) Working = Defaults;
	Collection = ParameterCollection.LoadSynchronous();
	Collections.Reset();
	if (Collection) Collections.Add(Collection);
	for (const TSoftObjectPtr<UMaterialParameterCollection>& Shared : SharedCollections)
	{
		if (UMaterialParameterCollection* Loaded = Shared.LoadSynchronous()) Collections.AddUnique(Loaded);
	}
	UMaterialInterface* Base = Material.LoadSynchronous();
	if (!Base || Base->GetMaterial()->MaterialDomain != MD_PostProcess)
	{
		UE_LOG(LogRetroRender, Error, TEXT("Post process material missing/invalid. Run Plugins/RetroRender/Scripts/SetupRetroAssets.py."));
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

	// Global CVars: only from a game world, original values saved once and restored on OFF/unregister.
	if (!IsGameWorld()) return;
	auto SetCVar = [this](const TCHAR* Name, const FString& Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (!SavedCVars.Contains(Name)) SavedCVars.Add(Name, Var->GetString());
			Var->Set(*Value, ECVF_SetByCode);
		}
	};
	auto RestoreCVar = [this](const TCHAR* Name)
	{
		FString Original;
		if (SavedCVars.RemoveAndCopyValue(Name, Original))
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name)) Var->Set(*Original, ECVF_SetByCode);
		}
	};
	if (bActive && P.bDisableTemporalAA) SetCVar(TEXT("r.AntiAliasingMethod"), TEXT("0"));
	else RestoreCVar(TEXT("r.AntiAliasingMethod"));

	FIntPoint ViewSize(0, 0);
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport()) if (Viewport->Viewport) ViewSize = Viewport->Viewport->GetSizeXY();
	if (bActive && P.bLowResolutionRender && ViewSize.Y > 0)
	{
		const float Rows = FMath::Max(P.PixelResolutionY, 60.f);
		SetCVar(TEXT("r.ScreenPercentage"), FString::SanitizeFloat(FMath::Clamp(100.f * Rows / ViewSize.Y, 10.f, 100.f)));
		SetCVar(TEXT("r.Upscale.Quality"), TEXT("0"));
	}
	else
	{
		RestoreCVar(TEXT("r.ScreenPercentage"));
		RestoreCVar(TEXT("r.Upscale.Quality"));
	}
}

void URetroRenderComponent::RestorePipeline()
{
	FRetroRenderParameters Off;
	ApplyPipeline(Off, false);
	for (const TPair<FString, FString>& Saved : SavedCVars)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Saved.Key)) Var->Set(*Saved.Value, ECVF_SetByCode);
	}
	SavedCVars.Reset();
}

#if WITH_EDITOR
void URetroRenderComponent::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	const FName Name = Event.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(URetroRenderComponent, InitialPreset)) CurrentPreset = InitialPreset;
	if (Name == GET_MEMBER_NAME_CHECKED(URetroRenderComponent, bPreviewInEditor) && !bPreviewInEditor) Teardown();
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
