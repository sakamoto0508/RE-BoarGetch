#include "Rendering/RetroRenderComponent.h"
#include "Components/PostProcessComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "GameFramework/Actor.h"

URetroRenderComponent::URetroRenderComponent()
{
 PrimaryComponentTick.bCanEverTick=false;
 Material=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Art/Rendering/Retro/M_RetroCompare.M_RetroCompare")));
 PresetData=TSoftObjectPtr<URetroRenderPresetData>(FSoftObjectPath(TEXT("/Game/Art/Rendering/Retro/DA_RetroPresets.DA_RetroPresets")));
}
void URetroRenderComponent::BeginPlay()
{
 Super::BeginPlay();
 if (GetNetMode()==NM_DedicatedServer) return;
 const URetroRenderPresetData* Data=PresetData.LoadSynchronous();
 Defaults=(Data && Data->Presets.Num()==5) ? Data->Presets : GetDefault<URetroRenderPresetData>()->Presets;
 Working=Defaults;
 auto* Base=Material.LoadSynchronous();
 if (!Base || Base->GetMaterial()->MaterialDomain!=MD_PostProcess)
 {
  UE_LOG(LogTemp,Error,TEXT("[Retro] Post process material missing/invalid. Run Rendering/SetupRetroAssets.py."));
  return;
 }
 // One MID and one isolated blendable for the entire component lifetime.
 MID=UMaterialInstanceDynamic::Create(Base,this);
 PostProcess=NewObject<UPostProcessComponent>(GetOwner(),NAME_None,RF_Transient);
 PostProcess->bUnbound=true; PostProcess->Priority=1000; PostProcess->BlendWeight=0;
 PostProcess->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f,MID));
 PostProcess->RegisterComponent();
 Apply();
}
void URetroRenderComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 if (PostProcess) PostProcess->DestroyComponent();
 PostProcess=nullptr; MID=nullptr;
 Super::EndPlay(Reason);
}
FRetroRenderParameters URetroRenderComponent::GetParameters() const
{
 return Working.IsValidIndex(int32(CurrentPreset)) ? Working[int32(CurrentPreset)] : FRetroRenderParameters();
}
void URetroRenderComponent::SetPreset(ERetroRenderPreset NewPreset)
{
 if (uint8(NewPreset)>4) return;
 CurrentPreset=NewPreset; Apply();
}
void URetroRenderComponent::SetParameter(FName Name,float Value)
{
 if (!Working.IsValidIndex(int32(CurrentPreset)) || !FMath::IsFinite(Value)) return;
 for (const auto& D:GetRetroParameterDescriptors()) if (Name==D.Name)
 {
  Working[int32(CurrentPreset)].*(D.Member)=FMath::Clamp(Value,D.Min,D.Max); Apply(); return;
 }
}
void URetroRenderComponent::ResetCurrentPreset()
{
 if (Working.IsValidIndex(int32(CurrentPreset))) Working[int32(CurrentPreset)]=Defaults[int32(CurrentPreset)];
 Apply();
}
void URetroRenderComponent::Apply()
{
 if (!MID || !PostProcess) return;
 const auto P=GetParameters();
 for (const auto& D:GetRetroParameterDescriptors()) MID->SetScalarParameterValue(D.Name,P.*(D.Member));
 // OFF unregisters the custom effect from view evaluation, preserving original PP/camera settings.
 PostProcess->BlendWeight=CurrentPreset==ERetroRenderPreset::OFF ? 0.f : 1.f;
 PostProcess->bEnabled=CurrentPreset!=ERetroRenderPreset::OFF;
}
