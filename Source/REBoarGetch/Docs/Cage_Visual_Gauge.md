# Cage visual / HP gauge

- Native `UCageVisualComponent` is attached to the existing ACage root. Original cage mesh and four gameplay collision boxes are retained.
- `UCageHealthWidget` paints the simple horizontal bar on a 640x100 World Space WidgetComponent. This is a C++ Widget, not a new WBP asset.
- CagePresentation / Gauge settings: DisplayDistance 12000 cm (0 = unlimited), GaugeHeight 650 cm, GaugeWorldScale 0.9, MediumThreshold 0.6, LowThreshold 0.25.
- Presentation subscribes to OnHealthChanged / OnCageDestroyed / OnRespawned. No health, recovery, capture, release, or respawn logic is rewritten.
- Camera facing updates at 20 Hz. Gauge rendering is requested only for value changes, trailing damage, white flash (0.18 seconds), or low-HP pulse.
- Materials: /Game/BP/Cage/Materials/M_Cage_White, M_Cage_Navy, M_Cage_Yellow, M_Cage_Glow. Glow has Tint and Intensity parameters; runtime lamp instances are transient.
- All added geometry and gauge components disable collision and navigation influence. Existing imported cage assets are unchanged.
- C++ Development Editor build succeeded. BP_Cage compiled and saved; /Game/Level/Test saved. Editor viewport confirmed shell placement.
- PIE was not run. User should verify camera facing, distance visibility, high/mid/low HP, damage flash/trail, recovery, destruction/respawn, captured boar visibility, and contact with the visually thicker frames/base.
- The current request's reference image was not received; implementation follows the written A horizontal-bar specification.
