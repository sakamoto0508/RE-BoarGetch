# Capture success feedback

Notion source: https://app.notion.com/p/3eab6c88752581daa745f4436ac77adc

## Editor settings
BP_BoarPlayerController / BP_PC_Lobby > CapturePresentation component:
- HitStopDuration .06 real seconds; HitStopTimeScale .01 (near-stop, not Pause).
- SlowDuration .26 real seconds; SlowTimeScale .3.
- CameraZoomAmount 10 degrees; CameraDuration .28 real seconds.
- GlowDuration .34 real seconds; VFXScale .55.
- GlowMaterial / CaptureVFX / CameraShake can be replaced.
- ImpactSound and CaptureSuccessSound intentionally unset: imported sounds were inventoried, but their suitability has not been verified. Assign appropriate Sound assets here. Playback uses BoarAudioManagerSubsystem only.

## Flow / ownership
NetGadget resets one success-feedback flag per use. Only successful CaptureComponent capture sets it. Empty swings do not start presentation.
BoarBase::CaptureWithFeedback preserves captured flag/movement stop, then queues presentation at the capture location. The existing Capture() API remains.
CapturePresentationComponent owns real-time sequencing, temporary overlay, Niagara instances, saved camera FOV/time scale and its own shake. It does not change original Boar materials or camera orientation.
At visual completion, restore overlays and shared state, then call the existing BoarGameMode::HandleBoarCaptured. Cage collection, item drop and count update remain there. Clear requests wait until pending presentation/batch collection finishes, preventing partial multi-capture clear.
World tick real-time deltas drive completion; no dilated Timer is used. Pause suspends age and restores camera/time scale. Normal completion, stage end, controller/component EndPlay and destroyed targets clean up their owned state. Existing unrelated shakes are not stopped.

## Assets
/Game/REBoarGetch/Art/VFX/Capture/M_CaptureGlow: translucent unlit skeletal-mesh overlay; GlowAmount/GlowColor/GlowStrength parameters.
/Game/REBoarGetch/Art/VFX/Capture/NS_CaptureSuccess: copy of imported NS_JumpPad; broad base glow disabled, ring lifetime .28s, ring rate8, small rising particles rate24, CPU simulation. Original imported system unchanged. Component ends the effect after the short capture presentation.

## User PIE checks (not run by Codex)
1. Empty swing: unchanged cooldown/animation; no success glow, slow or confirm.
2. Single capture: original location glow, brief stop/slow/zoom, then Cage and count/drop.
3. Three captures in one swing, including later overlaps: shared feedback once, all eligible boars collected once.
4. Final capture: capture presentation completes before Clear presentation.
5. Pause/resume during each phase: no stuck time scale/zoom or premature Cage collection.
6. GameOver, Retry, Level transition and destroyed target/player during presentation: no overlay, VFX, FOV or time-scale residue.
7. Confirm actual VFX size/visibility, glow brightness, camera comfort and selected sound timing.

Runtime behavior and perceptual tuning are unverified until these checks. No PIE is used for asset compilation.

## Verification result
- Development Editor C++ build: Succeeded (UE 5.8).
- BP_BoarPlayerController / BP_PC_Lobby: presentation component and asset references retrieved after Compile/Save.
- BP_NormalBoar / BP_NetGadget: compiled through BlueprintEditorLibrary, no property changes.
- NS_CaptureSuccess: UpToDate, bHasErrors=false, bHasWarnings=false after Editor restart.
- Dirty map/content packages: none. PIE: false.
- Editor startup logged unrelated ABP_Player preview warnings about an unset PlayerCharacter; its animation graph was not changed.
- Audio slots remain unset; no claim of audible success feedback until suitable sounds are assigned.
