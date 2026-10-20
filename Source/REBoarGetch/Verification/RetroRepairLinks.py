"""One-off repair (2026-10-20): restores links removed by a faulty orphan purge in PatchRetroMaterials.py.
Reconstructed from the dangling original nodes (see RetroDiagDangling.log / RetroDiagWater.log)."""
import unreal
mel = unreal.MaterialEditingLibrary
lib = unreal.EditorAssetLibrary
WPO = unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET
def nodes(m): return {e.get_name(): e for e in mel.get_material_expressions(m)}
def tagged(e): return str(e.get_editor_property('desc')).startswith('RetroRender')
def wpo_restore(path, source):
    m = unreal.load_asset(path)
    ex = nodes(m)
    # Drop our (now input-less) snap node, put the original chain back on WPO; the patcher re-wraps it.
    ours = [e for e in ex.values() if tagged(e)]
    if ours: unreal.RetroRenderEditorLibrary.remove_retro_expressions(m, ours)
    assert mel.connect_material_property(ex[source], '', WPO), path
    errors = mel.recompile_material(m)
    assert not errors, errors
    lib.save_loaded_asset(m)
    unreal.log('[Repair] %s: WPO <- %s' % (path, source))
wpo_restore('/Game/InportAssets/LPRiverForest/Materials/M_LowPoly', 'MaterialExpressionAdd_5')
wpo_restore('/Game/InportAssets/LPRiverForest/Materials/M_LowPolyGrass', 'MaterialExpressionMultiply_2')
water = '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyWater'
m = unreal.load_asset(water)
ex = nodes(m)
ours = [e for e in ex.values() if tagged(e)]
if ours: unreal.RetroRenderEditorLibrary.remove_retro_expressions(m, ours)
s = ex['MaterialExpressionTextureSample_1']
s.set_editor_property('mip_value_mode', unreal.TextureMipValueMode.TMVM_NONE)
assert mel.connect_material_expressions(ex['MaterialExpressionPanner_0'], '', s, 'UVs')
assert mel.connect_material_expressions(ex['MaterialExpressionTextureCoordinate_0'], '', ex['MaterialExpressionMultiply_6'], 'A')
assert mel.connect_material_property(ex['MaterialExpressionReroute_13'], '', WPO)
errors = mel.recompile_material(m)
assert not errors, errors
lib.save_loaded_asset(m)
unreal.log('[Repair] water: UV panner, TexCoord, WPO restored')
copy = '/Game/Art/Rendering/Retro/Materials/M_RetroFBXPhong'
if lib.does_asset_exist(copy):
    assert lib.delete_asset(copy), 'delete FBX copy'
    unreal.log('[Repair] deleted damaged ' + copy + ' (re-duplicated by the patcher)')
unreal.log('[Repair] COMPLETE')
if '-RetroQuit' in unreal.SystemLibrary.get_command_line():
    unreal.SystemLibrary.quit_editor()
