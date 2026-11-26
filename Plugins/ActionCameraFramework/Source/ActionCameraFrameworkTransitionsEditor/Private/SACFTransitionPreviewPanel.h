#pragma once

#include "CoreMinimal.h"
#include "ACFTransitionCatalog.h"
#include "ACFTransitionTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "ACFTransitionPreviewSettings.h"

class FACFTransitionView;
class IDetailsView;
class UTexture2D;
class UACFTransitionPreset;
struct FAssetData;

/**
 * The "ACF Transition Preview" tab (README 33 章「Editor Preview」): the effects in a list; select one, press Play (Close ->
 * hold -> Open), change the look in the details view and play again. The preview draws the same widget and material as the
 * game (FACFTransitionView) over a test picture that is also the screen capture of the capture-based effects. "Play in
 * PIE" plays the settings in the running PIE session; "Save as Preset" writes a UACFTransitionPreset.
 * Editor only; no PIE needed for the preview itself.
 */
class SACFTransitionPreviewPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SACFTransitionPreviewPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SACFTransitionPreviewPanel() override;

	// Model (also used by the tests)
	void SelectEffect(EACFTransitionEffect Effect);
	void LoadPreset(const UACFTransitionPreset& Preset);
	EACFTransitionEffect GetSelectedEffect() const { return Selected; }
	void Play(bool bClose, bool bOpen);
	void PlayAll();
	void Stop();
	bool IsPlaying() const { return Mode != EMode::Idle; }
	/** Advances the preview by DeltaSeconds (the active timer calls it every frame while playing). */
	void Advance(float DeltaSeconds);
	float GetCoverage() const { return Coverage; }
	FACFTransitionView* GetView() const { return View.Get(); }
	UACFTransitionPreviewSettings* GetSettings() const { return Settings.Get(); }
	bool PlayInPie();

private:
	enum class EMode : uint8 { Idle, Closing, Holding, Opening };

	TSharedRef<ITableRow> MakeRow(TSharedPtr<FACFTransitionEffectInfo> Item, const TSharedRef<STableViewBase>& Owner);
	void OnPresetSelected(const FAssetData& Asset);
	void SyncEffectSelection();
	void OnListSelection(TSharedPtr<FACFTransitionEffectInfo> Item, ESelectInfo::Type Type);
	EActiveTimerReturnType OnTimer(double CurrentTime, float DeltaTime);
	void EnsureTimer();
	void ApplyFrame();
	void StartPhase(EMode NewMode);
	FReply SaveAsPreset();
	FText GetStatusText() const;

	TArray<TSharedPtr<FACFTransitionEffectInfo>> Items;
	TSharedPtr<SListView<TSharedPtr<FACFTransitionEffectInfo>>> List;
	TSharedPtr<IDetailsView> Details;
	TStrongObjectPtr<UACFTransitionPreviewSettings> Settings;
	FString PreviewPresetPath;
	TStrongObjectPtr<UTexture2D> Picture;
	FSlateBrush PictureBrush;
	TSharedPtr<FACFTransitionView> View;
	TSharedPtr<FActiveTimerHandle> Timer;

	EACFTransitionEffect Selected = EACFTransitionEffect::BlackFade;
	EMode Mode = EMode::Idle;
	bool bPlayOpenAfterClose = true;
	bool bPlayingAll = false;
	float PhaseTime = 0.f;
	float EffectTime = 0.f;
	float Coverage = 0.f;
	EACFTransitionPhase DrawPhase = EACFTransitionPhase::Close;
};
