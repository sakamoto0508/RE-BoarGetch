"""Patch L_Stage02 in place (full editor, ticked) WITHOUT rebuilding it, so manual edits made in the editor survive.
UnrealEditor.exe <uproject> /Engine/Maps/Entry -ExecutePythonScript=<this> -RenderOffscreen ... [-S02Quit]

2026-11-19 patch (after the user's hand edits: bridge moved to the old ford, bollards/beacons/shortcut signposts removed):
  1. reimport SM_S02_Terrain (old bridge abutment and its path removed, paths rerouted over the new bridge) and shift every
     prop by the terrain height change under it (no floating / buried props)
  2. replace all LP flowers with natural clusters (s02_dressing.natural_flowers / landing clusters), drop the ford stones
  3. invisible water blockers along the river and over the pond (players and boars cannot enter the water; the span
     under the bridge stays open - the deck is only ~76 cm above the river bed, nothing fits under it)
  4. rebuild navigation, check paths, save. Writes Logs/patch_result.json."""
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
RESULT = {}
SKIP_REGROUND = ('SM_S02_Terrain', 'SM_S02_RiverWater', 'SM_S02_PondWater', 'SM_LPBGMountain', 'SM_S01R_WoodBridge', 'Cube')


def log(m):
    unreal.log_warning('[S02Patch] ' + m)


def V(x, y, z):
    return unreal.Vector(x, y, z)


def mesh_name(a):
    if isinstance(a, unreal.StaticMeshActor) and a.static_mesh_component.static_mesh:
        return a.static_mesh_component.static_mesh.get_name()
    return ''


def terrain_actor():
    for a in eas.get_all_level_actors():
        if mesh_name(a) == 'SM_S02_Terrain':
            return a
    return None


def ground(world, terrain, x, y):
    """Terrain height at (x, y): trace that only accepts hits on the terrain actor."""
    ignore = []
    for _ in range(6):
        hit = unreal.SystemLibrary.line_trace_single(world, V(x, y, 4000), V(x, y, -2000), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                     True, ignore, unreal.DrawDebugTrace.NONE, True)
        if not hit:
            return None
        t = hit.to_tuple()
        if t[9] == terrain:
            return t[4].z
        if t[9] is None:
            return None
        ignore.append(t[9])
    return None


def place(mesh_path, x, y, z, yaw, scale, folder, label, collision='default'):
    mesh = unreal.load_asset(mesh_path)
    a = eas.spawn_actor_from_object(mesh, V(x, y, z), unreal.Rotator(roll=0, pitch=0, yaw=yaw))
    a.set_actor_scale3d(V(*scale))
    a.set_actor_label(label)
    a.set_folder_path(folder)
    a.tags = ['S02']
    c = a.static_mesh_component
    c.set_mobility(unreal.ComponentMobility.STATIC)
    if collision == 'none':
        c.set_collision_profile_name('NoCollision')
        c.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    return a


def water_blocker(label, x, y, z0, z1, length, width, yaw):
    """Invisible (editor and game) pawn blocker; ignores camera/visibility; cuts the NavMesh."""
    a = place('/Engine/BasicShapes/Cube', x, y, (z0 + z1) / 2, yaw, (length / 100, width / 100, (z1 - z0) / 100),
              'Stage02/WaterBlock', label)
    c = a.static_mesh_component
    c.set_visibility(False)
    a.set_actor_hidden_in_game(True)
    c.set_collision_profile_name('BlockAll')
    c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
    c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY, unreal.CollisionResponseType.ECR_IGNORE)
    c.set_editor_property('cast_shadow', False)
    c.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    c.set_editor_property('can_ever_affect_navigation', True)
    return a


def bridge_axis_dist(x, y):
    """Distance from the bridge's long axis (the span between its ends)."""
    bx, by = D.BRIDGE
    ux, uy = math.cos(math.radians(D.BRIDGE_YAW)), math.sin(math.radians(D.BRIDGE_YAW))
    dx, dy = x - bx, y - by
    along = dx * ux + dy * uy
    return abs(-dx * uy + dy * ux), along


