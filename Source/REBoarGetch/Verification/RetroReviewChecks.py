"""PR #2 review checks (real GPU editor). UnrealEditor.exe REBoarGetch.uproject -ExecutePythonScript=<this> -unattended
 -RenderOffscreen ... -RetroQuit
  A editor : bPreviewInEditor OFF -> ON rebuilds the preview (MID, post process, MPC)
  B PIE    : PS1(+low-res) changes CVars/clouds/MPC/PP; a 2nd component in the same world stays inert and its
             removal changes nothing; OFF restores CVars, clouds, both MPCs and disables the post process
  C bake   : existing bakes are kept by default, overwritten only on request, foreign assets never touched
             (migrates RetroRender.BakedFrom metadata onto the existing /Baked assets first)
Writes Verification/RetroReviewChecks.json. Saves only the metadata migration and the temporary bake folder
(/Game/Art/Rendering/Retro/_BakeTest, deleted at the end)."""
from pathlib import Path
import json
import math
import time
import traceback
import unreal

HERE = Path(__file__).resolve().parent
LEVEL = '/Game/Art/Rendering/Retro/L_RetroCompare'
LIB = unreal.RetroRenderEditorLibrary
lib = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
P = unreal.RetroRenderPreset
CVARS = ['r.AntiAliasingMethod', 'r.ScreenPercentage', 'r.Upscale.Quality']
results = {'checks': [], 'failed': [], 'values': {}}
state = {'gen': None, 'wait': 0, 'h': None, 'deadline': time.monotonic() + 1200}
MPC = unreal.load_asset('/Game/Art/Rendering/Retro/MPC_Retro')
MPC_LP = unreal.load_asset('/Game/InportAssets/LPRiverForest/Materials/Common/MPC_UnlitStrength')


def check(label, ok, detail=None):
    (results['checks'] if ok else results['failed']).append(label + ('' if detail is None else ' [%s]' % detail))
    unreal.log('[RetroReview] %s %s %s' % ('PASS' if ok else 'FAIL', label, '' if detail is None else detail))


def cvars():
    return {n: unreal.SystemLibrary.get_console_variable_float_value(n) for n in CVARS}


def mpcs(world):
    return {'Retro': LIB.get_collection_scalar(world, MPC, 'VertexSnapStrength'),
            'LP': LIB.get_collection_scalar(world, MPC_LP, 'VertexSnapStrength') if MPC_LP else 0.0}


def clouds_visible(world):
    comps = []
    for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        comps += a.get_components_by_class(unreal.VolumetricCloudComponent)
    return [c.is_visible() for c in comps]


def post_process_enabled(actor):
    return [p.get_editor_property('enabled') and p.get_editor_property('blend_weight') > 0 for p in actor.get_components_by_class(unreal.PostProcessComponent)]


