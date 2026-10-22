"""Build /Game/Level/L_Stage01 from a duplicate of /Game/Level/Stage1 (full editor, ticked).
UnrealEditor.exe <uproject> -ExecutePythonScript=<this> -RenderOffscreen ... [-S01Quit]
Stage1 itself is never modified or saved. Keeps the lighting, PlayerStart, BP_Cage, BoarSpawnPoints, NavMesh actors of the copy;
removes the old blockout/art, places s01r_dressing.placements(), invisible boundary walls, rebuilds navigation, saves the copy."""
import math, os, sys, time, traceback, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import importlib, s01r_layout, s01r_dressing
importlib.reload(s01r_layout); importlib.reload(s01r_dressing)
import s01r_dressing as D

SRC = '/Game/Level/Stage1'
DST = '/Game/Level/L_Stage01'  # NOTE: rebuilding re-places every S01R actor and drops manual edits; prefer ue_patch_collision_level.py-style patches
args = unreal.SystemLibrary.get_command_line()
eal = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state = {'gen': None, 'wait': 0, 'busy': False, 'deadline': time.monotonic() + 1500, 'h': None}
KEEP = (unreal.DirectionalLight, unreal.SkyLight, unreal.SkyAtmosphere, unreal.ExponentialHeightFog, unreal.VolumetricCloud,
        unreal.PlayerStart, unreal.BoarSpawnPoint, unreal.Cage, unreal.NavMeshBoundsVolume, unreal.RecastNavMesh, unreal.WorldSettings)


def log(m):
    unreal.log('[S01Build] ' + m)


def V(x, y, z):
    return unreal.Vector(x, y, z)


def R(yaw, pitch=0.0):
    return unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw)


def place(mesh_path, x, y, z, yaw, scale, folder, label, collision='default', mobility=unreal.ComponentMobility.STATIC):
    mesh = unreal.load_asset(mesh_path)
    if not mesh:
        log('WARN missing mesh ' + mesh_path)
        return None
    a = eas.spawn_actor_from_object(mesh, V(x, y, z), R(yaw))
    a.set_actor_scale3d(V(*scale))
    a.set_actor_label(label)
    a.set_folder_path(folder)
    a.tags = ['S01R']
    c = a.static_mesh_component
    c.set_mobility(mobility)
    if collision == 'none':
        c.set_collision_profile_name('NoCollision')
        c.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    return a


def wall(label, x, y, sx, sy):
    """Invisible boundary: blocks pawns, ignores the camera so the spring arm never snaps on it."""
    a = place('/Engine/BasicShapes/Cube', x, y, 900, 0, (sx / 100, sy / 100, 24), 'S01R/Boundary', label)
    a.set_actor_hidden_in_game(True)
    c = a.static_mesh_component
    c.set_collision_profile_name('BlockAll')
    c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
    c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY, unreal.CollisionResponseType.ECR_IGNORE)
    c.set_editor_property('cast_shadow', False)
    c.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    return a


def add_rotation(actor, rate):
    """RotatingMovement on the windmill rotor instance (roll around the rotor's local X axis)."""
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = sds.k2_gather_subobject_data_for_instance(actor)
    params = unreal.AddNewSubobjectParams(parent_handle=handles[0], new_class=unreal.RotatingMovementComponent)
    h, fail = sds.add_new_subobject(params)
    if not fail.is_empty():
        log('WARN rotating movement: %s' % fail)
        return False
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h))
    comp.set_editor_property('rotation_rate', unreal.Rotator(roll=rate, pitch=0.0, yaw=0.0))
    comp.set_editor_property('rotation_in_local_space', True)
    return True


def tune_lighting(actors):
    for a in actors:
        if isinstance(a, unreal.DirectionalLight):
            a.set_actor_rotation(R(-55.0, -46.0), False)   # sun behind/right of the start camera: cage front lit
            c = a.light_component
            c.set_editor_property('intensity', 7.5)
            c.set_editor_property('light_color', unreal.Color(r=255, g=246, b=232, a=255))
            c.set_editor_property('light_source_angle', 2.5)  # softer shadow edges
            log('sun set')
        elif isinstance(a, unreal.SkyLight):
            c = a.light_component
            c.set_editor_property('intensity', 1.6)
            c.set_editor_property('lower_hemisphere_is_black', False)
            c.set_editor_property('lower_hemisphere_color', unreal.LinearColor(0.32, 0.42, 0.22, 1.0))
            log('skylight real_time=%s' % c.get_editor_property('real_time_capture'))
        elif isinstance(a, unreal.ExponentialHeightFog):
            c = a.component
            c.set_editor_property('fog_density', 0.006)
            c.set_editor_property('fog_height_falloff', 0.25)
            c.set_editor_property('start_distance', 5000.0)
            a.set_actor_location(V(0, -1000, -300), False, False)


