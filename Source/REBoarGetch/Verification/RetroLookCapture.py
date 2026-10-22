"""Real-GPU PIE capture for the RetroRender rework.
UnrealEditor.exe REBoarGetch.uproject -ExecutePythonScript=<this> -unattended -nosplash -RenderOffscreen -Windowed -ResX=1920 -ResY=1080 -NoSound
Optional: -RetroCaptureSet=presets,breakdown,closeup,motion,lowres,hud,final  (default: all but final)
'final' runs alone: bakes low-poly copies (/Game/Art/Rendering/Retro/Baked), applies the texture utility in memory,
swaps the meshes in the (unsaved) editor world, captures, then restores meshes and texture settings.
Writes Saved/Screenshots/Retro/<stamp>_<label>.png and Verification/RetroLookCapture.json. Saves no assets.
"""
from pathlib import Path
import json
import time
import traceback
import unreal

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent.parent
LEVEL = '/Game/Art/Rendering/Retro/L_RetroCompare'
args = unreal.SystemLibrary.get_command_line()
SETS = 'presets,breakdown,closeup,motion,lowres,hud'
BAKED = '/Game/Art/Rendering/Retro/Baked'
RETRO = unreal.RetroRenderEditorLibrary
undo = {'meshes': [], 'textures': []}
for token in args.split():
    if token.startswith('-RetroCaptureSet='):
        SETS = token.split('=', 1)[1]
SETS = set(SETS.split(','))
STAMP = time.strftime('%Y%m%d_%H%M%S')
level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
P = unreal.RetroRenderPreset
results = {'stamp': STAMP, 'shots': [], 'checks': {}, 'error': None}
state = {'gen': None, 'wait': 0, 'handle': None, 'busy': False, 'deadline': time.monotonic() + 1500}


def log(msg):
    unreal.log('[RetroCapture] ' + msg)


def wait_shaders():
    # Shots must never contain the default material shown while permutations compile.
    stable = 0
    while stable < 3:
        if RETRO.get_remaining_shader_jobs() > 0:
            RETRO.finish_shader_compilation()
            stable = 0
        else:
            stable += 1
        yield 5


def prepare_final():
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    comps, meshes, textures = [], {}, {}
    for a in actors:
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            m = c.get_editor_property('static_mesh')
            if not m:
                continue
            path = m.get_path_name()
            for mat in c.get_materials():
                if mat:
                    for t in unreal.MaterialEditingLibrary.get_material_used_textures(mat):
                        if isinstance(t, unreal.Texture2D) and t.get_path_name().startswith('/Game/'):
                            textures[t.get_path_name()] = t
            if path.startswith('/Engine/') or 'Collision' in path:
                continue  # engine blockout shapes / gameplay collision proxies are not converted
            comps.append(c)
            meshes[path] = m
    order = sorted(meshes)
    # Stage assets are already low-poly (48-1000 tris): 15% (RetroRender default) broke tree silhouettes, 50% keeps them.
    baked, report = RETRO.bake_retro_meshes([meshes[k] for k in order], 0.5, 0, 64, 30.0, True, BAKED)
    results['final_mesh_report'] = report
    # Never swap in a bake that ended up heavier (Nanite source denser than its fallback).
    lookup = {k: b for k, b in zip(order, baked) if b and b.get_num_triangles(0) < meshes[k].get_num_triangles(0)}
    for c in comps:
        src = c.get_editor_property('static_mesh')
        if src.get_path_name() in lookup:
            undo['meshes'].append((c, src))
            c.set_static_mesh(lookup[src.get_path_name()])
    tex = [textures[k] for k in sorted(textures)]
    results['final_texture_report'] = RETRO.retro_textures(tex, 256, True, True, True, True, False)
    undo['textures'] = tex
    log('final: %d components swapped, %d textures' % (len(undo['meshes']), len(tex)))


def restore_final():
    for c, src in undo['meshes']:
        c.set_static_mesh(src)
    if undo['textures']:
        results['final_texture_revert'] = RETRO.revert_retro_textures(undo['textures'], True, False)
    undo['meshes'], undo['textures'] = [], []


def shot(harness, label):
    yield from wait_shaders()
    name = '%s_%s' % (STAMP, label)
    while not harness.request_labeled_screenshot(name):
        yield 1
    yield 2
    while harness.is_screenshot_pending():
        yield 1
    yield 3
    results['shots'].append(name + '.png')
    log('shot ' + name)


def params_of(renderer, preset):
    renderer.set_preset(preset)
    renderer.reset_current_preset()
    return renderer.get_parameters()


def set_fields(p, **values):
    for k, v in values.items():
        p.set_editor_property(k, v)
    return p


def look(pc, pitch, yaw):
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw))


