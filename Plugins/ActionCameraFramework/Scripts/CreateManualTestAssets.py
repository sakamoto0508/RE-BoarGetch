"""Creates the ACF manual-verification content (idempotent). Headless:
  run_ue_py.sh C:/UnrealGames/REBoarGetch/Plugins/ActionCameraFramework/Scripts/CreateManualTestAssets.py
Presets in /ActionCameraFramework/Samples (used by the console command ACF.Sample.Use <Name>):
  DA_ACFSample_Follow      Follow (axis damping, lookahead) + Look At + Collision
  DA_ACFSample_ThirdPerson Orbital Follow (Control Rotation = the project's mouse / gamepad look) + Combat Camera + Collision (solver)
  DA_ACFSample_FreeLook    Orbital Follow with FreeLook rings + Collision
  DA_ACFSample_Composer    Orbital Follow + Rotation Composer (dead / soft zone on the ACFLookTarget pillar) + Collision
  DA_ACFSample_LockOn      Lock-On + Combat Camera + Group Framing (player + locked enemy) + Collision (solver)  (ACF.Sample.LockOn)
  DA_ACFSample_Group       Orbital Follow + Rotation Composer (Target Group) + Group Framing + Collision (ACF.Sample.Group)
Phase 4C: DA_ACFSample_Shot_* (Shot Anchor), DA_ACFSample_Finisher* (effects, sequence) for ACF.Sample.Finisher.
Phase 5A: LS_ACFSample_Cut (two hard-cut shots, ends with a cut) and LS_ACFSample_Blend (one dolly shot with 1 s ease in / out)
  for ACF.Sample.Sequencer Play Cut | Blend (cameras are Spawnables inside the sequences).
Map /ActionCameraFramework/Samples/L_ACFCameraTest: open floor, single wall, narrow corridor with a low ceiling,
pillars, a tall pillar tagged ACFLookTarget and six cylinders tagged ACFEnemy (near, far, boss behind the wall, small, behind the pillars, in the corridor). Uses the project's default game mode / pawn (nothing in
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


def collision(outer, radius=15.0, solver=False, check_look_at=True):
    c = unreal.new_object(unreal.ACFCameraNode_Collision, outer=outer)
    c.set_editor_property('probe_radius', radius)
    c.set_editor_property('return_damping_time', 0.35)
    if solver:  # Phase 4B constraint solver: deoccluder + framing recovery
        d = unreal.ACFCameraDeoccluderSettings()
        d.set_editor_property('enabled', True)
        d.set_editor_property('check_look_at_target', check_look_at)
        c.set_editor_property('deoccluder', d)
        r = unreal.ACFCameraFramingRecoverySettings()
        r.set_editor_property('enabled', True)
        c.set_editor_property('framing_recovery', r)
    return c


def combat(outer):
    return unreal.new_object(unreal.ACFCameraNode_Combat, outer=outer)


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
    OUT['ThirdPerson'] = save_preset(p, [orbit(p), combat(p), collision(p, solver=True, check_look_at=False)])

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
    OUT['LockOn'] = save_preset(p, [lock, combat(p), framing, collision(p, solver=True)], blend_in=0.5)

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
    OUT['Group'] = save_preset(p, [orbit(p, 500.0), comp, framing, collision(p, solver=True)])


# --- Phase 4C: finisher sample (ACF.Sample.Finisher) -------------------------------------------------------

def ensure_asset(name, cls):
    path = FOLDER + '/' + name
    existing = unreal.load_asset(path + '.' + name)
    if existing:
        return existing
    f = unreal.DataAssetFactory()
    f.set_editor_property('data_asset_class', cls)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, cls, f)


def shot_anchor(outer, point, offset, freeze, fov, damping_time=0.12, anchor_offset=(0, 0, 80)):
    a = unreal.new_object(unreal.ACFCameraNode_ShotAnchor, outer=outer)
    a.set_editor_property('anchor_point', point)
    a.set_editor_property('anchor_offset', unreal.Vector(*anchor_offset))
    a.set_editor_property('basis', unreal.ACFCameraShotAnchorBasis.LINE_OF_ACTION)
    a.set_editor_property('camera_offset', unreal.Vector(*offset))
    a.set_editor_property('freeze_at_shot_start', freeze)
    a.set_editor_property('damping_time', damping_time)
    a.set_editor_property('field_of_view', fov)
    return a


def envelope(attack, sustain, release):
    e = unreal.ACFCameraEffectEnvelope()
    e.set_editor_property('attack', attack)
    e.set_editor_property('sustain', sustain)
    e.set_editor_property('release', release)
    return e


def effect_preset(name, location, rotation, fov, env):
    p = ensure_asset(name, unreal.ACFCameraEffectPreset)
    layer = unreal.new_object(unreal.ACFCameraEffect_Punch, outer=p)
    layer.set_editor_property('location', unreal.Vector(*location))
    layer.set_editor_property('rotation', unreal.Rotator(roll=rotation[2], pitch=rotation[0], yaw=rotation[1]))
    layer.set_editor_property('fov', fov)
    layer.set_editor_property('envelope', env)
    p.set_editor_property('layers', [layer])
    E.save_loaded_asset(p, False)
    return p


def shot(name, preset, mode, duration, blend_in, fallback=None, deoccluder=False, effects=()):
    s = unreal.ACFCameraShot()
    s.set_editor_property('name', name)
    s.set_editor_property('preset', preset)
    s.set_editor_property('advance_mode', mode)
    s.set_editor_property('duration', duration)
    s.set_editor_property('blend_in', blend_in)
    s.set_editor_property('enable_deoccluder', deoccluder)
    if fallback:
        f = unreal.ACFCameraShotFallback()
        f.set_editor_property('fallback_preset', fallback)
        s.set_editor_property('fallback', f)
    fx = []
    for preset_fx, lifetime in effects:
        e = unreal.ACFCameraShotEffect()
        e.set_editor_property('preset', preset_fx)
        e.set_editor_property('lifetime', lifetime)
        fx.append(e)
    s.set_editor_property('effects', fx)
    return s


def finisher_assets():
    P = unreal.ACFCameraShotAnchorPoint
    # Dynamic: right of the attacker -> target line, slightly behind the middle (both on screen).
    p = ensure_preset('DA_ACFSample_Shot_Approach')
    save_preset(p, [shot_anchor(p, P.BETWEEN, (-150, 600, 50), False, 60.0, anchor_offset=(0, 0, 40)), collision(p, 12.0, solver=True)])
    # Fixed: reverse angle from beyond the target (over its shoulder onto the attacker), captured when the shot starts.
    p_strike = ensure_preset('DA_ACFSample_Shot_Strike')
    a = shot_anchor(p_strike, P.BETWEEN, (380, -150, 50), True, 50.0, anchor_offset=(0, 0, 50))
    a.set_editor_property('between_alpha', 0.8)
    save_preset(p_strike, [a, collision(p_strike, 12.0, solver=True)])
    # Fixed: high wide shot over the action.
    p_over = ensure_preset('DA_ACFSample_Shot_Overhead')
    save_preset(p_over, [shot_anchor(p_over, P.BETWEEN, (-420, 220, 330), True, 70.0, anchor_offset=(0, 0, 40)), collision(p_over, 12.0, solver=True)])
    # Fallback for narrow places: close behind the attacker's shoulder (fits a 270 cm corridor).
    p_fb = ensure_preset('DA_ACFSample_Shot_Fallback')
    save_preset(p_fb, [shot_anchor(p_fb, P.FOLLOW_TARGET, (-230, 60, 60), False, 65.0, anchor_offset=(0, 0, 90)), collision(p_fb, 12.0, solver=True)])

    hit = effect_preset('DA_ACFSample_FinisherHit', (-25, 0, 0), (-1.5, 0, 0), -6.0, envelope(0.04, 0.15, 0.45))
    tension = effect_preset('DA_ACFSample_FinisherTension', (0, 0, 0), (0, 0, 3.0), -3.0, envelope(0.4, -1.0, 0.5))

    L = unreal.ACFCameraShotEffectLifetime
    M = unreal.ACFCameraShotAdvanceMode
    seq = ensure_asset('DA_ACFSample_FinisherSequence', unreal.ACFCameraSequence)
    seq.set_editor_property('shots', [
        shot('Approach', p, M.DURATION, 1.2, blend(0.35), fallback=p_fb, effects=((tension, L.SEQUENCE),)),
        shot('Strike', p_strike, M.WAIT_FOR_EVENT, 2.5, blend(0.0, unreal.ACFCameraBlendCurve.CUT), fallback=p_fb, effects=((hit, L.SHOT),)),
        shot('Overhead', p_over, M.DURATION, 1.6, blend(0.6), fallback=p_fb),
    ])
    seq.set_editor_property('return_blend', blend(0.6))
    E.save_loaded_asset(seq, False)
    OUT['finisher'] = [a.get_path_name() for a in (p, p_strike, p_over, p_fb, hit, tension, seq)]


# --- Phase 5A: sample Level Sequences (ACF.Sample.Sequencer Play Cut / Blend) --------------------------------
# Cameras are Spawnables inside the sequence (no binding to a level actor), placed for L_ACFCameraTest around the start.

def ensure_level_sequence(name):
    path = FOLDER + '/' + name
    seq = unreal.load_asset(path + '.' + name)
    if seq:
        # 既存のアセットを再利用し、中身（Binding と Track）だけ作り直す（冪等）。
        for binding in seq.get_bindings():
            binding.remove()
        for track in seq.get_tracks():
            seq.remove_track(track)
        return seq
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.LevelSequence, unreal.LevelSequenceFactoryNew())


def spawn_camera(seq, label, keys, fps):
    """keys: [(seconds, (x, y, z), (pitch, yaw)), ...] -> spawnable Cine Camera with a transform track."""
    binding = seq.add_spawnable_from_class(unreal.CineCameraActor)
    binding.set_display_name(label)
    track = binding.add_track(unreal.MovieScene3DTransformTrack)
    section = track.add_section()
    section.set_range(0, int(seq.get_playback_end()) + 1)  # 全区間（範囲外では Spawnable が既定の位置に戻ってしまう）
    ch = section.get_all_channels()  # Location X Y Z, Rotation X(roll) Y(pitch) Z(yaw), Scale X Y Z
    for seconds, loc, rot in keys:
        frame = unreal.FrameNumber(int(round(seconds * fps)))
        for i, v in enumerate(loc):
            ch[i].add_key(frame, v)
        ch[3].add_key(frame, 0.0)
        ch[4].add_key(frame, rot[0])
        ch[5].add_key(frame, rot[1])
    for i in (6, 7, 8):
        ch[i].set_default(1.0)
    return binding


def camera_cut(seq, cut_track, binding, start, end, fps, ease_in=0.0, ease_out=0.0):
    section = cut_track.add_section()
    section.set_range(int(start * fps), int(end * fps))
    section.set_camera_binding_id(seq.get_binding_id(binding))
    section.set_completion_mode(unreal.MovieSceneCompletionMode.RESTORE_STATE)  # back to the gameplay view target
    if ease_in > 0 or ease_out > 0:
        # Sequencer Scripting の API（Easing の各項目は Python から直接書けない）。Section は既定で組み込みの
        # Easing 関数を持つので、Camera Cut はそれを PCM の ViewTarget Blend に変換する。
        ticks = seq.get_tick_resolution().numerator / seq.get_tick_resolution().denominator
        if ease_in > 0:
            section.set_ease_in_duration(int(ease_in * ticks))
        if ease_out > 0:
            section.set_ease_out_duration(int(ease_out * ticks))
    return section


def level_sequences():
    fps = 30
    # Cut: two shots with a hard cut between them, then the playback ends with a cut back (ACF Return Blend).
    seq = ensure_level_sequence('LS_ACFSample_Cut')
    seq.set_display_rate(unreal.FrameRate(fps, 1))
    seq.set_playback_start(0)
    seq.set_playback_end(int(3.0 * fps))
    a = spawn_camera(seq, 'Cam_Wide', [(0.0, (900, -900, 500), (-25, 135)), (1.5, (800, -700, 450), (-25, 140))], fps)
    b = spawn_camera(seq, 'Cam_Low', [(1.5, (-500, 450, 80), (8, -40)), (3.0, (-450, 300, 90), (6, -30))], fps)
    cut = seq.add_track(unreal.MovieSceneCameraCutTrack)
    camera_cut(seq, cut, a, 0.0, 1.5, fps)
    camera_cut(seq, cut, b, 1.5, 3.0, fps)
    E.save_loaded_asset(seq, False)
    OUT['LS_ACFSample_Cut'] = seq.get_path_name()

    # Blend: one dolly shot that eases in from and out to the gameplay camera (PCM view target blend, 1 s each).
    seq = ensure_level_sequence('LS_ACFSample_Blend')
    seq.set_display_rate(unreal.FrameRate(fps, 1))
    seq.set_playback_start(0)
    seq.set_playback_end(int(4.0 * fps))
    c = spawn_camera(seq, 'Cam_Dolly', [(0.0, (-700, -600, 350), (-20, 40)), (4.0, (-200, -800, 450), (-25, 75))], fps)
    cut = seq.add_track(unreal.MovieSceneCameraCutTrack)
    # Camera Cut Track の Can Blend（bCanBlend）は Python から設定できない（UPROPERTY() のみ）。UE 5.8 の実行時は Section の
    # Ease In / Out をこのフラグに関係なく使うので再生には影響しない。Sequencer で編集するときは Can Blend を ON にすること。
    camera_cut(seq, cut, c, 0.0, 4.0, fps, ease_in=1.0, ease_out=1.0)
    E.save_loaded_asset(seq, False)
    OUT['LS_ACFSample_Blend'] = seq.get_path_name()


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
                                   ('Enemy_3_Small', (-200, -1400, 40), (0.5, 0.5, 0.8)),
                                   ('Enemy_4_Pillars', (-950, -1200, 90), (0.8, 0.8, 1.8)),
                                   ('Enemy_5_Corridor', (650, 1500, 90), (0.8, 0.8, 1.8)),
                                   ('Enemy_6_Finisher', (1500, -1500, 90), (0.8, 0.8, 1.8))):
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
    finisher_assets()
    level_sequences()
    build_map()
except Exception:
    OUT['error'] = traceback.format_exc()

open(unreal.Paths.project_saved_dir() + 'ACFVerification/CreateManualTestAssets.json', 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, default=str))
unreal.log('[ACF] COMPLETE' if 'error' not in OUT else '[ACF] FAILED')
