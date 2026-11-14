"""Phase 7C-3A: the Camera Zone sample in L_ACFCameraTest (README 32 章 "サンプル"). Idempotent; only adds / replaces its own
content, the rest of the map and the other sample presets are left as they are. Headless:
  Scripts/RunEditorPython.sh Scripts/CreateZoneSampleAssets.py   (from the plugin folder, editor closed)

Presets in /ActionCameraFramework/Samples:
  DA_ACFSample_Zone_Plaza     crane camera above the plaza (Follow in world axes + Look At), 1.0 s blends
  DA_ACFSample_Zone_Corridor  side view of the corridor along the south wall (Follow in world axes + Look At), 0.8 s blends
  DA_ACFSample_Zone_Alcove    close orbit (no input, starts from the camera on screen) + Collision, for the north-west corner
Map /ActionCameraFramework/Samples/L_ACFCameraTest, zone area in the north-west corner of the floor (x -3900..-2100,
y 2000..3800), away from the other sample areas, the player start and the test areas:
  Zone_Plaza    box (-3000, 2900) extent (900, 900, 400), priority 0, Exit Delay 0.5 s
  Zone_Corridor box (-3000, 2400) extent (800, 150, 300), priority 1 (inside the plaza: wins), Enter 0.2 s / Exit 0.3 s
  Zone_Alcove   box (-3450, 3400) extent (350, 350, 300), priority 2 (inside the plaza: wins), Enter 0.5 s, Enter Rotation
                Keep View (the close orbit starts from the plaza view)
  geometry: the corridor's north wall (open at its east end: x -3800..-2400), the alcove's two walls, labels; Enemy_7_Zone (tag ACFEnemy, sorted after the others).
Actors carry the tag ACFZoneSample (removed and created again on each run); the zones also ACFSampleZone and
ACFSampleZone_<Name> (ACF.Sample.Zone finds them by tag). Writes Saved/ACFVerification/CreateZoneSampleAssets.json."""
import unreal, json, traceback

FOLDER = '/ActionCameraFramework/Samples'
MAP = FOLDER + '/L_ACFCameraTest'
E = unreal.EditorAssetLibrary
OUT = {}
TAG = 'ACFZoneSample'


def ensure_preset(name):
    path = FOLDER + '/' + name
    existing = unreal.load_asset(path + '.' + name)
    if existing:
        return existing
    f = unreal.DataAssetFactory()
    f.set_editor_property('data_asset_class', unreal.ACFCameraPreset)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.ACFCameraPreset, f)


def blend(seconds):
    b = unreal.ACFCameraBlendSettings()
    b.set_editor_property('curve', unreal.ACFCameraBlendCurve.EASE_IN_OUT)
    b.set_editor_property('duration', seconds)
    b.set_editor_property('source', unreal.ACFCameraBlendSource.LIVE)
    return b


def damping(t):
    d = unreal.ACFCameraPositionDamping()
    d.set_editor_property('time', unreal.Vector(t, t, t))
    d.set_editor_property('space', unreal.ACFCameraDampingSpace.WORLD)
    return d


def crane(preset, offset, blend_seconds):
    follow = unreal.new_object(unreal.ACFCameraNode_Follow, outer=preset)
    follow.set_editor_property('offset', unreal.Vector(*offset))
    follow.set_editor_property('offset_space', unreal.ACFCameraOffsetSpace.WORLD)
    follow.set_editor_property('damping', damping(0.25))
    look = unreal.new_object(unreal.ACFCameraNode_LookAt, outer=preset)
    look.set_editor_property('target_source', unreal.ACFCameraLookAtSource.FOLLOW_TARGET)
    look.set_editor_property('target_offset', unreal.Vector(0, 0, 60))
    look.set_editor_property('damping_time', 0.1)
    preset.set_editor_property('nodes', [follow, look])
    preset.set_editor_property('default_blend_in', blend(blend_seconds))
    preset.set_editor_property('default_blend_out', blend(blend_seconds))
    return E.save_loaded_asset(preset, False)


def presets():
    plaza = ensure_preset('DA_ACFSample_Zone_Plaza')
    OUT['Zone_Plaza'] = crane(plaza, (700, -800, 900), 1.0)
    corridor = ensure_preset('DA_ACFSample_Zone_Corridor')
    OUT['Zone_Corridor'] = crane(corridor, (0, -650, 180), 0.8)
    alcove = ensure_preset('DA_ACFSample_Zone_Alcove')
    o = unreal.new_object(unreal.ACFCameraNode_OrbitalFollow, outer=alcove)
    o.set_editor_property('input_source', unreal.ACFCameraOrbitInputSource.NONE)
    o.set_editor_property('initial_rotation', unreal.ACFCameraOrbitInitialRotation.UPSTREAM_CAMERA)
    o.set_editor_property('pivot_offset', unreal.Vector(0, 0, 60))
    o.set_editor_property('distance', 260.0)
    o.set_editor_property('pitch_min', -60.0)
    o.set_editor_property('pitch_max', 40.0)
    c = unreal.new_object(unreal.ACFCameraNode_Collision, outer=alcove)
    c.set_editor_property('probe_radius', 15.0)
    alcove.set_editor_property('nodes', [o, c])
    alcove.set_editor_property('default_blend_in', blend(0.6))
    alcove.set_editor_property('default_blend_out', blend(0.6))
    OUT['Zone_Alcove'] = E.save_loaded_asset(alcove, False)
    return {'Plaza': plaza, 'Corridor': corridor, 'Alcove': alcove}


