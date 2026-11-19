"""Captures the Stage 1 and Stage 2 dioramas (BoarStagePreviewActor parts) as transparent PNGs, GUI editor, real GPU, no PIE:
  sh Tools/Verification/run_ue_gui.sh C:/UnrealGames/REBoarGetch/SourceArt/Stage02/Diorama/capture_dioramas.py
Same placement / camera as ABoarStagePreviewActor (orthographic, SceneColorHDR, show-only), 2x resolution.
Writes SourceArt/Stage02/Diorama/Screenshots/Diorama_Stage0{1,2}.png (git-ignored). Opens L_Lobby, saves nothing."""
import unreal, json, math, struct, time, traceback, zlib
from pathlib import Path

ROOT = Path('C:/UnrealGames/REBoarGetch')
OUT_DIR = ROOT / 'SourceArt/Stage02/Diorama/Screenshots'
DIORAMAS = {'Stage01': '/Game/UI/StagePreview/BP_Stage01_Diorama', 'Stage02': '/Game/UI/StagePreview/BP_Stage02_Diorama'}
W, H = 2048, 1536
ORIGIN = unreal.Vector(0, 0, -100000)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
R = {}
S = {'gen': None, 'wait': 0, 'h': None, 'busy': False, 'deadline': time.monotonic() + 900}


def write_png(path, w, h, rows):
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b''.join(b'\x00' + r for r in rows)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) \
        + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def srgb(v):
    v = max(0.0, min(1.0, v))
    return 12.92 * v if v <= 0.0031308 else 1.055 * (v ** (1 / 2.4)) - 0.055


def run():
    assert les.load_level('/Game/Level/L_Lobby')
    yield 30
    for key, path in DIORAMAS.items():
        yield from capture(key, path)


def capture(key, DIORAMA):
    OUT_PNG = OUT_DIR / ('Diorama_%s.png' % key)
    R[key] = {}
    world = ues.get_editor_world()
    cls = unreal.load_object(None, DIORAMA + '.' + DIORAMA.rsplit('/', 1)[1] + '_C')
    cdo = unreal.get_default_object(cls)
    parts = cdo.get_editor_property('parts')
    ortho = cdo.get_editor_property('ortho_width')
    R[key]['parts'] = len(parts)
    R[key]['ortho_width'] = ortho
    actors = []
    for p in parts:
        mesh = p.get_editor_property('mesh')
        if not mesh:
            continue
        t = p.get_editor_property('transform')
        a = eas.spawn_actor_from_class(unreal.StaticMeshActor, ORIGIN + t.translation, t.rotation.rotator())
        a.set_actor_scale3d(t.scale3d)
        c = a.static_mesh_component
        c.set_static_mesh(mesh)
        mat = p.get_editor_property('material')
        if mat:
            c.set_material(0, mat)
        c.set_cast_shadow(False)
        actors.append(a)
    loc = ORIGIN + unreal.Vector(-1100, -1100, 1100)
    d = unreal.Vector(0, 0, 80) - unreal.Vector(-1100, -1100, 1100)
    rot = unreal.Rotator(roll=0, pitch=math.degrees(math.atan2(d.z, math.hypot(d.x, d.y))), yaw=math.degrees(math.atan2(d.y, d.x)))
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, loc, rot)
    cc = cap.capture_component2d
    rt = unreal.RenderingLibrary.create_render_target2d(world, W, H, unreal.TextureRenderTargetFormat.RTF_RGBA16F,
                                                        unreal.LinearColor(0, 0, 0, 1), False)
    cc.set_editor_property('projection_type', unreal.CameraProjectionMode.ORTHOGRAPHIC)
    cc.set_editor_property('ortho_width', ortho)
    cc.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_SCENE_COLOR_HDR)
    cc.set_editor_property('primitive_render_mode', unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
    for a in actors:
        cc.show_only_actor_components(a)
    cc.set_editor_property('capture_every_frame', False)
    cc.set_editor_property('show_flag_settings', [unreal.EngineShowFlagsSetting(show_flag_name=n, enabled=False)
                                                  for n in ('Atmosphere', 'Fog', 'VolumetricFog', 'Bloom')])
    cc.set_editor_property('texture_target', rt)
    for a in actors:
        a.static_mesh_component.prestream_textures(10.0, False)
    # Shaders / textures may still be compiling or streaming: capture repeatedly for a while.
    for _ in range(60):
        cc.capture_scene()
        yield 5
    cc.capture_scene()
    yield 10
    px = unreal.RenderingLibrary.read_render_target_raw(world, rt, False)
    R[key]['pixels'] = len(px)
    rows = []
    covered = 0
    bb = [W, H, -1, -1]
    for y in range(H):
        row = bytearray(W * 4)
        for x in range(W):
            c = px[y * W + x]
            a = 1.0 - max(0.0, min(1.0, c.a))
            if a > 0.004:
                covered += 1
                bb = [min(bb[0], x), min(bb[1], y), max(bb[2], x), max(bb[3], y)]
                # SceneColor is premultiplied by coverage at soft edges; un-premultiply before encoding.
                k = 1.0 / a
                row[x * 4:x * 4 + 4] = bytes((int(srgb(c.r * k) * 255 + .5), int(srgb(c.g * k) * 255 + .5),
                                              int(srgb(c.b * k) * 255 + .5), int(a * 255 + .5)))
        rows.append(bytes(row))
    R[key]['coverage'] = round(covered / (W * H), 3)
    R[key]['bbox_px'] = bb      # occupied rectangle in the 2048x1536 capture (x0, y0, x1, y1)
    write_png(OUT_PNG, W, H, rows)
    R[key]['png'] = str(OUT_PNG)
    for a in actors + [cap]:
        eas.destroy_actor(a)


def finish(error=None):
    if S['h'] is not None:
        unreal.unregister_slate_post_tick_callback(S['h'])
        S['h'] = None
    R['error'] = error
    (ROOT / 'Saved/Verification').mkdir(parents=True, exist_ok=True)
    (ROOT / 'Saved/Verification/capture_dioramas.json').write_text(json.dumps(R, ensure_ascii=False, indent=1, default=str), encoding='utf-8')
    unreal.log('[capture_dioramas] %s' % ('ERROR ' + error if error else 'COMPLETE'))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if '-RetroQuit' in unreal.SystemLibrary.get_command_line():
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    if S['busy']:
        return
    S['busy'] = True
    try:
        if time.monotonic() > S['deadline']:
            raise TimeoutError('timeout')
        if S['wait'] > 0:
            S['wait'] -= 1
            return
        S['wait'] = next(S['gen']) or 0
    except StopIteration:
        finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        S['busy'] = False


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
S['gen'] = run()
S['h'] = unreal.register_slate_post_tick_callback(tick)
