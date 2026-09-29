# Lobby dedicated facilities

Notion checked: Lobby / Stage 1 space design updated 2026-09-26. Lobby is now a hub for separate facilities.

## Assets and responsibility
- /Game/Level/L_GadgetLab: 24 x 18 m lab; existing loadout UI terminal, movement/ramp/steps and two attack-interface test targets.
- /Game/Level/L_Archive: 18 x 14 m archive; existing encyclopedia UI terminal and three display stations.
- /Game/Level/L_Lobby: Stage Select preserved, left/right portal visuals reuse the central gate meshes at 80% scale. Old same-level functionality is hidden/disabled, not deleted.
- /Game/BP/Lobby/BP_GM_GadgetLab and BP_GM_Archive: BoarFacilityGameMode (GameModeBase), using the existing player and BP_PC_Lobby. No StageConfig or stage progression.
- /Game/Lobby/Materials/M_Facility_Sky: simple bright unlit sky backdrop.
- AHubPortal: local player overlap; Travel retains a soft World reference for asset dependencies, strips the PIE prefix with UWorld::RemovePIEPrefix, validates the real package, and calls OpenLevel with the normalized package name. Loadout/Archive actions request the existing UI through BoarPlayerController.
- AGadgetTestTarget: existing AttackActivatable interface; yellow feedback on attack notification, resets after 0.6 seconds. It is not a capturable Boar and does not exercise capture/AI behavior.

## User flow
Lobby left/right gate -> dedicated level -> walk onto cyan terminal pad -> existing UI -> Back returns to local control. Leave and re-enter the pad to reopen. Walk into LOBBY gate to return to the existing lobby start; spawn and exit triggers are separated.

## Preserved
StageEntrance.cpp, stage catalog/config, Stage Select widget, GadgetComponent, existing loadout/encyclopedia widget logic and save routines were not changed. PlayerController only adds dedicated facility UI ownership/cleanup and input gating while it is open. No new input action or mapping.

## Validation
C++ Development Editor build and both new GameMode Blueprint compiles succeeded. All maps/assets saved. Soft destinations, terminal modes, spawn separation and the original Stage Select references were re-read. PIE, runtime interaction and packaged cooking were not run.

## User checks
- Left -> GadgetLab and right -> Archive; each LOBBY exit returns without a travel loop.
- Terminal UI, gamepad focus and Back; movement/gadget use resumes after closing.
- Loadout change persists and is available on return / stage entry.
- Gadget animation, jump/ramp/steps; test-target feedback only for attacks that call the existing attack-activation interface. Capture-specific effects need actual Boars and are outside this test room.
- Encyclopedia/save progress remains correct; no stage clear/result processing in either facility.
- Portal/frame collision and signs from the player camera; packaged build includes both soft-referenced maps.