def bake_checks():
    # Migrate: bakes made before the metadata existed belong to the baker (they keep their source as complex collision).
    migrated = 0
    for path in lib.list_assets('/Game/Art/Rendering/Retro/Baked', recursive=True, include_folder=False):
        mesh = unreal.load_asset(path)
        source = mesh.get_editor_property('complex_collision_mesh') if isinstance(mesh, unreal.StaticMesh) else None
        if source and not lib.get_metadata_tag(mesh, 'RetroRender.BakedFrom'):
            lib.set_metadata_tag(mesh, 'RetroRender.BakedFrom', source.get_path_name())
            lib.save_loaded_asset(mesh)
            migrated += 1
    results['values']['bake_metadata_migrated'] = migrated

    folder = '/Game/Art/Rendering/Retro/_BakeTest'
    if lib.does_directory_exist(folder):
        lib.delete_directory(folder)
    source = unreal.load_asset('/Game/InportAssets/LPRiverForest/Meshes/Trees/SM_LPTree02')
    baked, report1 = LIB.bake_retro_meshes([source], 0.5, 0, 64, 30.0, True, folder, False)
    first = baked[0]
    check('C1 first bake creates asset with BakedFrom tag', first is not None and lib.get_metadata_tag(first, 'RetroRender.BakedFrom') == source.get_path_name(), report1.splitlines()[-1])
    first.set_editor_property('light_map_resolution', 77)  # stands in for a manual edit
    lib.save_loaded_asset(first)
    baked, report2 = LIB.bake_retro_meshes([source], 0.2, 0, 64, 30.0, True, folder, False)
    check('C2 re-bake without overwrite keeps the existing asset', baked[0] is not None and baked[0].get_editor_property('light_map_resolution') == 77
          and 'kept existing' in report2, report2.splitlines()[-1])
    baked, report3 = LIB.bake_retro_meshes([source], 0.2, 0, 64, 30.0, True, folder, True)
    check('C3 re-bake with overwrite rebuilds it', baked[0] is not None and 'kept existing' not in report3 and 'SKIPPED' not in report3, report3.splitlines()[-1])
    # Foreign asset occupying the target name: never overwritten, even with overwrite on.
    other = unreal.load_asset('/Game/InportAssets/LPRiverForest/Meshes/Trees/SM_LPTree03')
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    foreign = tools.duplicate_asset('SM_LPTree01_Retro', folder, other)
    lib.save_loaded_asset(foreign)
    before = foreign.get_num_triangles(0)
    tree01 = unreal.load_asset('/Game/InportAssets/LPRiverForest/Meshes/Trees/SM_LPTree01')
    baked, report4 = LIB.bake_retro_meshes([tree01], 0.2, 0, 64, 30.0, True, folder, True)
    check('C4 foreign asset with the target name is skipped', baked[0] is None and 'SKIPPED' in report4 and foreign.get_num_triangles(0) == before, report4.splitlines()[-1])
    lib.delete_directory(folder)


