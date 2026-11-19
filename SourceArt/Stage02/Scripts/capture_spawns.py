"""AKI-175: top-down editor-world shot of the 4 Boar SpawnPoints (no PIE, saves nothing).
UnrealEditor.exe <uproject> /Engine/Maps/Entry -ExecutePythonScript=<this> -RenderOffscreen [-VerificationQuit|-S02Quit]
Temporary marker cylinders + labels are spawned in the editor world only and the level is never saved.
Writes Screenshots/aki175/spawn_top.png (+ spawn_oblique.png)."""
import time, traceback, unreal
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'Screenshots' / 'aki175'
OUT.mkdir(parents=True, exist_ok=True)
LEVEL = '/Game/Level/L_Stage02'
args = unreal.SystemLibrary.get_command_line().split()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state = {'gen': None, 'wait': 0, 'busy': False, 'deadline': time.monotonic() + 1500, 'h': None}
# SpawnPointId -> (label, colour)
STYLE = {'Stage02_Spawn_001': ('N1', (0.1, 0.9, 0.2)), 'Stage02_Spawn_002': ('N2', (0.1, 0.9, 0.9)),
         'Stage02_Spawn_003': ('RED', (1.0, 0.05, 0.05)), 'Stage02_Spawn_004': ('BLUE', (0.1, 0.25, 1.0))}
CAMS = {'spawn_top': (0, 0, 7300, -89.9, 0, 90), 'spawn_oblique': (-7600, -2600, 4600, -30, 22, 75)}


def log(m):
    unreal.log_warning('[S02SpawnCapture] ' + m)


def wait_shaders():
    lib = getattr(unreal, 'RetroRenderEditorLibrary', None)
    stable = 0
    while stable < 5:
        if lib and lib.get_remaining_shader_jobs() > 0:
            lib.finish_shader_compilation(); stable = 0
        else:
            stable += 1
        yield 10


def marker(loc, text, rgb, temp):
    cyl = unreal.load_asset('/Engine/BasicShapes/Cylinder')
    mat = unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial')
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(loc.x, loc.y, loc.z + 400))
    c = a.static_mesh_component
    c.set_static_mesh(cyl)
    a.set_actor_scale3d(unreal.Vector(3.2, 3.2, 9.0))
    mid = c.create_dynamic_material_instance(0, mat)
    mid.set_vector_parameter_value('Color', unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1))
    temp.append(a)
    t = eas.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(loc.x + 330, loc.y, loc.z + 900),
                                   unreal.Rotator(roll=0, pitch=90, yaw=180))
    tc = t.text_render
    tc.set_text(text)
    tc.set_world_size(420)
    tc.set_text_render_color(unreal.Color(r=int(rgb[0] * 255), g=int(rgb[1] * 255), b=int(rgb[2] * 255), a=255))
    tc.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    temp.append(t)


def run():
    assert les.load_level(LEVEL), LEVEL
    yield 60
    world = ues.get_editor_world()
    assert world.get_path_name().startswith(LEVEL), world.get_path_name()
    temp = []
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.BoarSpawnPoint):
            pid = str(a.get_editor_property('spawn_point_id'))
            if pid in STYLE:
                marker(a.get_actor_location(), STYLE[pid][0], STYLE[pid][1], temp)
        elif isinstance(a, unreal.Cage):
            marker(a.get_actor_location(), 'CAGE', (1.0, 0.85, 0.0), temp)
        elif isinstance(a, unreal.PlayerStart):
            marker(a.get_actor_location(), 'START', (1.0, 1.0, 1.0), temp)
    log('markers %d' % len(temp))
    yield 30
    yield from wait_shaders()
    rt = unreal.RenderingLibrary.create_render_target2d(world, 1600, 1100, unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 0))
    comp = cap.capture_component2d
    comp.set_editor_property('texture_target', rt)
    comp.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    comp.set_editor_property('capture_every_frame', False)
    for name, (x, y, z, p, yw, fov) in CAMS.items():
        cap.set_actor_location_and_rotation(unreal.Vector(x, y, z), unreal.Rotator(roll=0, pitch=p, yaw=yw), False, False)
        comp.set_editor_property('fov_angle', fov)
        for _ in range(3):
            comp.capture_scene(); yield 10
        yield from wait_shaders()
        comp.capture_scene(); yield 5
        unreal.RenderingLibrary.export_render_target(world, rt, str(OUT), name + '.png')
        log('shot ' + name)
    for a in temp + [cap]:
        eas.destroy_actor(a)          # editor world only; never saved


def finish(err=None):
    unreal.unregister_slate_post_tick_callback(state['h'])
    log('ERROR ' + err if err else 'COMPLETE')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if '-S02Quit' in args or '-VerificationQuit' in args:
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
