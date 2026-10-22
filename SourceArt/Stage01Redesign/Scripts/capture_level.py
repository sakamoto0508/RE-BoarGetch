"""Editor-world SceneCapture shots (no PIE, saves nothing).
UnrealEditor.exe <uproject> -ExecutePythonScript=<this> -RenderOffscreen ... -S01Level=/Game/Level/Stage1 -S01Tag=before -S01Quit
Cameras: name -> (loc, rot pitch/yaw, fov). Writes PNGs to SourceArt/Stage01Redesign/Screenshots/<tag>/."""
import time, traceback, unreal
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
args = unreal.SystemLibrary.get_command_line().split()
opt = {t.split('=', 1)[0][4:]: t.split('=', 1)[1] for t in args if t.startswith('-S01') and '=' in t}
LEVEL = opt.get('Level', '/Game/Level/Stage1')
TAG = opt.get('Tag', 'before')
CAMS = opt.get('Cams', 'before')
OUT = ROOT / 'Screenshots' / TAG
OUT.mkdir(parents=True, exist_ok=True)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SETS = {
    # (x, y, z, pitch, yaw, fov)
    'before': {
        'overview': (0, -1400, 7500, -89.9, -90, 70),
        'overview_oblique': (0, 3800, 4200, -42, -90, 70),
        'start_view': (0, 380, 300, -14, -90, 80),
        'from_cage': (120, -1100, 420, -18, 180, 80),
        'water': (-1700, -1000, 420, -20, -150, 80),
        'plateau': (1300, -600, 500, -15, -40, 80),
        'windmill_bg': (-500, -1800, 350, -5, -150, 80),
        'research_bg': (600, -1000, 300, -4, -80, 80),
    },
}
state = {'gen': None, 'wait': 0, 'busy': False, 'deadline': time.monotonic() + 1200, 'h': None}


def log(m):
    unreal.log('[S01Capture] ' + m)


def wait_shaders():
    lib = getattr(unreal, 'RetroRenderEditorLibrary', None)
    stable = 0
    while stable < 5:
        if lib and lib.get_remaining_shader_jobs() > 0:
            lib.finish_shader_compilation(); stable = 0
        else:
            stable += 1
        yield 10


def cams():
    if CAMS in SETS:
        return SETS[CAMS]
    import json
    return json.loads((ROOT / 'Scripts' / ('cams_%s.json' % CAMS)).read_text())


def run():
    assert les.load_level(LEVEL), LEVEL
    yield 60
    world = ues.get_editor_world()
    yield from wait_shaders()
    rt = unreal.RenderingLibrary.create_render_target2d(world, 1600, 900, unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 0))
    comp = cap.capture_component2d
    comp.set_editor_property('texture_target', rt)
    comp.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    comp.set_editor_property('capture_every_frame', False)
    if 'Nav' in opt:  # navmesh overlay for diagnostics
        comp.set_editor_property('show_flag_settings', [unreal.EngineShowFlagsSetting(show_flag_name='Navigation', enabled=True)])
    for name, c in cams().items():
        x, y, z, p, yw, fov = c
        cap.set_actor_location_and_rotation(unreal.Vector(x, y, z), unreal.Rotator(roll=0, pitch=p, yaw=yw), False, False)
        comp.set_editor_property('fov_angle', fov)
        for _ in range(3):
            comp.capture_scene(); yield 10
        yield from wait_shaders()
        comp.capture_scene(); yield 5
        unreal.RenderingLibrary.export_render_target(world, rt, str(OUT), name + '.png')
        log('shot ' + name)
        yield 2
    eas.destroy_actor(cap)


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
            state['wait'] -= 1; return
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