def run():
    if eal.does_asset_exist(DST):
        assert les.load_level(DST)
    else:
        # new level from the Stage1 template: Stage1 is read, never loaded for edit or saved
        assert les.new_level_from_template(DST, SRC), 'new level from template'
    yield 30
    world = ues.get_editor_world()
    assert world.get_path_name().startswith(DST), world.get_path_name()
    actors = eas.get_all_level_actors()
    removed = 0
    for a in actors:
        keep = isinstance(a, KEEP) or a.get_actor_label() == 'SM_SkySphere'
        if 'S01R' in [str(t) for t in a.tags]:
            keep = isinstance(a, unreal.BoarSpawnPoint) and not str(a.get_editor_property('spawn_point_id')).startswith('Stage01_Coin')
        if not keep and not isinstance(a, unreal.WorldSettings):
            eas.destroy_actor(a)
            removed += 1
    log('removed %d old actors' % removed)
    yield 5
    actors = eas.get_all_level_actors()
    tune_lighting(actors)
    # gameplay actors keep their identity; only transforms change
    for a in actors:
        if isinstance(a, unreal.PlayerStart):
            a.set_actor_location_and_rotation(V(D.START[0], D.START[1], D.height(*D.START) + 92), R(-90.0), False, False)
        elif isinstance(a, unreal.Cage):
            log('cage stays at %s' % a.get_actor_location())
        elif isinstance(a, unreal.BoarSpawnPoint):
            sid = str(a.get_editor_property('spawn_point_id'))
            if sid in D.SPAWN_POINTS:
                x, y, yaw = D.SPAWN_POINTS[sid]
                a.set_actor_location_and_rotation(V(x, y, D.height(x, y) + 5), R(yaw), False, False)
                log('spawn %s -> %.0f %.0f' % (sid, x, y))
            else:
                log('WARN spawn point %s not in layout' % sid)
        elif isinstance(a, unreal.NavMeshBoundsVolume):
            a.set_actor_location(V(0, -1000, 350), False, False)
            a.set_actor_scale3d(V(38.5, 27.5, 7.0))
    # special coin candidate on the watchtower deck (not referenced by DA_TestStageConfig)
    cls = unreal.load_class(None, '/Game/BP/Boar/BP_BoarSpawnPoint.BP_BoarSpawnPoint_C')
    x, y, yaw = D.SPAWN_POINTS['Stage01_CoinSpawn_001']
    coin = eas.spawn_actor_from_class(cls, V(x, y, D.height(x, y) + D.TOWER_DECK_RISE + 60), R(yaw))
    coin.set_editor_property('spawn_point_id', 'Stage01_CoinSpawn_001')
    coin.set_actor_label('BP_BoarSpawnPoint_CoinCandidate')
    coin.set_folder_path('S01R/Gameplay')
    coin.tags = ['S01R']
    yield 2
    counts = {}
    for i, p in enumerate(D.placements()):
        name = p['mesh'].split('/')[-1]
        mob = unreal.ComponentMobility.MOVABLE if p['tag'] == 'WindmillRotor' else unreal.ComponentMobility.STATIC
        a = place(p['mesh'], p['x'], p['y'], p['z'], p['yaw'], p['scale'], 'S01R/' + p['tag'], '%s_%03d' % (name, i),
                  p['collision'], mob)
        if a and p['tag'] == 'WindmillRotor':
            log('rotor rotation added=%s' % add_rotation(a, 22.0))
        counts[p['tag']] = counts.get(p['tag'], 0) + 1
        if i % 40 == 0:
            yield 1
    log('placed %s' % counts)
    x0, x1, y0, y1 = D.PLAY
    wall('S01R_Wall_West', x0 - 200, (y0 + y1) / 2, 200, y1 - y0 + 800)
    wall('S01R_Wall_East', x1 + 200, (y0 + y1) / 2, 200, y1 - y0 + 800)
    wall('S01R_Wall_Front', 0, y1 + 200, x1 - x0 + 800, 200)
    wall('S01R_Wall_Back', 0, D.FENCE_Y - 120, x1 - x0 + 800, 200)
    yield 10
    # The editor nav system holds an AsyncLoadLock until asset compilation settles; Build() is a no-op while locked.
    # Retry until probes on the new geometry (pond bed, bridge deck, hill top) project onto the navmesh.
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
            p = unreal.NavigationSystemV1.project_point_to_navigation(world, V(x, y, z + 40), None, None, V(40, 40, 80))
            hits.append(p is not None and abs(p.z - z) < 30 and (abs(p.x) + abs(p.y)) > 0)
        log('nav attempt %d probes=%s' % (attempt, hits))
        if all(hits):
            ok = True
            break
    log('navigation rebuilt=%s' % ok)
    yield 10
    les.save_current_level()
    log('saved ' + DST)


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
