"""Add invisible water blockers to L_Stage01 in place (full editor, ticked). Every other actor stays untouched.
- removes old S01_WaterBlock_* actors, then places pawn-only blockers over the pond and the stream (s01r_water.boxes)
  (tall: water-150 .. water+950 so nobody stands on top; camera/visibility/nets pass through)
- each blocker gets a NavModifierComponent (NavArea_Null) so the NavMesh is cut out of the water
- rebuilds navigation until no navmesh is left in the water, checks paths (bridge stays usable), saves.
Writes Logs/water_patch.json."""
import json, math, os, sys, time, traceback, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import importlib, s01r_layout, s01r_water
importlib.reload(s01r_layout); importlib.reload(s01r_water)
import s01r_layout as L
import s01r_water as W

DST = '/Game/Level/L_Stage01'
ROOT = os.path.dirname(HERE)
args = unreal.SystemLibrary.get_command_line()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state = {'gen': None, 'wait': 0, 'busy': False, 'deadline': time.monotonic() + 1800, 'h': None}
RESULT = {}
CR = unreal.CollisionResponseType


def log(m):
    unreal.log_warning('[S01Water] ' + m)


def V(x, y, z):
    return unreal.Vector(x, y, z)


def add_nav_null(actor):
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = sds.k2_gather_subobject_data_for_instance(actor)
    params = unreal.AddNewSubobjectParams(parent_handle=handles[0], new_class=unreal.NavModifierComponent)
    h, fail = sds.add_new_subobject(params)
    if not fail.is_empty():
        return 'fail %s' % fail
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h))
    comp.set_editor_property('area_class', unreal.NavArea_Null)
    return 'added'


def blocker(label, x, y, sx, sy, wz):
    z0, z1 = wz - 150, wz + 950
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, V(x, y, (z0 + z1) / 2), unreal.Rotator(roll=0, pitch=0, yaw=0))
    c = a.static_mesh_component
    c.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    a.set_actor_scale3d(V(sx / 100, sy / 100, (z1 - z0) / 100))
    a.set_actor_label(label)
    a.set_folder_path('Stage01/WaterBlock')
    a.tags = ['S01', 'WaterBlock']
    c.set_mobility(unreal.ComponentMobility.STATIC)
    c.set_visibility(False)
    a.set_actor_hidden_in_game(True)
    c.set_collision_profile_name('BlockAll')
    c.set_collision_response_to_all_channels(CR.ECR_IGNORE)
    c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN, CR.ECR_BLOCK)
    c.set_editor_property('cast_shadow', False)
    c.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    c.set_editor_property('can_ever_affect_navigation', True)
    return add_nav_null(a)


def run():
    assert les.load_level(DST)
    yield 40
    world = ues.get_editor_world()
    assert world.get_path_name().startswith(DST), world.get_path_name()
    old = [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith('S01_WaterBlock_')]
    for a in old:
        eas.destroy_actor(a)
    RESULT['removed_old'] = len(old)
    res = {}
    bx = W.boxes()
    for i, (label, x, y, sx, sy, wz) in enumerate(bx):
        r = blocker(label, x, y, sx, sy, wz)
        res[r] = res.get(r, 0) + 1
        if i % 10 == 0:
            yield 1
    RESULT['blockers'] = len(bx)
    RESULT['nav_modifier'] = res
    log('blockers %d nav modifiers %s' % (len(bx), res))

    bxc, byc = L.BRIDGE
    probes = {'cage': L.CAGE, 'start': L.START, 'bridge_east': (bxc + 420, byc), 'bridge_west': (bxc - 420, byc),
              'windmill': (-3170.0, -2500.0), 'pond_shore_e': (-1350.0, -500.0), 'spawn_002': (-1150.0, -380.0)}
    water_pts = {'pond_center': L.POND[:2], 'pond_e': (L.POND[0] + 500, L.POND[1] - 100), 'pond_w': (L.POND[0] - 500, L.POND[1] + 100),
                 'pond_n': (L.POND[0], L.POND[1] + 400), 'pond_s': (L.POND[0], L.POND[1] - 400),
                 'stream_a': (-2720.0, -2650.0), 'stream_b': (-2570.0, -1500.0), 'stream_c': (-2470.0, -1150.0),
                 'stream_d': (-2780.0, -2950.0)}

    def nav_hits(x, y, zs):
        hits = []
        for z in zs:
            p = unreal.NavigationSystemV1.project_point_to_navigation(world, V(x, y, z), None, None, V(30, 30, 30))
            if p is not None and (abs(p.x) + abs(p.y)) > 0:
                hits.append([z, round(p.z)])
        return hits

    for attempt in range(14):
        yield 200
        unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
        yield 30
        t0 = time.monotonic()
        while unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world) and time.monotonic() - t0 < 300:
            yield 10
        ok = all(unreal.NavigationSystemV1.project_point_to_navigation(world, V(x, y, L.height(x, y) + 40), None, None, V(60, 60, 120))
                 is not None for (x, y) in probes.values())
        wet = any(nav_hits(x, y, (-60, 0, 40, 100)) for (x, y) in water_pts.values())
        log('nav attempt %d ground_ok=%s water_has_nav=%s' % (attempt, ok, wet))
        RESULT['nav_attempts'] = attempt + 1
        if ok and not wet:
            break
    RESULT['navmesh_in_water'] = {k: nav_hits(x, y, (-60, 0, 40, 100, 330, 700, 1000)) for k, (x, y) in water_pts.items()}
    RESULT['ground_probes'] = {k: unreal.NavigationSystemV1.project_point_to_navigation(world, V(x, y, L.height(x, y) + 40), None, None,
                                                                                     V(60, 60, 120)) is not None for k, (x, y) in probes.items()}

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
                       'cage->windmill': path(L.CAGE, probes['windmill']),
                       'start->spawn_002': path(L.START, probes['spawn_002']),
                       'pond_shore->pond_center (should fail)': path(probes['pond_shore_e'], L.POND[:2])}
    log('result %s' % json.dumps(RESULT))
    yield 10
    RESULT['saved'] = bool(les.save_current_level())
    log('saved %s' % RESULT['saved'])
    json.dump(RESULT, open(os.path.join(ROOT, 'Logs', 'water_patch.json'), 'w'), indent=1)


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
