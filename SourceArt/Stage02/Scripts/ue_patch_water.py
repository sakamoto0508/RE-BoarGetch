"""Patch the water blockers of L_Stage02 in place (full editor, ticked). Keeps every other actor untouched.
- make each S02_WaterBlock_* tall (water - 150 .. water + 950) so nobody can stand on an invisible top
- add a NavModifierComponent (NavArea_Null) so the NavMesh is removed over the river and the pond for sure
- rebuild navigation, verify (no navmesh in the water, bridge crossing works), save. Writes Logs/water_patch.json."""
import json, math, os, sys, time, traceback, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import importlib, s02_layout
importlib.reload(s02_layout)
import s02_layout as L

DST = '/Game/Level/L_Stage02'
ROOT = os.path.dirname(HERE)
args = unreal.SystemLibrary.get_command_line()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state = {'gen': None, 'wait': 0, 'busy': False, 'deadline': time.monotonic() + 1800, 'h': None}
RESULT = {}


def log(m):
    unreal.log_warning('[S02Water] ' + m)


def V(x, y, z):
    return unreal.Vector(x, y, z)


def add_nav_null(actor):
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = sds.k2_gather_subobject_data_for_instance(actor)
    for h in handles:   # idempotent: skip if one already exists
        o = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h))
        if isinstance(o, unreal.NavModifierComponent):
            return 'exists'
    params = unreal.AddNewSubobjectParams(parent_handle=handles[0], new_class=unreal.NavModifierComponent)
    h, fail = sds.add_new_subobject(params)
    if not fail.is_empty():
        return 'fail %s' % fail
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h))
    comp.set_editor_property('area_class', unreal.NavArea_Null)
    return 'added'


def run():
    assert les.load_level(DST)
    yield 40
    world = ues.get_editor_world()
    assert world.get_path_name().startswith(DST), world.get_path_name()
    blockers = [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith('S02_WaterBlock_')]
    res = {}
    for i, a in enumerate(blockers):
        l = a.get_actor_location()
        s = a.get_actor_scale3d()
        wz = L.water_z(l.x, l.y)
        z0, z1 = wz - 150, wz + 950
        a.set_actor_location(V(l.x, l.y, (z0 + z1) / 2), False, False)
        a.set_actor_scale3d(V(s.x, s.y, (z1 - z0) / 100))
        r = add_nav_null(a)
        res[r] = res.get(r, 0) + 1
        if i % 20 == 0:
            yield 1
    RESULT['blockers'] = len(blockers)
    RESULT['nav_modifier'] = res
    log('blockers %d nav modifiers %s' % (len(blockers), res))
    bx, by = L.BRIDGE
    ux, uy = math.cos(math.radians(L.BRIDGE_YAW)), math.sin(math.radians(L.BRIDGE_YAW))
    probes = {'cage': L.CAGE, 'bridge_east': (bx + ux * 420, by + uy * 420), 'bridge_west': (bx - ux * 420, by - uy * 420),
              'west_bank_south': (-900.0, -3500.0), 'west_hill': (1000.0, -4000.0)}
    water_pts = {'pond_center': L.POND[:2], 'pond_n': (L.POND[0] + 300, L.POND[1] + 140), 'pond_s': (L.POND[0] - 300, L.POND[1] - 140),
                 'river_mid': (800.0, -2450.0), 'river_old_bridge': (-1500.0, -2860.0), 'river_south': (-2300.0, -3250.0),
                 'river_north': (1550.0, -2700.0)}
    def water_has_nav():
        for (x, y) in water_pts.values():
            for z in (-60, 0, 40, 80, 150, 330):
                p = unreal.NavigationSystemV1.project_point_to_navigation(world, V(x, y, z), None, None, V(30, 30, 30))
                if p is not None and (abs(p.x) + abs(p.y)) > 0:
                    return True
        return False
    # The editor silently ignores Build() while assets compile (AsyncLoadLock): the old navmesh then still passes ground
    # probes. Retry until the navmesh really reflects the blockers (no navmesh left in the water).
    for attempt in range(14):
        yield 200
        unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
        yield 30
        t0 = time.monotonic()
        while unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world) and time.monotonic() - t0 < 300:
            yield 10
        ok = all(unreal.NavigationSystemV1.project_point_to_navigation(world, V(x, y, L.height(x, y) + 40), None, None, V(60, 60, 120))
                 is not None for (x, y) in probes.values())
        wet = water_has_nav()
        log('nav attempt %d ground_ok=%s water_has_nav=%s' % (attempt, ok, wet))
        RESULT['nav_attempts'] = attempt + 1
        if ok and not wet:
            break
    inwater = {}
    for k, (x, y) in water_pts.items():
        hits = []
        for z in (-60, 0, 40, 80, 150, 330, 700, 1000):
            p = unreal.NavigationSystemV1.project_point_to_navigation(world, V(x, y, z), None, None, V(30, 30, 30))
            if p is not None and (abs(p.x) + abs(p.y)) > 0:
                hits.append([z, round(p.z)])
        inwater[k] = hits
    RESULT['navmesh_in_water'] = inwater
    log('navmesh in water %s' % inwater)

    def path(a, b):
        pa = V(a[0], a[1], L.height(*a) + 60); pb = V(b[0], b[1], L.height(*b) + 60)
        p = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, pa, pb)
        if not p:
            return None
        pts = p.get_editor_property('path_points')
        ln = sum((pts[i + 1] - pts[i]).length() for i in range(len(pts) - 1)) if len(pts) > 1 else 0
        reached = bool(pts) and math.hypot(pts[-1].x - b[0], pts[-1].y - b[1]) < 150
        return {'ok': reached and not p.is_partial(), 'len_m': round(ln / 100, 1)}
    RESULT['paths'] = {'over_bridge': path(probes['bridge_east'], probes['bridge_west']),
                       'cage->west_bank_south': path(L.CAGE, probes['west_bank_south']),
                       'cage->west_hill': path(L.CAGE, probes['west_hill']),
                       'old_bridge_east->old_bridge_west': path((-1500.0, -2450.0), (-1400.0, -3300.0)),
                       'pond_shore->pond_center': path((L.POND[0] + 1000, L.POND[1] + 450), L.POND[:2])}
    log('paths %s' % RESULT['paths'])
    yield 10
    RESULT['saved'] = bool(les.save_current_level())
    log('saved %s' % RESULT['saved'])
    json.dump(RESULT, open(os.path.join(ROOT, 'Logs', 'water_patch.json'), 'w'), indent=1)


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
