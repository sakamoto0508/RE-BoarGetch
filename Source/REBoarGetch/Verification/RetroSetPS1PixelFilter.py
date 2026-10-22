"""Sets PixelFilter=1 on the PS1 entry of DA_RetroPresets only (other tuned values are kept)."""
import unreal
lib = unreal.EditorAssetLibrary
data = unreal.load_asset('/Game/Art/Rendering/Retro/DA_RetroPresets')
presets = list(data.get_editor_property('presets'))
before = presets[1].get_editor_property('pixel_filter')
presets[1].set_editor_property('pixel_filter', 1.0)
data.set_editor_property('presets', presets)
lib.save_loaded_asset(data)
unreal.log('[RetroDA] PS1 PixelFilter %s -> %s' % (before, data.get_editor_property('presets')[1].get_editor_property('pixel_filter')))