def build_water_blockers():
    n = 0
    pts = []
    rs = D.RIVER_S
    # resample the river centreline every ~180 cm
    for i in range(len(rs) - 1):
        (ax, ay), (bx, by) = rs[i], rs[i + 1]
        seg = math.hypot(bx - ax, by - ay)
        k = max(1, int(seg // 180))
        for j in range(k):
            pts.append((ax + (bx - ax) * j / k, ay + (by - ay) * j / k))
    pts.append(rs[-1])
    skipped = 0
    for i in range(len(pts) - 1):
        (ax, ay), (bx, by) = pts[i], pts[i + 1]
        cx, cy = (ax + bx) / 2, (ay + by) / 2
        side, along = bridge_axis_dist(cx, cy)
        if side < 170 and abs(along) < 320:      # under the bridge deck: leave open
            skipped += 1
            continue
        ln = math.hypot(bx - ax, by - ay) + 60
        yaw = math.degrees(math.atan2(by - ay, bx - ax))
        wz = D.water_z(cx, cy)
        water_blocker('S02_WaterBlock_River_%03d' % i, cx, cy, wz - 150, wz + 260, ln, D.RIVER_W * 0.72 + 30, yaw)
        n += 1
    # pond: strips along the ellipse's long axis
    pcx, pcy, rx, ry, rot = D.POND
    wz = D.water_z(pcx, pcy)
    strips = 9
    for k in range(strips):
        u0 = -rx * 0.97 + 2 * rx * 0.97 * k / strips
        u1 = u0 + 2 * rx * 0.97 / strips
        um = (u0 + u1) / 2
        half_v = ry * 0.97 * math.sqrt(max(0.0, 1 - (max(abs(u0), abs(u1)) / (rx * 0.97)) ** 2 * 0.6 - (um / (rx * 0.97)) ** 2 * 0.4))
        if half_v < 40:
            continue
        x = pcx + um * math.cos(rot)
        y = pcy + um * math.sin(rot)
        water_blocker('S02_WaterBlock_Pond_%02d' % k, x, y, wz - 150, wz + 260, (u1 - u0) + 20, 2 * half_v, math.degrees(rot))
        n += 1
    return n, skipped


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
    return {'ok': bool(pts) and not p.is_partial() and reached, 'len_m': round(ln / 100, 1), 'n': len(pts)}


def run():
    assert les.load_level(DST)
    yield 40
    world = ues.get_editor_world()
    assert world.get_path_name().startswith(DST), world.get_path_name()
    terrain = terrain_actor()
    assert terrain, 'terrain actor'
    # 1a. ground under every prop before the terrain changes
    props = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.StaticMeshActor)
             and not any(s in mesh_name(a) for s in SKIP_REGROUND) and 'Boundary' not in a.get_actor_label()]
    before = {}
    for i, a in enumerate(props):
        l = a.get_actor_location()
        g = ground(world, terrain, l.x, l.y)
        if g is not None:
            before[a.get_name()] = g
        if i % 60 == 0:
            yield 1
    log('measured %d props' % len(before))
    # 1b. reimport the terrain (headless import script, run in this editor)
    g = {'__file__': os.path.join(HERE, 'ue_import_assets.py'), '__name__': '__s02_import__'}
    exec(open(os.path.join(HERE, 'ue_import_assets.py'), encoding='utf-8').read(), g)
    yield 120
    terrain = terrain_actor()
    moved = []
    for i, a in enumerate(props):
        k = a.get_name()
        if k not in before:
            continue
        l = a.get_actor_location()
        g2 = ground(world, terrain, l.x, l.y)
        if g2 is None:
            continue
        dz = g2 - before[k]
        if abs(dz) > 1.0:
            a.set_actor_location(V(l.x, l.y, l.z + dz), False, False)
            moved.append((a.get_actor_label(), round(dz, 1)))
        if i % 60 == 0:
            yield 1
    RESULT['regrounded'] = moved
    log('re-grounded %d props (max |dz| %.1f)' % (len(moved), max([abs(m[1]) for m in moved] or [0])))
    # 2. flowers and ford stones
    removed = []
    for a in eas.get_all_level_actors():
        m = mesh_name(a)
        l = a.get_actor_location()
        is_ford = m.startswith('SM_LPRockSlab') and math.hypot(l.x - D.BRIDGE[0], l.y - D.BRIDGE[1]) < 500
        if m.startswith('SM_LPFlower') or is_ford or a.get_actor_label().startswith('S02_WaterBlock'):
            removed.append(a.get_actor_label())
            eas.destroy_actor(a)
    RESULT['removed'] = removed
    yield 5
    nf = 0
    for i, p in enumerate([p for p in D.placements() if p['tag'] == 'Flowers']):
        place(p['mesh'], p['x'], p['y'], p['z'], p['yaw'], p['scale'], 'Stage02/Flowers', 'S02_%s_%03d' % (p['mesh'].split('/')[-1], i), 'none')
        nf += 1
    RESULT['flowers_placed'] = nf
    log('removed %d (flowers/ford stones), placed %d flower-cluster items' % (len(removed), nf))
    # 3. water blockers
    n, skipped = build_water_blockers()
    RESULT['water_blockers'] = n
    RESULT['river_segments_open_under_bridge'] = skipped
    log('water blockers %d (open under bridge: %d)' % (n, skipped))
    yield 10
    # 4. navigation
    bx, by = D.BRIDGE
    ux, uy = math.cos(math.radians(D.BRIDGE_YAW)), math.sin(math.radians(D.BRIDGE_YAW))
    probes = {'cage': D.CAGE, 'start': D.START, 'ridge_top': (3250.0, -1500.0), 'west_hill': (1000.0, -4000.0),
              'west_bank_south': (-900.0, -3500.0), 'bridge_east': (bx + ux * 420, by + uy * 420), 'bridge_west': (bx - ux * 420, by - uy * 420),
              'old_bridge_east': (-1500.0, -2450.0), 'old_bridge_west': (-1400.0, -3300.0), 'terrace': (300.0, 3300.0)}
    hits = {}
    for attempt in range(12):
        yield 200
        unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
        yield 20
        t0 = time.monotonic()
        while unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world) and time.monotonic() - t0 < 300:
            yield 10
        hits = {}
        for k, (x, y) in probes.items():
            z = D.height(x, y)
            p = unreal.NavigationSystemV1.project_point_to_navigation(world, V(x, y, z + 40), None, None, V(60, 60, 120))
            hits[k] = p is not None and abs(p.z - z) < 60 and (abs(p.x) + abs(p.y)) > 0
        log('nav attempt %d probes=%s' % (attempt, hits))
        if all(hits.values()):
            break
    RESULT['nav_probes'] = hits
    pairs = {'bridge_east->bridge_west (over the bridge)': ('bridge_east', 'bridge_west'),
             'old_bridge_east->old_bridge_west (must detour)': ('old_bridge_east', 'old_bridge_west'),
             'cage->west_hill': ('cage', 'west_hill'), 'cage->west_bank_south': ('cage', 'west_bank_south'),
             'start->west_bank_south': ('start', 'west_bank_south'), 'cage->ridge_top': ('cage', 'ridge_top'),
             'cage->terrace': ('cage', 'terrace'), 'start->cage': ('start', 'cage')}
    RESULT['nav_paths'] = {k: nav_path(world, probes[a], probes[b]) for k, (a, b) in pairs.items()}
    log('nav paths %s' % RESULT['nav_paths'])
    # water really blocks: points in the river / pond are not on the navmesh
    inwater = {}
    for k, (x, y) in {'river_mid': (800.0, -2450.0), 'river_south': (-1500.0, -2860.0), 'pond': D.POND[:2]}.items():
        p = unreal.NavigationSystemV1.project_point_to_navigation(world, V(x, y, D.water_z(x, y) + 40), None, None, V(40, 40, 80))
        inwater[k] = p is not None and math.hypot(p.x - x, p.y - y) < 40
    RESULT['navmesh_inside_water'] = inwater
    log('navmesh inside water %s' % inwater)
    yield 10
    RESULT['saved'] = bool(les.save_current_level())
    log('saved %s' % RESULT['saved'])
    json.dump(RESULT, open(os.path.join(ROOT, 'Logs', 'patch_result.json'), 'w'), indent=0)


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
