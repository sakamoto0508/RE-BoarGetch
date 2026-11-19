"""Rebuild /Game/Level/L_Stage02 (AKI-174 Phase A redesign) in the full editor (ticked, real nav build).
UnrealEditor.exe <uproject> /Engine/Maps/Entry -ExecutePythonScript=<this> -RenderOffscreen ... [-S02Quit]

Keeps the lighting/sky, PlayerStart, BP_Cage, NavMeshBoundsVolume, RecastNavMesh and WorldSettings (GameMode
override BP_GM_Stage02) of the existing level; removes every other actor (the old Cube blockout), places
s02_dressing.placements(), invisible boundary walls, rebuilds navigation, verifies paths, saves L_Stage02 only.
Writes Logs/build_result.json (actors, nav probes, path checks)."""
import json, math, os, sys, time, traceback, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import importlib, s02_layout, s02_dressing
importlib.reload(s02_layout); importlib.reload(s02_dressing)
import s02_dressing as D

DST = '/Game/Level/L_Stage02'
ROOT = os.path.dirname(HERE)
args = unreal.SystemLibrary.get_command_line()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state = {'gen': None, 'wait': 0, 'busy': False, 'deadline': time.monotonic() + 2400, 'h': None}
KEEP = (unreal.DirectionalLight, unreal.SkyLight, unreal.SkyAtmosphere, unreal.ExponentialHeightFog, unreal.VolumetricCloud,
        unreal.PlayerStart, unreal.Cage, unreal.NavMeshBoundsVolume, unreal.RecastNavMesh, unreal.WorldSettings)
RESULT = {'placed': {}, 'missing': [], 'nav': {}}


def log(m):
    unreal.log_warning('[S02Build] ' + m)


def V(x, y, z):
    return unreal.Vector(x, y, z)


def R(yaw, pitch=0.0):
    return unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw)


def place(mesh_path, x, y, z, yaw, scale, folder, label, collision='default', mobility=unreal.ComponentMobility.STATIC):
    mesh = unreal.load_asset(mesh_path)
    if not mesh:
        RESULT['missing'].append(mesh_path)
        log('WARN missing mesh ' + mesh_path)
        return None
    a = eas.spawn_actor_from_object(mesh, V(x, y, z), R(yaw))
    a.set_actor_scale3d(V(*scale))
    a.set_actor_label(label)
    a.set_folder_path(folder)
    a.tags = ['S02']
    c = a.static_mesh_component
    c.set_mobility(mobility)
    if collision == 'none':
        c.set_collision_profile_name('NoCollision')
        c.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    return a


def wall(label, x, y, sx, sy):
    """Invisible boundary: blocks pawns, ignores camera/visibility so the spring arm never snaps on it."""
    a = place('/Engine/BasicShapes/Cube', x, y, 900, 0, (sx / 100, sy / 100, 26), 'Stage02/Boundary', label)
    a.set_actor_hidden_in_game(True)
    c = a.static_mesh_component
    c.set_collision_profile_name('BlockAll')
    c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
    c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY, unreal.CollisionResponseType.ECR_IGNORE)
    c.set_editor_property('cast_shadow', False)
    c.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    c.set_editor_property('can_ever_affect_navigation', True)
    return a


def add_rotation(actor, rate):
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
            # summer sun from behind-left of the start camera: Cage, ridge and windmill are front lit
            a.set_actor_rotation(R(D.START_YAW + 35.0, -48.0), False)
            c = a.light_component
            c.set_editor_property('intensity', 7.5)
            c.set_editor_property('light_color', unreal.Color(r=255, g=244, b=226, a=255))
            c.set_editor_property('light_source_angle', 2.5)
        elif isinstance(a, unreal.SkyLight):
            c = a.light_component
            c.set_editor_property('intensity', 1.6)
            c.set_editor_property('lower_hemisphere_is_black', False)
            c.set_editor_property('lower_hemisphere_color', unreal.LinearColor(0.32, 0.42, 0.22, 1.0))
        elif isinstance(a, unreal.ExponentialHeightFog):
            c = a.component
            c.set_editor_property('fog_density', 0.0045)
            c.set_editor_property('fog_height_falloff', 0.18)
            c.set_editor_property('start_distance', 6500.0)
            a.set_actor_location(V(0, 0, -300), False, False)