def run():
    assert les.load_level(LEVEL)
    yield 30
    world = ues.get_editor_world()
    harness = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RetroCompareActor)[0]
    comp = harness.get_editor_property('renderer')
    # ---------------- A: editor preview toggle
    comp.set_preset(P.PS1)
    yield 5
    check('A1 editor preview active with PS1', comp.is_ready() and comp.is_world_owner() and mpcs(world)['Retro'] == 1.0, mpcs(world))
    comp.set_editor_property('preview_in_editor', False)
    yield 5
    check('A2 preview OFF tears down (no MID, MPC 0, no post process)', not comp.is_ready() and mpcs(world)['Retro'] == 0.0 and not post_process_enabled(harness),
          {'ready': comp.is_ready(), 'mpc': mpcs(world), 'pp': post_process_enabled(harness)})
    comp.set_editor_property('preview_in_editor', True)
    yield 5
    check('A3 preview ON again rebuilds (MID, MPC 1, post process)', comp.is_ready() and mpcs(world)['Retro'] == 1.0 and any(post_process_enabled(harness)),
          {'ready': comp.is_ready(), 'mpc': mpcs(world), 'pp': post_process_enabled(harness)})
    comp.set_preset(P.OFF)
    yield 5
    # ---------------- C: baker (editor only)
    bake_checks()
    yield 5
    # ---------------- B: PIE
    base_cvars = cvars()
    results['values']['cvars_before_pie'] = base_cvars
    les.editor_request_begin_play()
    while not les.is_in_play_in_editor():
        yield 1
    while True:
        pie = ues.get_game_world()
        pc = unreal.GameplayStatics.get_player_controller(pie, 0) if pie else None
        if pc and pc.get_controlled_pawn():
            break
        yield 1
    yield 30
    owner_actor = unreal.GameplayStatics.get_all_actors_of_class(pie, unreal.RetroCompareActor)[0]
    owner = owner_actor.get_editor_property('renderer')
    owner_actor.set_debug_viewport_size(1920, 1080)  # low-res render only kicks in above the virtual resolution
    yield 20
    base_clouds = clouds_visible(pie)
    check('B0 OFF baseline: post process disabled, MPC 0', not any(post_process_enabled(owner_actor)) and mpcs(pie) == {'Retro': 0.0, 'LP': 0.0}, mpcs(pie))
    owner.set_preset(P.PS1)
    p = owner.get_parameters()
    p.set_editor_property('low_resolution_render', True)
    owner.set_preset_parameters(p)
    yield 10
    on = cvars()
    check('B1 PS1 sets AA=0, screen percentage, nearest upscale', on['r.AntiAliasingMethod'] == 0 and 0 < on['r.ScreenPercentage'] < 100 and on['r.Upscale.Quality'] == 0, on)
    check('B2 PS1 hides clouds, MPCs = 1, post process on', not any(clouds_visible(pie)) and mpcs(pie) == {'Retro': 1.0, 'LP': 1.0} and any(post_process_enabled(owner_actor)),
          {'clouds': clouds_visible(pie), 'mpc': mpcs(pie)})
    # A second RetroRenderComponent registered at runtime in the same world (on the player pawn).
    host = pc.get_controlled_pawn()
    second_comp = LIB.add_test_retro_component(host)
    second_comp.set_preset(P.N64)
    yield 10
    check('B3 second component in the same world stays inert', not second_comp.is_world_owner() and not second_comp.is_ready() and owner.is_world_owner(),
          {'second_owner': second_comp.is_world_owner(), 'second_ready': second_comp.is_ready()})
    LIB.remove_test_component(second_comp)
    yield 10
    check('B4 destroying the second component keeps the owner settings', cvars() == on and mpcs(pie) == {'Retro': 1.0, 'LP': 1.0} and not any(clouds_visible(pie)),
          {'cvars': cvars(), 'mpc': mpcs(pie)})
    owner.set_preset(P.OFF)
    yield 10
    check('B5 OFF restores CVars', cvars() == base_cvars, {'now': cvars(), 'base': base_cvars})
    check('B6 OFF restores clouds, MPCs 0, post process off', clouds_visible(pie) == base_clouds and mpcs(pie) == {'Retro': 0.0, 'LP': 0.0} and not any(post_process_enabled(owner_actor)),
          {'clouds': clouds_visible(pie), 'mpc': mpcs(pie), 'pp': post_process_enabled(owner_actor)})
    owner.set_preset(P.PS1)
    owner.set_preset_parameters(p)
    yield 10
    results['values']['cvars_ps1_before_end'] = cvars()
    owner_actor.set_debug_viewport_size(0, 0)
    les.editor_request_end_play()
    while les.is_in_play_in_editor():
        yield 1
    yield 20
    check('B7 ending PIE while PS1 is active restores CVars', cvars() == base_cvars, {'now': cvars(), 'base': base_cvars})


def finish(error=None):
    if state['h'] is not None:
        unreal.unregister_slate_post_tick_callback(state['h'])
        state['h'] = None
    results['error'] = error
    (HERE / 'RetroReviewChecks.json').write_text(json.dumps(results, ensure_ascii=False, indent=1, default=str), encoding='utf-8')
    if les.is_in_play_in_editor():
        les.editor_request_end_play()
    unreal.log('[RetroReview] %s passed=%d failed=%d' % ('ERROR ' + error if error else 'COMPLETE', len(results['checks']), len(results['failed'])))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if '-RetroQuit' in unreal.SystemLibrary.get_command_line():
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    if state.get('busy'):
        return  # slow editor work (bake/save) pumps ticks; never re-enter the generator
    state['busy'] = True
    try:
        if time.monotonic() > state['deadline']:
            raise TimeoutError('timeout')
        if state['wait'] > 0:
            state['wait'] -= 1
            return
        state['wait'] = next(state['gen']) or 0
    except StopIteration:
        finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        state['busy'] = False


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['gen'] = run()
state['h'] = unreal.register_slate_post_tick_callback(tick)
