# Architecture pass — 2026-09-29

Targets: `/Game/Level/L_GadgetLab`, `/Game/Level/L_Lobby`.

Before editing, Unreal MCP audit confirmed Lab 2400 x 1800 cm and Lobby 3600 x 3000 cm, existing station, pods, portals, start positions, low walls and open roofs. The current Lobby's unsaved edits were saved intact before level switching.

Lab: broad light floor panels, faceted Navy station inset, restrained Cyan circulation lines, short Yellow markers, perimeter floor borders/baseboards. Navy display bays with repeated white structural framing behind wall-side pod positions. Original room and movement collision unchanged. Roof at approximately 760 cm: large white panels, Navy beams, rectangular white luminaires, station coffer and small ventilation panels.

Lobby: high atrium roof at approximately 1600 cm, central luminous skylight panel, radial beams aligned with portal directions, high perimeter enclosure/clerestory panels. Existing plaza, garden, gates and floor are retained.

Assets added:
- `/Game/Lobby/Meshes/SM_L_GadgetLab_Architecture_*`
- `/Game/Lobby/Meshes/SM_L_Lobby_Architecture_*`
- `/Game/Lobby/Meshes/SM_L_GadgetLab_CeilingDiffuser`
- `/Game/Lobby/Meshes/SM_L_Lobby_CeilingDiffuser`
- `/Game/Lobby/Materials/M_Architecture_SoftWhite`
- `/Game/Lobby/Materials/M_Architecture_CeilingWhite`

All new architecture actors use NoCollision and do not affect navigation. Existing meshes, props, Blueprint logic, transition settings and input were not changed by this architectural pass. Original actor transforms and hidden flags were compared before/after and matched. Materials reused the Lobby palette; the new ceiling face material is Unlit for a stable stylized white underside. New architecture casts no shadows to preserve existing bright daylight. This is a stylized lighting choice, not physically accurate indoor lighting.

Lighting: four neutral 5800 K RectLights per room, Lab 500 lm each / Lobby 1200 lm each. Existing lighting is retained. Cyan remains architectural accent rather than general illumination.

Material compilation, asset/level saves and reload checks completed. Editor views inspected Lab roof/station/bay and Lobby atrium/three portal signs. Serialized actor/material/collision/light evidence is in `verify_architecture_finish.json`.

No PIE. User checks: camera swing/jump near roof and walls; movement and portal access; indoor exposure on target graphics settings; all viewing angles; lighting performance. Ceiling is visual-only and therefore does not block the camera; extreme debug/free-camera travel can leave the enclosure.
