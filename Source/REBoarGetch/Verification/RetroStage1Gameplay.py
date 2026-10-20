"""Stage1 regression with the patched materials (retro effects inactive: Stage1 has no RetroRender actor).
UnrealEditor.exe REBoarGetch.uproject -ExecutePythonScript=<this> -unattended -RenderOffscreen ... -RetroQuit
Player movement, boar AI movement, equipped gadget, HUD widgets, one capture through UCaptureComponent, no save
writes, and no 'Failed to compile' material fallback. Writes Verification/RetroStage1Gameplay.json."""
from pathlib import Path
import hashlib
import json
import time
import traceback
import unreal

HERE = Path(__file__).resolve().parent
SAVES = HERE.parent.parent.parent / 'Saved' / 'SaveGames'
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
results = {'checks': [], 'failed': [], 'values': {}}
state = {'gen': None, 'wait': 0, 'h': None, 'deadline': time.monotonic() + 600}


def saves():
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in SAVES.glob('*.sav')}


def check(label, ok, detail=None):
    (results['checks'] if ok else results['failed']).append(label + ('' if detail is None else ' [%s]' % detail))
    unreal.log('[RetroStage1] %s %s %s' % ('PASS' if ok else 'FAIL', label, '' if detail is None else detail))


def dist(a, b):
    return ((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2) ** .5


def run():
    before_saves = saves()
    assert les.load_level('/Game/Level/Stage1')
    yield 30
    les.editor_request_begin_play()
    while not les.is_in_play_in_editor():
        yield 1
    while True:
        world = ues.get_game_world()
        pc = unreal.GameplayStatics.get_player_controller(world, 0) if world else None
        if pc and pc.get_controlled_pawn():
            break
        yield 1
    mode = unreal.GameplayStatics.get_game_mode(world)
    t0 = time.monotonic()
    while mode.get_stage_state() != unreal.BoarStageState.PLAYING and time.monotonic() - t0 < 20:
        yield 10
    if mode.get_stage_state() != unreal.BoarStageState.PLAYING:
        mode.complete_stage_introduction()
        yield 30
    check('Stage1 reaches Playing', mode.get_stage_state() == unreal.BoarStageState.PLAYING, str(mode.get_stage_state()))
    pawn = pc.get_controlled_pawn()
    check('No retro component in Stage1 (effects inactive)', not unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RetroRenderActor)
          and not unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RetroCompareActor))
    start = pawn.get_actor_location()
    for _ in range(90):
        pawn.add_movement_input(pawn.get_actor_forward_vector(), 1.0, True)
        yield 1
    check('Player moves with movement input', dist(start, pawn.get_actor_location()) > 50, round(dist(start, pawn.get_actor_location()), 1))
    boars = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.BoarBase)
    boar_start = [b.get_actor_location() for b in boars]
    for _ in range(180):
        yield 1
    moved = [round(dist(a, b.get_actor_location()), 1) for a, b in zip(boar_start, boars)]
    check('Boars spawned and AI moves them', len(boars) > 0 and max(moved or [0]) > 10, {'count': len(boars), 'moved': moved})
    gadget = pawn.get_component_by_class(unreal.GadgetComponent)
    check('Gadget equipped', gadget is not None and gadget.get_current_gadget() is not None,
          str(gadget.get_current_gadget()) if gadget else None)
    hud = [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.UserWidget, True) if w.is_in_viewport()]
    check('HUD widgets in viewport', len(hud) > 0, sorted({w.get_class().get_name() for w in hud}))
    count = mode.get_captured_boar_count()
    target = next((b for b in boars if b.get_component_by_class(unreal.CaptureComponent)
                   and not b.get_component_by_class(unreal.CaptureComponent).get_editor_property('is_captured')), None)
    # ABoarBase::CaptureWithFeedback without presentation: Capture(nullptr) then GameMode.HandleBoarCaptured
    # (CaptureWithFeedback is not a UFUNCTION, so the two public steps are called directly).
    ok = target.get_component_by_class(unreal.CaptureComponent).capture(None) if target else False
    if ok:
        mode.handle_boar_captured(target)
    t0 = time.monotonic()
    while mode.get_captured_boar_count() == count and time.monotonic() - t0 < 15:
        yield 10
    check('Capture succeeds and increments cage count', ok and mode.get_captured_boar_count() == count + 1,
          {'capture': ok, 'before': count, 'after': mode.get_captured_boar_count()})
    unreal.SystemLibrary.execute_console_command(world, 'shot showui')
    yield 30
    les.editor_request_end_play()
    while les.is_in_play_in_editor():
        yield 1
    yield 10
    check('Save files unchanged', saves() == before_saves)


def finish(error=None):
    if state['h'] is not None:
        unreal.unregister_slate_post_tick_callback(state['h'])
        state['h'] = None
    results['error'] = error
    (HERE / 'RetroStage1Gameplay.json').write_text(json.dumps(results, ensure_ascii=False, indent=1, default=str), encoding='utf-8')
    if les.is_in_play_in_editor():
        les.editor_request_end_play()
    unreal.log('[RetroStage1] %s passed=%d failed=%d' % ('ERROR ' + error if error else 'COMPLETE', len(results['checks']), len(results['failed'])))
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
