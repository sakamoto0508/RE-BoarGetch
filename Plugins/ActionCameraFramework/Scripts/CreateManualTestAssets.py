"""Creates the ACF manual-verification content (idempotent). Headless:
  run_ue_py.sh C:/UnrealGames/REBoarGetch/Plugins/ActionCameraFramework/Scripts/CreateManualTestAssets.py
Presets in /ActionCameraFramework/Samples (used by the console command ACF.Sample.Use <Name>):
  DA_ACFSample_Follow      Follow (axis damping, lookahead) + Look At + Collision
  DA_ACFSample_ThirdPerson Orbital Follow (Control Rotation = the project's mouse / gamepad look) + Collision
  DA_ACFSample_FreeLook    Orbital Follow with FreeLook rings + Collision
  DA_ACFSample_Composer    Orbital Follow + Rotation Composer (dead / soft zone on the ACFLookTarget pillar) + Collision
Map /ActionCameraFramework/Samples/L_ACFCameraTest: open floor, single wall, narrow corridor with a low ceiling,
pillars, and a tall pillar tagged ACFLookTarget. Uses the project's default game mode / pawn (nothing in
REBoarGetch is changed). Writes Saved/ACFVerification/CreateManualTestAssets.json."""
import unreal, json, os, traceback

FOLDER = '/ActionCameraFramework/Samples'
MAP = FOLDER + '/L_ACFCameraTest'
E = unreal.EditorAssetLibrary
OUT = {}


def ensure_preset(name):
    path = FOLDER + '/' + name
    existing = unreal.load_asset(path + '.' + name)
    if existing:
        return existing
    f = unreal.DataAssetFactory()
    f.set_editor_property('data_asset_class', unreal.ACFCameraPreset)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.ACFCameraPreset, f)


def blend(seconds, curve=unreal.ACFCameraBlendCurve.EASE_IN_OUT):
    b = unreal.ACFCameraBlendSettings()
    b.set_editor_property('curve', curve)
    b.set_editor_property('duration', seconds)
    b.set_editor_property('source', unreal.ACFCameraBlendSource.LIVE)
    return b


def damping(x, y, z, space):
    d = unreal.ACFCameraPositionDamping()
    d.set_editor_property('time', unreal.Vector(x, y, z))
    d.set_editor_property('space', space)
    return d


def motion(lookahead_time=0.0):
    m = unreal.ACFCameraTargetMotionSettings()
    la = unreal.ACFCameraLookaheadSettings()
    la.set_editor_property('time', lookahead_time)
    la.set_editor_property('smoothing', 0.3)
    la.set_editor_property('max_distance', 200.0)
    m.set_editor_property('lookahead', la)
    return m


def collision(outer, radius=15.0):
    c = unreal.new_object(unreal.ACFCameraNode_Collision, outer=outer)
    c.set_editor_property('probe_radius', radius)
    c.set_editor_property('return_damping_time', 0.35)
    return c


def orbit(outer, distance=350.0, rings=False):
    o = unreal.new_object(unreal.ACFCameraNode_OrbitalFollow, outer=outer)
    o.set_editor_property('input_source', unreal.ACFCameraOrbitInputSource.CONTROL_ROTATION)
    o.set_editor_property('initial_rotation', unreal.ACFCameraOrbitInitialRotation.CONTROL_ROTATION)
    o.set_editor_property('pivot_offset', unreal.Vector(0, 0, 60))
    o.set_editor_property('distance', distance)
    o.set_editor_property('shoulder_offset', unreal.Vector(0, 50, 30))
    o.set_editor_property('pivot_damping', damping(0.08, 0.08, 0.15, unreal.ACFCameraDampingSpace.CAMERA_YAW))
    o.set_editor_property('pitch_min', -70.0)
    o.set_editor_property('pitch_max', 50.0)
    o.set_editor_property('target_motion', motion(0.0))
    if rings:
        o.set_editor_property('use_rings', True)
        o.set_editor_property('top_distance', 650.0)
        o.set_editor_property('bottom_distance', 200.0)
        o.set_editor_property('shoulder_offset', unreal.Vector(0, 0, 0))
    return o


def save_preset(preset, nodes, blend_in=0.6):
    preset.set_editor_property('nodes', nodes)
    preset.set_editor_property('default_blend_in', blend(blend_in))
    preset.set_editor_property('default_blend_out', blend(0.6))
    return E.save_loaded_asset(preset, False)


