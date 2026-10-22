"""GadgetLab: logs pawn/camera, captures OFF vs PS1+CRT from the same view (diagnoses the blue-grid screenshot)."""
import time, traceback, unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LIB = unreal.RetroRenderEditorLibrary
st = {'g': None, 'w': 0, 'h': None, 'busy': False}
STAMP = time.strftime('%Y%m%d_%H%M%S')
def shot(world, label):
    unreal.SystemLibrary.execute_console_command(world, 'HighResShot 1920x1080 filename=%s/Screenshots/Retro/%s_fac_%s.png' % (unreal.Paths.project_saved_dir().rstrip('/'), STAMP, label))
def run():
    assert les.load_level('/Game/Level/L_GadgetLab'); yield 30
    les.editor_request_begin_play()
    while not les.is_in_play_in_editor(): yield 1
    t0 = time.monotonic()
    while time.monotonic() - t0 < 60:
        w = ues.get_game_world(); pc = unreal.GameplayStatics.get_player_controller(w, 0) if w else None
        if pc and pc.get_controlled_pawn(): break
        yield 10
    pawn = pc.get_controlled_pawn() if pc else None
    cam = pc.player_camera_manager if pc else None
    unreal.log('[RetroFac] waited %.1fs pawn=%s cam=%s view=%s' % (time.monotonic() - t0, pawn.get_name() if pawn else None,
               cam.get_camera_location() if cam else None, pc.get_view_target().get_name() if pc else None))
    yield 60
    a = [x for x in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.RetroRenderActor)][0]
    r = a.get_editor_property('renderer')
    while LIB.get_remaining_shader_jobs() > 0: LIB.finish_shader_compilation(); yield 5
    r.set_crt_overlay(False); r.set_preset(unreal.RetroRenderPreset.OFF); yield 30; shot(w, 'OFF'); yield 30
    r.set_preset(unreal.RetroRenderPreset.PS1); r.set_crt_overlay(True); yield 30; shot(w, 'PS1CRT'); yield 30
    unreal.log('[RetroFac] cam after=%s' % (pc.player_camera_manager.get_camera_location(),))
    les.editor_request_end_play()
def tick(dt):
    if st['busy']: return
    st['busy'] = True
    try:
        if st['w'] > 0: st['w'] -= 1; return
        st['w'] = next(st['g']) or 0
    except StopIteration:
        unreal.unregister_slate_post_tick_callback(st['h']); unreal.log('[RetroFac] COMPLETE'); unreal.SystemLibrary.quit_editor()
    except Exception:
        unreal.log_error('[RetroFac] ' + traceback.format_exc()); unreal.unregister_slate_post_tick_callback(st['h']); unreal.SystemLibrary.quit_editor()
    finally:
        st['busy'] = False
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
st['g'] = run(); st['h'] = unreal.register_slate_post_tick_callback(tick)
