"""PIE check of the project-wide Retro Render default (Project Settings > Plugins > Retro Render).
UnrealEditor.exe REBoarGetch.uproject -ExecutePythonScript=<this> -unattended -RenderOffscreen ... -RetroQuit
For each map: auto actor spawned (or the level's own actor wins), preset/CRT/MPC/CVars as configured, screenshot,
no material compile fallback; on L_Stage01 also movement + capture; after PIE the CVars are back.
Writes Verification/RetroAutoApplyCheck.json and Saved/Screenshots/Retro/<stamp>_auto_<map>.png."""
from pathlib import Path
import json
import time
import traceback
import unreal

HERE = Path(__file__).resolve().parent
MAPS = ['/Game/Level/L_Lobby', '/Game/Level/L_Stage01', '/Game/Level/L_GadgetLab', '/Game/Level/L_Archive',
        '/Game/Art/Rendering/Retro/L_RetroCompare']
STAMP = time.strftime('%Y%m%d_%H%M%S')
LIB = unreal.RetroRenderEditorLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
MPC = unreal.load_asset('/Game/Art/Rendering/Retro/MPC_Retro')
results = {'checks': [], 'failed': [], 'shots': []}
state = {'gen': None, 'wait': 0, 'h': None, 'busy': False, 'deadline': time.monotonic() + 1800}
CVARS = ['r.AntiAliasingMethod', 'r.ScreenPercentage', 'r.Upscale.Quality']


def check(label, ok, detail=None):
    (results['checks'] if ok else results['failed']).append(label + ('' if detail is None else ' [%s]' % detail))
    unreal.log('[RetroAuto] %s %s %s' % ('PASS' if ok else 'FAIL', label, '' if detail is None else detail))


def cvars():
    return {n: unreal.SystemLibrary.get_console_variable_float_value(n) for n in CVARS}


def wait_shaders():
    stable = 0
    while stable < 3:
        if LIB.get_remaining_shader_jobs() > 0:
            LIB.finish_shader_compilation()
            stable = 0
        else:
            stable += 1
        yield 5


def renderers(world):
    found = []
    for cls in (unreal.RetroRenderActor, unreal.RetroCompareActor):
        for a in unreal.GameplayStatics.get_all_actors_of_class(world, cls):
            found.append((a, a.get_editor_property('renderer')))
    return found


def run():
    base = cvars()
    results['cvars_before'] = base
    for level in MAPS:
        name = level.split('/')[-1]
        assert les.load_level(level), level
        yield 30
        les.editor_request_begin_play()
        while not les.is_in_play_in_editor():
            yield 1
        world = None
        t0 = time.monotonic()
        while time.monotonic() - t0 < 30:
            world = ues.get_game_world()
            pc = unreal.GameplayStatics.get_player_controller(world, 0) if world else None
            if pc and pc.get_controlled_pawn():
                break
            yield 5
        yield 30
        found = renderers(world)
        owners = [(a, r) for a, r in found if r.is_world_owner()]
        auto = [a for a, r in found if a.get_name().startswith('RetroRender_ProjectDefault')]
        if name == 'L_RetroCompare':
            check(name + ': level actor wins, no auto actor', len(owners) == 1 and isinstance(owners[0][0], unreal.RetroCompareActor) and not auto,
                  [a.get_name() for a, _ in found])
        else:
            ok = len(owners) == 1 and owners[0][0].get_name() in {a.get_name() for a in auto}  # names: wrapper equality is unreliable
            r = owners[0][1] if owners else None
            check(name + ': project default auto-applied (PS1 + CRT)', ok and r.get_preset() == unreal.RetroRenderPreset.PS1 and r.is_crt_overlay() and r.is_ready(),
                  {'actors': [a.get_name() for a, _ in found], 'preset': str(r.get_preset()) if r else None})
            check(name + ': MPC + AA follow the preset', LIB.get_collection_scalar(world, MPC, 'VertexSnapStrength') == 1.0 and cvars()['r.AntiAliasingMethod'] == 0,
                  {'snap': LIB.get_collection_scalar(world, MPC, 'VertexSnapStrength'), 'cvars': cvars()})
        yield from wait_shaders()
        yield 30
        shot = '%s_auto_%s' % (STAMP, name)
        unreal.SystemLibrary.execute_console_command(world, 'HighResShot 1920x1080 filename=%s/Screenshots/Retro/%s.png' % (unreal.Paths.project_saved_dir().rstrip('/'), shot))
        yield 30
        results['shots'].append(shot + '.png')
        if name == 'L_Stage01':
            pawn = unreal.GameplayStatics.get_player_controller(world, 0).get_controlled_pawn()
            mode = unreal.GameplayStatics.get_game_mode(world)
            t1 = time.monotonic()
            while mode.get_stage_state() != unreal.BoarStageState.PLAYING and time.monotonic() - t1 < 20:
                yield 10
            start = pawn.get_actor_location()
            for _ in range(90):
                pawn.add_movement_input(pawn.get_actor_forward_vector(), 1.0, True)
                yield 1
            moved = (pawn.get_actor_location() - start).length()
            check('L_Stage01: player moves under PS1', moved > 50, round(moved, 1))
            boars = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.BoarBase)
            target = next((b for b in boars if b.get_component_by_class(unreal.CaptureComponent)), None)
            count = mode.get_captured_boar_count()
            ok = target.get_component_by_class(unreal.CaptureComponent).capture(None) if target else False
            if ok:
                mode.handle_boar_captured(target)
            yield 60
            check('L_Stage01: capture under PS1', ok and mode.get_captured_boar_count() == count + 1,
                  {'boars': len(boars), 'before': count, 'after': mode.get_captured_boar_count()})
        les.editor_request_end_play()
        while les.is_in_play_in_editor():
            yield 1
        yield 20
        check(name + ': CVars restored after PIE', cvars() == base, cvars())


def finish(error=None):
    if state['h'] is not None:
        unreal.unregister_slate_post_tick_callback(state['h'])
        state['h'] = None
    results['error'] = error
    (HERE / 'RetroAutoApplyCheck.json').write_text(json.dumps(results, ensure_ascii=False, indent=1, default=str), encoding='utf-8')
    if les.is_in_play_in_editor():
        les.editor_request_end_play()
    unreal.log('[RetroAuto] %s passed=%d failed=%d' % ('ERROR ' + error if error else 'COMPLETE', len(results['checks']), len(results['failed'])))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if '-RetroQuit' in unreal.SystemLibrary.get_command_line():
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    if state['busy']:
        return
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
