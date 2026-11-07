"""Creates the ACF manual-verification content (idempotent). Headless:
  run_ue_py.sh C:/UnrealGames/REBoarGetch/Plugins/ActionCameraFramework/Scripts/CreateManualTestAssets.py
Presets in /ActionCameraFramework/Samples (used by the console command ACF.Sample.Use <Name>):
  DA_ACFSample_Follow      Follow (axis damping, lookahead) + Look At + Collision
  DA_ACFSample_ThirdPerson Orbital Follow (Control Rotation = the project's mouse / gamepad look) + Collision
  DA_ACFSample_FreeLook    Orbital Follow with FreeLook rings + Collision
  DA_ACFSample_Composer    Orbital Follow + Rotation Composer (dead / soft zone on the ACFLookTarget pillar) + Collision
  DA_ACFSample_LockOn      Lock-On + Group Framing (player + locked enemy) + Collision   (ACF.Sample.LockOn)
  DA_ACFSample_Group       Orbital Follow + Rotation Composer (Target Group) + Group Framing + Collision (ACF.Sample.Group)
Map /ActionCameraFramework/Samples/L_ACFCameraTest: open floor, single wall, narrow corridor with a low ceiling,
pillars, a tall pillar tagged ACFLookTarget and four cylinders tagged ACFEnemy (near, far, boss, small). Uses the project's default game mode / pawn (nothing in
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

    # Phase 4A: Lock-On (player = Follow Target, enemy = Look At Target) + framing of both + Collision.
    p = ensure_preset('DA_ACFSample_LockOn')
    lock = unreal.new_object(unreal.ACFCameraNode_LockOn, outer=p)
    lock.set_editor_property('pivot_offset', unreal.Vector(0, 0, 60))
    lock.set_editor_property('adjust_field_of_view', True)
    lock.set_editor_property('near_field_of_view', 90.0)
    lock.set_editor_property('far_field_of_view', 70.0)
    framing = unreal.new_object(unreal.ACFCameraNode_GroupFraming, outer=p)
    framing.set_editor_property('frame_target_group', False)
    framing.set_editor_property('follow_target_subject', subject(60.0))
    framing.set_editor_property('look_at_target_subject', subject(100.0))
    framing.set_editor_property('framing_size', unreal.Vector2D(0.85, 0.85))
    framing.set_editor_property('max_dolly_out', 600.0)
    OUT['LockOn'] = save_preset(p, [lock, framing, collision(p)], blend_in=0.5)

    # Phase 4A: Target Group (exploration orbit + aim at the group centre + keep every member on screen).
    p = ensure_preset('DA_ACFSample_Group')
    comp = unreal.new_object(unreal.ACFCameraNode_RotationComposer, outer=p)
    comp.set_editor_property('target_source', unreal.ACFCameraLookAtSource.TARGET_GROUP)
    c = unreal.ACFCameraScreenComposition()
    c.set_editor_property('screen_position', unreal.Vector2D(0.0, 0.0))
    c.set_editor_property('dead_zone_size', unreal.Vector2D(0.1, 0.1))
    c.set_editor_property('soft_zone_size', unreal.Vector2D(0.6, 0.6))
    comp.set_editor_property('composition', c)
    comp.set_editor_property('damping', unreal.Vector2D(0.3, 0.3))
    framing = unreal.new_object(unreal.ACFCameraNode_GroupFraming, outer=p)
    framing.set_editor_property('framing_size', unreal.Vector2D(0.8, 0.8))
    framing.set_editor_property('max_dolly_out', 1500.0)
    OUT['Group'] = save_preset(p, [orbit(p, 500.0), comp, framing, collision(p)])


def subject(radius):
    s = unreal.ACFCameraFramingSubjectSettings()
    s.set_editor_property('include', True)
    s.set_editor_property('radius', radius)
    return s


def spawn_box(sub, label, location, scale, tag=None):
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    a.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube.Cube'))
    a.set_actor_scale3d(unreal.Vector(*scale))
    a.set_actor_label(label)
    if tag:
        a.tags = [unreal.Name(tag)]
    return a


def spawn_text(sub, text, location, yaw=180.0):
    t = sub.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(*location), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    comp = t.text_render
    comp.set_editor_property('text', unreal.Text(text))
    comp.set_editor_property('world_size', 60.0)
    comp.set_editor_property('horizontal_alignment', unreal.HorizTextAligment.EHTA_CENTER)
    t.set_actor_label('Label_' + text.split(' ')[0])
    return t


def setup_lighting(sub):
    """Same lighting setup as L_Lobby (Movable: the project has r.AllowStaticLighting=False).
    NOTE: Python unreal.Rotator positional order is (roll, pitch, yaw) - always use keywords."""
    for a in sub.get_all_level_actors():
        if a.get_actor_label() in ('Sun', 'SkyLight', 'SkyAtmosphere', 'HeightFog'):
            sub.destroy_actor(a)
    light = sub.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(roll=0.0, pitch=-50.0, yaw=30.0))
    light.set_actor_label('Sun')
    lc = light.get_component_by_class(unreal.DirectionalLightComponent)
    lc.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    lc.set_editor_property('atmosphere_sun_light', True)
    sky = sub.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 800), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    sky.set_actor_label('SkyLight')
    sky.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property('real_time_capture', True)
    atmo = sub.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    atmo.set_actor_label('SkyAtmosphere')
    fog = sub.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    fog.set_actor_label('HeightFog')
    OUT['sun_rotation'] = str(light.get_actor_rotation())


def build_map():
    disk = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_plugins_dir() + 'ActionCameraFramework/Content/Samples/L_ACFCameraTest.umap')
    if os.path.exists(disk):
        os.remove(disk)  # EditorAssetLibrary.delete_asset on maps fails silently in commandlets
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    OUT['new_level'] = level.new_level(MAP)
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    spawn_box(sub, 'Floor', (0, 0, -10), (80, 80, 0.2))
    ps = sub.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 120), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
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

    # 5. Lock-On / Target Group dummies (tag ACFEnemy, Movable so ACF.Sample.Enemy can move them).
    for label, location, scale in (('Enemy_0_Near', (500, -700, 90), (0.8, 0.8, 1.8)),
                                   ('Enemy_1_Far', (2600, -1800, 90), (0.8, 0.8, 1.8)),
                                   ('Enemy_2_Boss', (2200, 400, 300), (3.0, 3.0, 6.0)),
                                   ('Enemy_3_Small', (-200, -1400, 40), (0.5, 0.5, 0.8))):
        e = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
        e.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        e.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cylinder.Cylinder'))
        e.set_actor_scale3d(unreal.Vector(*scale))
        e.set_actor_label(label)
        e.tags = [unreal.Name('ACFEnemy')]
        spawn_text(sub, label.replace('_', ' ', 1), (location[0], location[1], location[2] + scale[2] * 50 + 60), yaw=180.0)

    setup_lighting(sub)
    OUT['saved_map'] = level.save_current_level()
    OUT['actors'] = len(sub.get_all_level_actors())


try:
    presets()
    build_map()
except Exception:
    OUT['error'] = traceback.format_exc()

open(unreal.Paths.project_saved_dir() + 'ACFVerification/CreateManualTestAssets.json', 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, default=str))
unreal.log('[ACF] COMPLETE' if 'error' not in OUT else '[ACF] FAILED')
