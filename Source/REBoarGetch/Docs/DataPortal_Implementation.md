# Data Portal / 2026-09-29

## Assets
- `/Game/Lobby/Materials/M_DataPortal_Tunnel`: masked, two-sided, Unlit surface. Custom HLSL source in `DataPortal.usf`.
- `/Game/Lobby/Materials/MI_DataPortal_Default`: shared tuning instance.
- `/Game/Lobby/Meshes/SM_DataPortal_Surface`: opening plane fitted to the supplied Meshy frame; collision disabled.
- Frame uses the supplied `/Game/InportAssets/ImportMeshy/Meshy_AI_Neon_Portal_Frame_0928055927_texture/StaticMeshes` asset and its original material without editing either.

## Placement
Seven functional entrances in L_Lobby (3), L_GadgetLab (2), L_Archive (1), L_GadgetTest_Net (1). Old frame actors retained with rendering disabled; existing gameplay trigger transforms and collision settings retained. New render actors have no collision and do not affect navigation.

The manually placed Meshy frame in GadgetLab is reused at the Return entrance. The redundant new Return frame actor is retained hidden. Duplicate ReturnSign is hidden; ReturnTitle remains. TestVisualActors references the new Test frame/surface plus the original title/pad, preserving selection-controlled visibility.

## Parameters (MI defaults)
| Parameter | Value | Purpose |
|---|---:|---|
| EmissiveStrength | 2 | Overall line emission |
| GridDensity | 5 | Perspective grid density |
| GridBrightness | 0.10 | Grid emission |
| ScrollSpeed | 0.18 | Inward flow speed |
| NoiseStrength | 0.16 | Subtle noise/scanline/glitch strength |
| DistortionStrength | 0.006 | Restrained UV jitter |
| CenterBlackness | 0.96 | Ambient darkness (1 = black) |
| DataBrightness | 0.5 | Sparse procedural square brightness |
| RimBrightness | 0.7 | Thin internal boundary emission |
| PortalColor | (0.015, 0.45, 0.85) | Linear RGB cyan-blue |

Open MI_DataPortal_Default and enable/edit parameter overrides. For per-entrance tuning, create a separate material instance and assign it to that Surface actor's material slot. The frame material is independent.

Depth is a material perspective illusion, not an actual tunnel or camera-parallax volume. Time animates grid, square packets and restrained glitch; no Niagara system is needed. Existing shared Lobby hologram decorations remain.

## Verification
Material recompiled and rendered in Editor. Assets and all four levels saved and reloaded; report: finish_data_portals.json. Editor screenshots checked Stage entrance and Lab portals. No PIE/runtime test performed.

User checks: approach from player height, transition/return at each entrance, Test portal hidden before selection and shown after selection, animation speed, dark-center readability, frame threshold alignment and old collision boundaries. Runtime performance and all camera angles remain unverified.