def tagged(actor, *tags):
    actor.tags = [unreal.Name(TAG)] + [unreal.Name(t) for t in tags]
    return actor


def spawn_box(sub, label, location, scale):
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    a.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube.Cube'))
    a.set_actor_scale3d(unreal.Vector(*scale))
    a.set_actor_label(label)
    return tagged(a)


def spawn_text(sub, label, text, location, yaw=0.0):
    t = sub.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(*location), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    comp = t.text_render
    comp.set_editor_property('text', unreal.Text(text))
    comp.set_editor_property('world_size', 60.0)
    comp.set_editor_property('horizontal_alignment', unreal.HorizTextAligment.EHTA_CENTER)
    t.set_actor_label(label)
    return tagged(t)


def spawn_zone(sub, name, center, extent, priority, preset, enter_delay, exit_delay, keep_view=False):
    a = sub.spawn_actor_from_class(unreal.ACFCameraZone, unreal.Vector(*center), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    a.set_actor_label('Zone_' + name)
    tagged(a, 'ACFSampleZone', 'ACFSampleZone_' + name)
    a.get_component_by_class(unreal.BoxComponent).set_box_extent(unreal.Vector(*extent))
    z = a.get_component_by_class(unreal.ACFCameraZoneComponent)
    z.set_editor_property('zone_priority', priority)
    z.set_editor_property('preset', preset)
    z.set_editor_property('enter_delay', enter_delay)
    z.set_editor_property('exit_delay', exit_delay)
    z.set_editor_property('exit_margin', 50.0)
    if keep_view:
        z.set_editor_property('enter_rotation', unreal.ACFCameraZoneRotationHandoff.KEEP_VIEW)
    return a


def build_area(p):
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    OUT['load_level'] = level.load_level(MAP)
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    removed = 0
    for a in sub.get_all_level_actors():
        if unreal.Name(TAG) in a.tags:
            sub.destroy_actor(a)
            removed += 1
    OUT['removed'] = removed

    spawn_zone(sub, 'Plaza', (-3000, 2900, 300), (900, 900, 400), 0, p['Plaza'], 0.0, 0.5)
    spawn_zone(sub, 'Corridor', (-3000, 2400, 200), (800, 150, 300), 1, p['Corridor'], 0.2, 0.3)
    spawn_zone(sub, 'Alcove', (-3450, 3400, 200), (350, 350, 300), 2, p['Alcove'], 0.5, 0.0, keep_view=True)
    # Geometry: the corridor's north wall (the side camera looks at it), the alcove corner.
    spawn_box(sub, 'ZoneSample_CorridorWall', (-3100, 2570, 150), (14, 0.2, 3))  # open at the east end (x > -2400)
    spawn_box(sub, 'ZoneSample_AlcoveWall_W', (-3820, 3400, 150), (0.2, 7, 3))
    spawn_box(sub, 'ZoneSample_AlcoveWall_N', (-3450, 3770, 150), (7.6, 0.2, 3))
    spawn_text(sub, 'Label_ZonePlaza', 'Zone Plaza (priority 0)', (-2150, 2900, 420), yaw=180.0)
    spawn_text(sub, 'Label_ZoneCorridor', 'Zone Corridor (priority 1)', (-2250, 2400, 320), yaw=180.0)
    spawn_text(sub, 'Label_ZoneAlcove', 'Zone Alcove (priority 2)', (-3450, 3740, 360), yaw=270.0)
    e = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(-2600, 3000, 90), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    e.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    e.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cylinder.Cylinder'))
    e.set_actor_scale3d(unreal.Vector(0.8, 0.8, 1.8))
    e.set_actor_label('Enemy_7_Zone')
    e.tags = [unreal.Name('ACFEnemy'), unreal.Name(TAG)]
    spawn_text(sub, 'Label_Enemy_7', 'Enemy 7', (-2600, 3000, 240), yaw=180.0)

    OUT['saved_map'] = level.save_current_level()
    OUT['zones'] = [a.get_actor_label() for a in sub.get_all_level_actors() if unreal.Name('ACFSampleZone') in a.tags]
    OUT['actors'] = len(sub.get_all_level_actors())


try:
    build_area(presets())
except Exception:
    OUT['error'] = traceback.format_exc()

open(unreal.Paths.project_saved_dir() + 'ACFVerification/CreateZoneSampleAssets.json', 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, default=str))
unreal.log('[ACF] COMPLETE' if 'error' not in OUT else '[ACF] FAILED')