def presets():
    p = ensure_preset('DA_ACFSample_Follow')
    follow = unreal.new_object(unreal.ACFCameraNode_Follow, outer=p)
    follow.set_editor_property('offset', unreal.Vector(-450, 0, 220))
    follow.set_editor_property('offset_space', unreal.ACFCameraOffsetSpace.TARGET_LOCAL)
    follow.set_editor_property('damping', damping(0.35, 0.25, 0.15, unreal.ACFCameraDampingSpace.TARGET_LOCAL))
    follow.set_editor_property('target_motion', motion(0.25))
    look = unreal.new_object(unreal.ACFCameraNode_LookAt, outer=p)
    look.set_editor_property('target_source', unreal.ACFCameraLookAtSource.FOLLOW_TARGET)
    look.set_editor_property('target_offset', unreal.Vector(0, 0, 60))
    look.set_editor_property('damping_time', 0.1)
    OUT['Follow'] = save_preset(p, [follow, look, collision(p)])

    p = ensure_preset('DA_ACFSample_ThirdPerson')
    OUT['ThirdPerson'] = save_preset(p, [orbit(p), collision(p)])

    p = ensure_preset('DA_ACFSample_FreeLook')
    OUT['FreeLook'] = save_preset(p, [orbit(p, 420.0, rings=True), collision(p)])

    p = ensure_preset('DA_ACFSample_Composer')
    comp = unreal.new_object(unreal.ACFCameraNode_RotationComposer, outer=p)
    comp.set_editor_property('target_source', unreal.ACFCameraLookAtSource.LOOK_AT_TARGET)
    comp.set_editor_property('target_offset', unreal.Vector(0, 0, 150))
    c = unreal.ACFCameraScreenComposition()
    c.set_editor_property('screen_position', unreal.Vector2D(0.15, 0.05))
    c.set_editor_property('dead_zone_size', unreal.Vector2D(0.12, 0.12))
    c.set_editor_property('soft_zone_size', unreal.Vector2D(0.5, 0.5))
    comp.set_editor_property('composition', c)
    comp.set_editor_property('damping', unreal.Vector2D(0.4, 0.4))
    OUT['Composer'] = save_preset(p, [orbit(p, 450.0), comp, collision(p)])


def spawn_box(sub, label, location, scale, tag=None):
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location), unreal.Rotator(0, 0, 0))
    a.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube.Cube'))
    a.set_actor_scale3d(unreal.Vector(*scale))
    a.set_actor_label(label)
    if tag:
        a.tags = [unreal.Name(tag)]
    return a


def spawn_text(sub, text, location, yaw=180.0):
    t = sub.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(*location), unreal.Rotator(0, 0, yaw))
    comp = t.text_render
    comp.set_editor_property('text', unreal.Text(text))
    comp.set_editor_property('world_size', 60.0)
    comp.set_editor_property('horizontal_alignment', unreal.HorizTextAligment.EHTA_CENTER)
    t.set_actor_label('Label_' + text.split(' ')[0])
    return t


def build_map():
    disk = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_plugins_dir() + 'ActionCameraFramework/Content/Samples/L_ACFCameraTest.umap')
    if os.path.exists(disk):
        os.remove(disk)  # EditorAssetLibrary.delete_asset on maps fails silently in commandlets
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    OUT['new_level'] = level.new_level(MAP)
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    spawn_box(sub, 'Floor', (0, 0, -10), (80, 80, 0.2))
    ps = sub.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 120), unreal.Rotator(0, 0, 0))
    ps.set_actor_label('PlayerStart')
    # 1. Single wall: walk to it and turn your back to it (or orbit the camera into it).
    spawn_box(sub, 'Wall_Single', (700, 0, 150), (0.2, 8, 3))
    spawn_text(sub, 'Wall', (690, 0, 340))
    # 2. Narrow corridor (270 cm between the inner faces, 1600 cm long) with a low ceiling over its far half.
    spawn_box(sub, 'Corridor_L', (0, 1500 - 135 - 10, 150), (16, 0.2, 3))
    spawn_box(sub, 'Corridor_R', (0, 1500 + 135 + 10, 150), (16, 0.2, 3))
    spawn_box(sub, 'Corridor_Ceiling', (400, 1500, 255), (8, 2.9, 0.1))
    spawn_text(sub, 'Corridor', (-850, 1500, 300), yaw=0.0)
    # 3. Pillars: blends / orbiting around thin obstacles.
    for i, y in enumerate((-1600, -1200, -800)):
        spawn_box(sub, 'Pillar_%d' % i, (-600, y, 200), (0.6, 0.6, 4))
    spawn_text(sub, 'Pillars', (-600, -1200, 460), yaw=0.0)
    # 4. Look-at target for the Rotation Composer sample.
    spawn_box(sub, 'LookTarget', (-1800, 0, 300), (1, 1, 6), tag='ACFLookTarget')
    spawn_text(sub, 'LookTarget (Composer)', (-1800, 0, 660), yaw=0.0)

    light = sub.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(-50, 30, 0))
    light.set_actor_label('Sun')
    sky = sub.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 800), unreal.Rotator(0, 0, 0))
    sky.light_component.set_editor_property('real_time_capture', True)
    sub.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    OUT['saved_map'] = level.save_current_level()
    OUT['actors'] = len(sub.get_all_level_actors())


try:
    presets()
    build_map()
except Exception:
    OUT['error'] = traceback.format_exc()

open(unreal.Paths.project_saved_dir() + 'ACFVerification/CreateManualTestAssets.json', 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, default=str))
unreal.log('[ACF] COMPLETE' if 'error' not in OUT else '[ACF] FAILED')
