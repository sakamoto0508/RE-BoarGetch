"""One-off repair #2 (2026-10-20): re-feed the original WPO chains into the snap nodes after the MF output GUIDs
were re-created by an earlier SetupRetroAssets run. Same sources as RetroRepairLinks.py."""
import unreal
mel = unreal.MaterialEditingLibrary
lib = unreal.EditorAssetLibrary
for path, source in [('/Game/InportAssets/LPRiverForest/Materials/M_LowPoly', 'MaterialExpressionAdd_5'),
                     ('/Game/InportAssets/LPRiverForest/Materials/M_LowPolyWater', 'MaterialExpressionReroute_13')]:
    m = unreal.load_asset(path)
    ex = {e.get_name(): e for e in mel.get_material_expressions(m)}
    snap = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    assert snap and str(snap.get_editor_property('desc')).startswith('RetroRender'), path
    assert mel.connect_material_expressions(ex[source], '', snap, 'WPO'), path
    errors = mel.recompile_material(m)
    assert not errors, errors
    lib.save_loaded_asset(m)
    unreal.log('[Repair] %s: %s -> snap.WPO' % (path, source))
copy = '/Game/Art/Rendering/Retro/Materials/M_RetroFBXPhong'
if lib.does_asset_exist(copy):
    assert lib.delete_asset(copy)
    unreal.log('[Repair] deleted ' + copy)
unreal.log('[Repair] COMPLETE')
if '-RetroQuit' in unreal.SystemLibrary.get_command_line():
    unreal.SystemLibrary.quit_editor()