def run():
    assert level_editor.load_level(LEVEL), 'load level'
    yield 30
    if 'final' in SETS:
        prepare_final()
        yield 30
    level_editor.editor_request_begin_play()
    while not level_editor.is_in_play_in_editor():
        yield 1
    world = pc = None
    while True:
        world = worlds.get_game_world()
        pc = unreal.GameplayStatics.get_player_controller(world, 0) if world else None
        if pc and pc.get_controlled_pawn():
            break
        yield 1
    harness = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RetroCompareActor)[0]
    renderer = harness.get_editor_property('renderer')
    harness.set_debug_viewport_size(1920, 1080)
    results['checks']['renderer_ready'] = renderer.is_ready()
    # Warm every preset once so patched-material shader permutations compile before the first shot.
    for preset in [P.PS1, P.N64, P.PS2, P.CRT, P.OFF]:
        renderer.set_preset(preset)
        yield 60
    yield from wait_shaders()
    rot = pc.get_control_rotation()
    base_pitch, base_yaw = rot.pitch, rot.yaw
    look(pc, -12.0, base_yaw)
    yield 30
    unreal.GameplayStatics.set_game_paused(world, True)
    yield 30

    if 'original' in SETS:
        renderer.set_preset(P.OFF)
        yield 30
        yield from shot(harness, 'identity_Original')
        results['checks']['identity_label'] = next((t.split('=', 1)[1] for t in args.split() if t.startswith('-RetroLabel=')), '')

    if 'groundab' in SETS:
        # Ground seam investigation: PS1 + CRT overlay with one pipeline/filter switch changed per shot.
        variants = [('a_current', {}), ('b_aa_on', {'disable_temporal_aa': False}), ('c_box', {'pixel_filter': 1.0}),
                    ('d_aa_on_box', {'disable_temporal_aa': False, 'pixel_filter': 1.0}), ('e_texel0', {'texel_snap_strength': 0.0}),
                    ('f_lighting_on', {'simplify_lighting': False}), ('g_snap0', {'vertex_snap_strength': 0.0, 'affine_amount': 0.0})]
        for pitch, view in ((-12.0, 'path'), (-28.0, 'low')):
            unreal.GameplayStatics.set_game_paused(world, False)
            look(pc, pitch, base_yaw)
            yield 30
            unreal.GameplayStatics.set_game_paused(world, True)
            yield 20
            for label, changes in variants:
                p = params_of(renderer, P.PS1)
                set_fields(p, **changes)
                renderer.set_preset_parameters(p)
                renderer.set_crt_overlay(True)
                yield 30
                yield from shot(harness, 'ground_%s_%s' % (view, label))
        renderer.set_crt_overlay(False)
        renderer.reset_current_preset()

    if 'presets' in SETS:
        for preset, label in [(P.OFF, 'Original'), (P.PS1, 'PS1'), (P.N64, 'N64'), (P.PS2, 'PS2'), (P.CRT, 'CRT')]:
            renderer.set_crt_overlay(False)
            renderer.set_preset(preset)
            renderer.reset_current_preset()
            yield 30
            yield from shot(harness, 'preset_' + label)
        renderer.set_preset(P.PS1)
        renderer.set_crt_overlay(True)
        yield 30
        yield from shot(harness, 'preset_PS1+CRT')
        renderer.set_crt_overlay(False)

    if 'breakdown' in SETS:
        full = params_of(renderer, P.PS1)
        steps = []
        p = params_of(renderer, P.PS1)
        set_fields(p, color_quantization=0.0, dithering_strength=0.0, vertex_snap_strength=0.0, affine_amount=0.0,
                   texel_snap_strength=0.0, saturation=1.0, contrast=1.0, brightness=1.0, disable_temporal_aa=False,
                   simplify_lighting=False, disable_modern_post_fx=False, hide_volumetric_clouds=False)
        steps.append(('1_pixelation', p))
        p2 = params_of(renderer, P.PS1)
        set_fields(p2, vertex_snap_strength=0.0, affine_amount=0.0, texel_snap_strength=0.0, disable_temporal_aa=False,
                   simplify_lighting=False, disable_modern_post_fx=False, hide_volumetric_clouds=False)
        steps.append(('2_pixel_color_dither', p2))
        p3 = params_of(renderer, P.PS1)
        set_fields(p3, texel_snap_strength=0.0, disable_temporal_aa=False, simplify_lighting=False,
                   disable_modern_post_fx=False, hide_volumetric_clouds=False)
        steps.append(('3_plus_vertex_affine', p3))
        steps.append(('4_full_ps1', full))
        for label, values in steps:
            renderer.set_preset(P.PS1)
            renderer.set_preset_parameters(values)
            yield 40
            yield from shot(harness, 'breakdown_' + label)
        renderer.reset_current_preset()

    if 'lowres' in SETS:
        renderer.set_preset(P.PS1)
        p = params_of(renderer, P.PS1)
        set_fields(p, low_resolution_render=True)
        renderer.set_preset_parameters(p)
        yield 40
        yield from shot(harness, 'lowres_PS1_real_lowres_render')
        results['checks']['lowres_screen_percentage'] = unreal.SystemLibrary.get_console_variable_float_value('r.ScreenPercentage')
        renderer.reset_current_preset()
        yield 20
        results['checks']['screen_percentage_restored'] = unreal.SystemLibrary.get_console_variable_float_value('r.ScreenPercentage')

    if 'closeup' in SETS:
        unreal.GameplayStatics.set_game_paused(world, False)
        look(pc, -55.0, base_yaw + 20.0)
        yield 40
        unreal.GameplayStatics.set_game_paused(world, True)
        yield 20
        for preset, label in [(P.OFF, 'Original'), (P.PS1, 'PS1'), (P.N64, 'N64')]:
            renderer.set_preset(preset)
            renderer.reset_current_preset()
            yield 30
            yield from shot(harness, 'closeup_' + label)
        unreal.GameplayStatics.set_game_paused(world, False)
        look(pc, -12.0, base_yaw)
        yield 40
        unreal.GameplayStatics.set_game_paused(world, True)
        yield 20

    if 'motion' in SETS:
        # Same tiny camera steps with and without vertex snapping: snapped edges jump in grid steps.
        for snap in (1.0, 0.0):
            p = params_of(renderer, P.PS1)
            set_fields(p, vertex_snap_strength=snap)
            renderer.set_preset_parameters(p)
            for i in range(4):
                unreal.GameplayStatics.set_game_paused(world, False)
                look(pc, -12.0, base_yaw + i * 0.35)
                yield 3
                unreal.GameplayStatics.set_game_paused(world, True)
                yield 20
                yield from shot(harness, 'motion_snap%d_step%d' % (int(snap), i))
        renderer.reset_current_preset()

    if 'final' in SETS:
        for preset, label in [(P.OFF, 'Original_lowpoly_textures'), (P.PS1, 'PS1'), (P.N64, 'N64'), (P.PS2, 'PS2')]:
            renderer.set_preset(preset)
            renderer.reset_current_preset()
            yield 30
            yield from shot(harness, 'final_' + label)

    if 'gpu' in SETS:
        # Whole-frame GPU time (RHIGetGPUFrameCycles), paused game, same camera; includes editor overhead.
        import statistics
        unreal.SystemLibrary.execute_console_command(world, 't.MaxFPS 0')
        unreal.SystemLibrary.execute_console_command(world, 'r.VSync 0')
        results['gpu_ms'] = {}
        for preset, label in [(P.OFF, 'Original'), (P.PS1, 'PS1'), (P.N64, 'N64'), (P.PS2, 'PS2'), (P.CRT, 'CRT'), (P.OFF, 'Original_again')]:
            renderer.set_preset(preset)
            renderer.reset_current_preset()
            yield from wait_shaders()
            yield 90
            samples = []
            for _ in range(180):
                v = harness.get_gpu_frame_milliseconds()
                if v > 0:
                    samples.append(v)
                yield 1
            results['gpu_ms'][label] = {'median': statistics.median(samples), 'mean': statistics.mean(samples), 'n': len(samples)}
            log('gpu %s %s' % (label, results['gpu_ms'][label]))

    if 'hud' in SETS:
        renderer.set_preset(P.PS1)
        yield 30
        unreal.SystemLibrary.execute_console_command(world, 'shot showui')
        yield 30
        results['checks']['hud_shot'] = 'Saved/Screenshots/WindowsEditor (latest ScreenShot*.png)'

    renderer.set_crt_overlay(False)
    renderer.set_preset(P.OFF)
    yield 10
    results['checks']['aa_restored'] = unreal.SystemLibrary.get_console_variable_float_value('r.AntiAliasingMethod')
    unreal.GameplayStatics.set_game_paused(world, False)
    harness.set_debug_viewport_size(0, 0)


def finish(error=None):
    try:
        restore_final()
    except Exception:
        results['restore_error'] = traceback.format_exc()
    if state['handle'] is not None:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        state['handle'] = None
    results['error'] = error
    (HERE / 'RetroLookCapture.json').write_text(json.dumps(results, ensure_ascii=False, indent=1), encoding='utf-8')
    if level_editor.is_in_play_in_editor():
        level_editor.editor_request_end_play()
    log('ERROR ' + error if error else 'COMPLETE')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if '-RetroQuit' in args:
        unreal.SystemLibrary.quit_editor()


def tick(delta):
    if state['busy']:
        return
    state['busy'] = True
    try:
        if time.monotonic() > state['deadline']:
            raise TimeoutError('capture timeout')
        if state['wait'] > 0:
            state['wait'] -= 1
            return
        try:
            state['wait'] = next(state['gen']) or 0
        except StopIteration:
            finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        state['busy'] = False


state['gen'] = run()
state['handle'] = unreal.register_slate_post_tick_callback(tick)
log('scheduled sets=' + ','.join(sorted(SETS)))
