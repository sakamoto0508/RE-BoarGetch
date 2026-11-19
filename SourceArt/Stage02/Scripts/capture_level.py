"""Editor-world SceneCapture shots (no PIE, saves nothing).
UnrealEditor.exe <uproject> /Engine/Maps/Entry -ExecutePythonScript=<this> -RenderOffscreen ... -S02Tag=before|after [-S02Nav] [-S02Quit]
Cameras come from cams_<tag>.json: name -> [x, y, z, pitch, yaw, fov]. Writes Screenshots/<tag>/<name>.png."""
import json, time, traceback, unreal
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
args = unreal.SystemLibrary.get_command_line().split()
opt = {t.split('=', 1)[0][4:]: t.split('=', 1)[1] for t in args if t.startswith('-S02') and '=' in t}
LEVEL = opt.get('Level', '/Game/Level/L_Stage02')
TAG = opt.get('Tag', 'before')
OUT = ROOT / 'Screenshots' / TAG
OUT.mkdir(parents=True, exist_ok=True)
CAMS = json.loads((ROOT / 'Scripts' / ('cams_%s.json' % TAG)).read_text())
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state = {'gen': None, 'wait': 0, 'busy': False, 'deadline': time.monotonic() + 1500, 'h': None}


def log(m):
    unreal.log_warning('[S02Capture] ' + m)


def wait_shaders():
    lib = getattr(unreal, 'RetroRenderEditorLibrary', None)
    stable = 0
    while stable < 5:
        if lib and lib.get_remaining_shader_jobs() > 0:
            lib.finish_shader_compilation(); stable = 0
        else:
            stable += 1
        yield 10


def run():
    assert les.load_level(LEVEL), LEVEL
    yield 60
    world = ues.get_editor_world()
    assert world.get_path_name().startswith(LEVEL), world.get_path_name()
    yield from wait_shaders()
    rt = unreal.RenderingLibrary.create_render_target2d(world, 1600, 900, unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 0))
    comp = cap.capture_component2d
    comp.set_editor_property('texture_target', rt)
    comp.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    comp.set_editor_property('capture_every_frame', False)
    if 'Nav' in ' '.join(args):
        comp.set_editor_property('show_flag_settings', [unreal.EngineShowFlagsSetting(show_flag_name='Navigation', enabled=True)])
    for name, c in CAMS.items():
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
    eas.destroy_actor(cap)   # editor-world only; the level is never saved by this script


def finish(err=None):
    unreal.unregister_slate_post_tick_callback(state['h'])
    log('ERROR ' + err if err else 'COMPLETE')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if '-S02Quit' in args:
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
