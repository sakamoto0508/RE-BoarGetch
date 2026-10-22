#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "RetroRenderEditorTypes.h"

class IDetailsView;
class SWidgetSwitcher;
class URetroLookEditorState;
class URetroTextureOptions;
class URetroDecimateOptions;
class URetroRenderComponent;

// "Retro Render" editor tab: Look (preset + live parameters), Textures (batch retro settings), Meshes (decimate baker).
class SRetroRenderPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRetroRenderPanel) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void SetActiveTab(int32 Index);
	// Test hooks (same paths as the buttons).
	void TestSelectPreset(ERetroRenderPreset P) { SelectPreset(P); }
	void TestPreviewTextures(const TArray<class UTexture2D*>& Textures);

private:
	TSharedRef<SWidget> BuildLook();
	TSharedRef<SWidget> BuildTextures();
	TSharedRef<SWidget> BuildMeshes();
	TSharedRef<SWidget> TabButton(const FText& Label, int32 Index);
	TSharedRef<SWidget> ActionButton(const FText& Label, const FText& Tip, TFunction<void()> Action);

	// Every registered component in the editor world and in running PIE worlds.
	TArray<URetroRenderComponent*> LiveComponents() const;
	URetroRenderComponent* Primary() const;
	void SelectPreset(ERetroRenderPreset Preset);
	void PushLook();
	void PullLook();
	void AddActorToLevel();
	void SaveToPresetAsset();
	TArray<class UTexture2D*> SelectedTextures() const;
	TArray<class UStaticMesh*> SelectedMeshes() const;
	FText StatusText() const;

	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<IDetailsView> LookDetails;
	TSharedPtr<IDetailsView> TextureDetails;
	TSharedPtr<IDetailsView> MeshDetails;
	TStrongObjectPtr<URetroLookEditorState> LookState;
	TStrongObjectPtr<URetroTextureOptions> TextureOptions;
	TStrongObjectPtr<URetroDecimateOptions> MeshOptions;
	ERetroRenderPreset Preset = ERetroRenderPreset::PS1;
	bool bCRTOverlay = false;
	FString TextureReport;
	FString MeshReport;
	FString LevelReport;
};
