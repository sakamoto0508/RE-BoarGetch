"""In-place patch of the stage level (keeps every manual edit): swap pack meshes that lack simple collision for the
Reused copies with collision (same transform), make sure those components block, rebuild navigation, save.
Full editor, ticked. Usage: ... -ExecutePythonScript=<this> [-S01Level=/Game/Level/L_Stage01] -S01Quit"""
import os, sys, time, traceback, unreal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s01r_dressing as D

args = unreal.SystemLibrary.get_command_line()
LEVEL = next((t.split('=', 1)[1] for t in args.split() if t.startswith('-S01Level=')), '/Game/Level/L_Stage01')
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state = {'gen': None, 'wait': 0, 'busy': False, 'deadline': time.monotonic() + 1500, 'h': None}


def log(m):
    unreal.log('[S01Patch] ' + m)


def run():
    assert les.load_level(LEVEL)
    yield 60
    world = ues.get_editor_world()
    assert world.get_path_name().split('.')[0] == LEVEL, world.get_path_name()
    copies = {k: unreal.load_asset(v) for k, v in D.REUSED.items()}
    swapped = {}
    for a in eas.get_all_level_actors():
        if not isinstance(a, unreal.StaticMeshActor):
            continue
        c = a.static_mesh_component
        m = c.static_mesh
        if not m:
            continue
        src = m.get_path_name().split('.')[0]
        if src in copies and 'NO_COLLISION' not in str(c.get_collision_enabled()):
            c.set_static_mesh(copies[src])
            swapped[src.split('/')[-1]] = swapped.get(src.split('/')[-1], 0) + 1
    log('swapped %s' % swapped)
    yield 20
    probes = [(D.POND[0], D.POND[1], -50), (D.BRIDGE[0], D.BRIDGE[1], 40), (D.HILL[0] - 200, D.HILL[1] + 200, 240)]
    ok = False
    for attempt in range(12):
        yield 200
        unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
        yield 20
        t0 = time.monotonic()
        while unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world) and time.monotonic() - t0 < 240:
            yield 10
        hits = []
        for x, y, z in probes:
            p = unreal.NavigationSystemV1.project_point_to_navigation(world, unreal.Vector(x, y, z + 40), None, None, unreal.Vector(40, 40, 80))
            hits.append(p is not None and abs(p.z - z) < 30 and (abs(p.x) + abs(p.y)) > 0)
        log('nav attempt %d probes=%s' % (attempt, hits))
        if all(hits):
            ok = True
            break
    log('navigation rebuilt=%s' % ok)
    assert ok
    les.save_current_level()
    log('saved ' + LEVEL)


def finish(err=None):
    unreal.unregister_slate_post_tick_callback(state['h'])
    log('ERROR ' + err if err else 'COMPLETE')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if '-S01Quit' in args:
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
        try:
            state['wait'] = next(state['gen']) or 0
        except StopIteration:
            finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        state['busy'] = False


state['gen'] = run()
state['h'] = unreal.register_slate_post_tick_callback(tick)