def nav_probe(world, x, y):
    z = D.height(x, y)
    p = unreal.NavigationSystemV1.project_point_to_navigation(world, V(x, y, z + 40), None, None, V(60, 60, 120))
    return p is not None and abs(p.z - z) < 60 and (abs(p.x) + abs(p.y)) > 0


def nav_path(world, a, b):
    pa = V(a[0], a[1], D.height(*a) + 60)
    pb = V(b[0], b[1], D.height(*b) + 60)
    p = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, pa, pb)
    if not p:
        return {'ok': False}
    pts = p.get_editor_property('path_points')
    ln = sum((pts[i + 1] - pts[i]).length() for i in range(len(pts) - 1)) if len(pts) > 1 else 0
    end = pts[-1] if pts else None
    reached = end is not None and math.hypot(end.x - b[0], end.y - b[1]) < 150
    return {'ok': bool(pts) and not p.is_partial() and reached, 'reached': reached, 'partial': p.is_partial(), 'len_m': round(ln / 100, 1), 'n': len(pts)}


def run():
    assert unreal.EditorAssetLibrary.does_asset_exist(DST), DST
    assert les.load_level(DST)
    yield 30
    world = ues.get_editor_world()
    assert world.get_path_name().startswith(DST), world.get_path_name()
    removed = 0
    for a in eas.get_all_level_actors():
        keep = isinstance(a, KEEP) or a.get_actor_label() == 'SM_SkySphere'
        if not keep:
            eas.destroy_actor(a)
            removed += 1
    log('removed %d old actors' % removed)
    RESULT['removed'] = removed
    yield 5
    actors = eas.get_all_level_actors()
    tune_lighting(actors)
    for a in actors:
        if isinstance(a, unreal.PlayerStart):
            a.set_actor_location_and_rotation(V(D.START[0], D.START[1], D.height(*D.START) + 97), R(D.START_YAW), False, False)
        elif isinstance(a, unreal.Cage):
            a.set_actor_location(V(D.CAGE[0], D.CAGE[1], D.height(*D.CAGE)), False, False)
            a.set_folder_path('Stage02/Gameplay')
        elif isinstance(a, unreal.NavMeshBoundsVolume):
            a.set_actor_location(V(0, 0, 350), False, False)
            a.set_actor_scale3d(V(1, 1, 1))
            o, e = a.get_actor_bounds(False)
            x0, x1, y0, y1 = D.PLAY
            a.set_actor_scale3d(V((x1 - x0) / 2 / e.x + 0.03, (y1 - y0) / 2 / e.y + 0.03, 750 / e.z))
            a.set_folder_path('Stage02/Gameplay')
    yield 2
    counts = {}
    for i, p in enumerate(D.placements()):
        name = p['mesh'].split('/')[-1]
        mob = unreal.ComponentMobility.MOVABLE if p['tag'] == 'WindmillRotor' else unreal.ComponentMobility.STATIC
        a = place(p['mesh'], p['x'], p['y'], p['z'], p['yaw'], p['scale'], 'Stage02/' + p['tag'], '%s_%03d' % (name, i),
                  p['collision'], mob)
        if a and p['tag'] == 'WindmillRotor':
            log('rotor rotation added=%s' % add_rotation(a, 22.0))
        counts[p['tag']] = counts.get(p['tag'], 0) + 1
        if i % 40 == 0:
            yield 1
    RESULT['placed'] = counts
    log('placed %s' % counts)
    x0, x1, y0, y1 = D.PLAY
    wall('S02_Boundary_South', x0 - 150, (y0 + y1) / 2, 200, y1 - y0 + 600)
    wall('S02_Boundary_North', x1 + 150, (y0 + y1) / 2, 200, y1 - y0 + 600)
    wall('S02_Boundary_West', (x0 + x1) / 2, y0 - 150, x1 - x0 + 600, 200)
    wall('S02_Boundary_East', (x0 + x1) / 2, y1 + 150, x1 - x0 + 600, 200)
    yield 10
    ya, yb = D.SHORTCUTS['NW'], D.SHORTCUTS['NE']
    rxa, rxb = D.ridge_x(ya), D.ridge_x(yb)
    probes = {'cage': D.CAGE, 'start': D.START, 'ridge_top': (3250.0, -1500.0), 'ridge_east': (3250.0, 2700.0),
              'terrace': (300.0, 3300.0), 'west_bank': (450.0, -3300.0), 'farm_east': (-2700.0, 900.0),
              'west_hill': (1000.0, -4000.0),
              'A_top': (rxa + 300, ya), 'A_ledge': (rxa - 420, ya), 'landing_NW': (rxa - 1150, ya),
              'B_top': (rxb + 300, yb), 'landing_NE': (rxb - 600, yb), 'east_mid': (1300.0, 3000.0)}
    hits = {}
    for attempt in range(12):
        yield 200
        unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
        yield 20
        t0 = time.monotonic()
        while unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world) and time.monotonic() - t0 < 300:
            yield 10
        hits = {k: nav_probe(world, *v) for k, v in probes.items()}
        log('nav attempt %d probes=%s' % (attempt, hits))
        if all(hits.values()):
            break
    RESULT['nav']['probes'] = hits
    RESULT['nav']['built'] = all(hits.values())
    pairs = {'cage->ridge_top': ('cage', 'ridge_top'), 'cage->ridge_east': ('cage', 'ridge_east'), 'cage->terrace': ('cage', 'terrace'),
             'cage->west_bank': ('cage', 'west_bank'), 'cage->west_hill': ('cage', 'west_hill'), 'start->cage': ('start', 'cage'),
             'terrace->farm_east': ('terrace', 'farm_east'), 'west_bank->cage': ('west_bank', 'cage'),
             'east_mid->ridge_east': ('east_mid', 'ridge_east'),
             # must NOT be reachable directly (boars cannot use the drops)
             'A_top->A_ledge': ('A_top', 'A_ledge'), 'A_ledge->landing_NW': ('A_ledge', 'landing_NW'),
             'B_top->landing_NE': ('B_top', 'landing_NE')}
    RESULT['nav']['paths'] = {k: nav_path(world, probes[a], probes[b]) for k, (a, b) in pairs.items()}
    log('nav paths %s' % RESULT['nav']['paths'])
    # route comparison: boar/normal route on the NavMesh vs the player's drop route (straight-ish walk + falls)
    def flat(a, b):
        return math.hypot(a[0] - b[0], a[1] - b[1]) / 100.0
    wt = (D.LANDMARKS['Watchtower'][0] - 250, D.LANDMARKS['Watchtower'][1])
    probes['watchtower'] = wt
    routes = {
        'A_top->cage': (('A_top', 'cage'), flat(probes['A_top'], (rxa, ya)) + flat((rxa, ya), D.CAGE), 2),
        'B_top->east_mid': (('B_top', 'east_mid'), flat(probes['B_top'], (rxb, yb)) + flat((rxb, yb), probes['east_mid']), 1),
        'B_top->terrace': (('B_top', 'terrace'), flat(probes['B_top'], (rxb, yb)) + flat((rxb, yb), probes['terrace']), 1),
        'watchtower->cage(via A)': (('watchtower', 'cage'), flat(wt, probes['A_top']) + flat(probes['A_top'], (rxa, ya)) + flat((rxa, ya), D.CAGE), 2),
        'watchtower->east_mid(via B)': (('watchtower', 'east_mid'), flat(wt, probes['B_top']) + flat(probes['B_top'], (rxb, yb)) + flat((rxb, yb), probes['east_mid']), 1),
    }
    cmp = {}
    for k, ((a, b), drop_m, falls) in routes.items():
        nav = nav_path(world, probes[a], probes[b])
        nl = nav.get('len_m')
        fall_s = falls * math.sqrt(2 * 3.3 / 9.8)
        cmp[k] = {'nav_m': nl, 'drop_route_m': round(drop_m, 1), 'falls': falls,
                  'walk5_nav_s': round(nl / 5.0, 1) if nl else None, 'walk5_drop_s': round(drop_m / 5.0 + fall_s, 1),
                  'dash9_nav_s': round(nl / 9.0, 1) if nl else None, 'dash9_drop_s': round(drop_m / 9.0 + fall_s, 1),
                  'boar6_nav_s': round(nl / 6.0, 1) if nl else None}
    RESULT['route_compare'] = cmp
    log('route compare %s' % cmp)
    # line of sight from the ridge observation points (eye 170 cm above ground)
    def sees(a, b, hb=120, eye=170):
        pa = V(a[0], a[1], D.height(*a) + eye)
        pb = V(b[0], b[1], D.height(*b) + hb)
        hit = unreal.SystemLibrary.line_trace_single(world, pa, pb, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [],
                                                     unreal.DrawDebugTrace.NONE, True)
        if not hit:
            return True
        t = hit.to_tuple()
        if t[9] is not None and isinstance(t[9], unreal.Cage):
            return True
        return (t[5] - pb).length() < 250      # blocked only if the hit is well before the target
    targets = {'cage': D.CAGE, 'landing_NW': probes['landing_NW'], 'landing_NE': probes['landing_NE'], 'A_top': probes['A_top'],
               'B_top': probes['B_top'], 'main_path_mid': (-1300, 120), 'west_bridge': D.BRIDGE, 'east_mid': probes['east_mid'],
               'terrace': probes['terrace'], 'farm_east': probes['farm_east'], 'start': D.START}
    views = {}
    for name, eye in (('watchtower', wt), ('A_top', probes['A_top']), ('B_top', probes['B_top']), ('ridge_top', probes['ridge_top'])):
        views[name] = {k: sees(eye, v) for k, v in targets.items() if math.hypot(eye[0] - v[0], eye[1] - v[1]) > 300}
    # watchtower deck (~3.3 m platform): start the ray just outside the tower toward each target
    deck = {}
    for k, v in targets.items():
        tx, ty = D.LANDMARKS['Watchtower']
        dx, dy = v[0] - tx, v[1] - ty
        n = math.hypot(dx, dy) or 1
        deck[k] = sees((tx + dx / n * 420, ty + dy / n * 420), v, eye=500)
    views['watchtower_deck'] = deck
    RESULT['visibility'] = views
    log('visibility %s' % views)
    # the ridge face must not be walkable between the trailheads: ridge top -> foot below must detour (> 25 m)
    descent = {}
    y = -2400.0
    while y <= 2400.0:
        top, foot = (D.ridge_x(y) + 350, y), (D.ridge_x(y) - 450, y)
        res = nav_path(world, top, foot)
        descent[str(int(y))] = res.get('len_m') if res.get('ok') else 'no path (blocked)'
        y += 600.0
    RESULT['nav']['ridge_descent_m'] = descent
    RESULT['nav']['ridge_face_blocks'] = all(isinstance(v, str) or v > 25 for v in descent.values())
    log('ridge descent %s' % descent)
    yield 10
    ok = les.save_current_level()
    RESULT['saved'] = bool(ok)
    log('saved %s %s' % (DST, ok))
    dump = []
    for a in eas.get_all_level_actors():
        o, e = a.get_actor_bounds(False)
        d = {'label': a.get_actor_label(), 'cls': a.get_class().get_name(), 'loc': [round(v) for v in (a.get_actor_location().x, a.get_actor_location().y, a.get_actor_location().z)],
             'yaw': round(a.get_actor_rotation().yaw, 1), 'bmin': [round(o.x - e.x), round(o.y - e.y), round(o.z - e.z)],
             'bmax': [round(o.x + e.x), round(o.y + e.y), round(o.z + e.z)]}
        if isinstance(a, unreal.StaticMeshActor) and a.static_mesh_component.static_mesh:
            d['mesh'] = a.static_mesh_component.static_mesh.get_path_name().split('.')[0]
            d['col'] = str(a.static_mesh_component.get_collision_profile_name())
        dump.append(d)
    RESULT['actors'] = dump
    json.dump(RESULT, open(os.path.join(ROOT, 'Logs', 'build_result.json'), 'w'), indent=0)


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
