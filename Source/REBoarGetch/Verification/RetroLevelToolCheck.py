"""Editor check of the per-level material tool (Retro Render panel buttons + map-open notification check).
Builds a throwaway level with two new unpatched materials, scans (dry run), patches, verifies, then deletes
/Game/Art/Rendering/Retro/_LevelToolTest and removes the test entries from PatchRetroMaterials.json."""
from pathlib import Path
import json
import time
import traceback
import unreal

HERE = Path(__file__).resolve().parent
RECORD = HERE.parent.parent.parent / 'Plugins' / 'RetroRender' / 'Scripts' / 'PatchRetroMaterials.json'
FOLDER = '/Game/Art/Rendering/Retro/_LevelToolTest'
LIB = unreal.RetroRenderEditorLibrary
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
WPO = unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET
results = {'checks': [], 'failed': [], 'reports': {}}
st = {'n': 0, 'h': None}


def check(label, ok, detail=None):
    (results['checks'] if ok else results['failed']).append(label + ('' if detail is None else ' [%s]' % detail))
    unreal.log('[RetroLevelTool] %s %s %s' % ('PASS' if ok else 'FAIL', label, '' if detail is None else detail))


def make_material(name, with_wpo):
    m = tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    color = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -300, 0)
    color.set_editor_property('constant', unreal.LinearColor(0.2, 0.7, 0.3, 1))
    mel.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    if with_wpo:
        offset = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -300, 300)
        offset.set_editor_property('constant', unreal.LinearColor(0, 0, 5, 1))
        mel.connect_material_property(offset, '', WPO)
    mel.recompile_material(m)
    lib.save_loaded_asset(m)
    return m


def main():
    if lib.does_directory_exist(FOLDER):
        lib.delete_directory(FOLDER)
    flat = make_material('M_ToolTest_Flat', False)
    wavy = make_material('M_ToolTest_Wpo', True)
    original_wpo = mel.get_material_property_input_node(wavy, WPO).get_name()
    assert les.new_level(FOLDER + '/L_ToolTest')
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    for i, mat in enumerate([flat, wavy]):
        a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(i * 300, 0, 0))
        c = a.get_component_by_class(unreal.StaticMeshComponent)
        c.set_static_mesh(cube)
        c.set_material(0, mat)
    unpatched = LIB.find_unpatched_level_materials()
    check('map-open check finds both new materials', sorted(p.split('/')[-1] for p in unpatched) == ['M_ToolTest_Flat', 'M_ToolTest_Wpo'], unpatched)
    dry = LIB.run_level_material_scan(True)
    results['reports']['dry'] = dry
    check('Scan (dry run) lists them as [patch] and changes nothing', 'patch=2' in dry and len(LIB.find_unpatched_level_materials()) == 2, dry.splitlines()[0])
    real = LIB.run_level_material_scan(False)
    results['reports']['patch'] = real
    check('Patch Level Materials patches both', 'patched=2' in real and not LIB.find_unpatched_level_materials(), real.splitlines()[0])
    snap = mel.get_material_property_input_node(wavy, WPO)
    source = mel.get_inputs_for_material_expression(wavy, snap)[0]
    check('existing WPO chained into the snap function', snap.get_editor_property('material_function').get_name() == 'MF_RetroVertexSnap'
          and source is not None and source.get_name() == original_wpo, {'snap_input': source.get_name() if source else None})
    record = json.loads(RECORD.read_text(encoding='utf-8'))
    check('auto targets remembered for unpatch/check', all(FOLDER + '/' + n in record.get('auto_targets', {}) for n in ['M_ToolTest_Flat', 'M_ToolTest_Wpo']))
    again = LIB.run_level_material_scan(True)
    check('second scan reports them as ok (idempotent)', 'patch=' not in again, again.splitlines()[0])
    # Cleanup: leave the test level, delete the assets and their record entries.
    les.load_level('/Game/Level/L_Archive')
    lib.delete_directory(FOLDER)
    record = json.loads(RECORD.read_text(encoding='utf-8'))
    record['auto_targets'] = {k: v for k, v in record.get('auto_targets', {}).items() if not k.startswith(FOLDER)}
    RECORD.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding='utf-8')
    check('test assets removed', not lib.does_directory_exist(FOLDER) or not lib.list_assets(FOLDER))


def tick(dt):
    st['n'] += 1
    if st['n'] < 60:
        return
    unreal.unregister_slate_post_tick_callback(st['h'])
    try:
        main()
    except Exception:
        results['error'] = traceback.format_exc()
        unreal.log_error('[RetroLevelTool] ' + results['error'])
    (HERE / 'RetroLevelToolCheck.json').write_text(json.dumps(results, ensure_ascii=False, indent=1), encoding='utf-8')
    unreal.log('[RetroLevelTool] COMPLETE passed=%d failed=%d' % (len(results['checks']), len(results['failed'])))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
st['h'] = unreal.register_slate_post_tick_callback(tick)
