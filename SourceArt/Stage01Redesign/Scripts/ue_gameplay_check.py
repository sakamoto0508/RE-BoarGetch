"""Stage01 Redesign gameplay verification (full editor, real GPU, PIE).
UnrealEditor.exe <uproject> -ExecutePythonScript=<this> -RenderOffscreen ... [-S01Quit] [-S01NoClear]
Editor-world: navigation paths and collision traces. PIE: start, camera view, move, jump, bridge, ramp/watchtower,
boar spawn/AI, capture -> cage -> clear -> result -> lobby travel. Save files are backed up before and restored after.
Writes Logs/gameplay_check.json and Screenshots/pie/*.png."""
import json, math, os, shutil, sys, time, traceback, unreal
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import s01r_layout as L

PROJECT = HERE.parents[2]
SAVES = PROJECT / 'Saved' / 'SaveGames'
BACKUP = Path(r'C:/Users/sakam/AppData/Local/Temp/claude/C--UnrealGames-REBoarGetch/8c2866f0-cd78-4de3-b9b2-23a70f1fc3ef/scratchpad/savegame_backup')
SHOTS = HERE.parent / 'Screenshots' / 'pie'
SHOTS.mkdir(parents=True, exist_ok=True)
LEVEL = '/Game/Level/L_Stage01'
args = unreal.SystemLibrary.get_command_line()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
res = {'checks': [], 'failed': [], 'values': {}}
state = {'gen': None, 'wait': 0, 'busy': False, 'h': None, 'deadline': time.monotonic() + 1500}


def check(label, ok, detail=None):
    (res['checks'] if ok else res['failed']).append(label + ('' if detail is None else ' [%s]' % detail))
    unreal.log('[S01Check] %s %s %s' % ('PASS' if ok else 'FAIL', label, '' if detail is None else detail))


def V(x, y, z):
    return unreal.Vector(x, y, z)


def dist(a, b):
    return math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2)


def trace_down(world, x, y, top=1500):
    hit = unreal.SystemLibrary.line_trace_single(world, V(x, y, top), V(x, y, -500), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                 False, [], unreal.DrawDebugTrace.NONE, True)
    if not hit:
        return None, None
    t = hit.to_tuple()
    loc, actor = t[4], t[9]
    return round(loc.z, 1), (actor.get_actor_label() if actor else None)


def screenshot(world, name):
    before = set(p for p in (PROJECT / 'Saved' / 'Screenshots').rglob('*.png'))
    unreal.SystemLibrary.execute_console_command(world, 'HighResShot 1600x900')
    for _ in range(120):
        yield 2
        new = [p for p in (PROJECT / 'Saved' / 'Screenshots').rglob('*.png') if p not in before]
        if new:
            yield 10
            shutil.copy(new[0], SHOTS / (name + '.png'))
            new[0].unlink()
            unreal.log('[S01Check] shot ' + name)
            return


def look(pc, pitch, yaw):
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw))


def walk(pawn, direction, seconds, track=None):
    """Hold movement input for real seconds (frame rate in an offscreen editor is not fixed)."""
    world = pawn.get_world()
    t0, w0, n, vmax = unreal.GameplayStatics.get_time_seconds(world), time.monotonic(), 0, 0.0
    while unreal.GameplayStatics.get_time_seconds(world) - t0 < seconds:
        pawn.add_movement_input(direction, 1.0, True)
        if track is not None:
            track.append(pawn.get_actor_location().z)
        v = pawn.get_velocity()
        vmax = max(vmax, math.hypot(v.x, v.y))
        n += 1
        yield 1
    mc = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    unreal.log('[S01Check] walk diag ticks=%d game=%.2fs wall=%.2fs vmax=%.0f max_walk=%s mode=%s dilation=%s' % (
        n, unreal.GameplayStatics.get_time_seconds(world) - t0, time.monotonic() - w0, vmax, mc.get_editor_property('max_walk_speed'),
        mc.movement_mode, unreal.GameplayStatics.get_global_time_dilation(world)))


def walk_to(pc, pawn, goal, seconds, track=None):
    """Physically walk the player pawn along the navmesh with path following (no teleports)."""
    world = pawn.get_world()
    unreal.AIHelperLibrary.simple_move_to_location(pc, goal)
    t0, vmax = unreal.GameplayStatics.get_time_seconds(world), 0.0
    while unreal.GameplayStatics.get_time_seconds(world) - t0 < seconds:
        loc = pawn.get_actor_location()
        if track is not None:
            track.append(loc)
        v = pawn.get_velocity()
        vmax = max(vmax, math.hypot(v.x, v.y))
        if math.hypot(loc.x - goal.x, loc.y - goal.y) < 80:
            break
        yield 1
    unreal.log('[S01Check] walk_to diag game=%.2fs vmax=%.0f end=%s' % (unreal.GameplayStatics.get_time_seconds(world) - t0, vmax,
               [round(pawn.get_actor_location().x), round(pawn.get_actor_location().y), round(pawn.get_actor_location().z)]))


def editor_checks(world):
    nav = unreal.NavigationSystemV1.get_navigation_system(world)
    start = V(L.START[0], L.START[1], 90)
    tz = L.height(*L.TOWER)
    goals = {
        'cage plaza edge': (L.CAGE[0], L.CAGE[1] + 720, 40),
        'tower deck': (L.TOWER[0] + 60, L.TOWER[1], None),
        'bridge middle': (L.BRIDGE[0], L.BRIDGE[1], None),
        'windmill door (across bridge)': (L.WINDMILL[0] + 330 * math.cos(math.radians(20)), L.WINDMILL[1] + 330 * math.sin(math.radians(20)), 40),
        'facility gate': (L.CAGE[0], L.FENCE_Y + 250, 40),
        'waterfall pool bank': (-2520, -2950, None),
    }
    for k, (x, y, yaw) in L.SPAWN_POINTS.items():
        if 'Coin' not in k:
            goals['spawn ' + k] = (x, y, L.height(x, y) + 40)
    for name, (x, y, z) in goals.items():
        if z is None:  # stand on whatever is walkable there (below the tower roof)
            z = (trace_down(world, x, y, top=L.height(x, y) + 300)[0] or L.height(x, y)) + 20
        p = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, start, V(x, y, z))
        pts = list(p.get_editor_property('path_points')) if p else []
        ok = bool(p) and p.is_valid() and not p.is_partial() and len(pts) > 1 and dist(pts[-1], V(x, y, z)) < 150
        length = round(p.get_path_length()) if p and p.is_valid() else None
        check('Nav path start -> ' + name, ok, {'len_cm': length, 'end': [round(pts[-1].x), round(pts[-1].y), round(pts[-1].z)] if pts else None})
    probes = {
        'bridge deck': (L.BRIDGE[0], L.BRIDGE[1], 'SM_S01R_WoodBridge', 30),
        'tower deck': (L.TOWER[0] + 60, L.TOWER[1], 'SM_S01R_Watchtower', tz + L.TOWER_DECK_RISE - 5),
        'tower ramp middle': (L.TOWER[0] - 160 - 240, L.TOWER[1], 'SM_S01R_Watchtower', tz + 60),
        'cage plaza': (L.CAGE[0] + 450, L.CAGE[1], 'SM_S01R_CagePlaza', 0),
        'pond (water has no collision)': (L.POND[0], L.POND[1], 'SM_S01R_Terrain', -80),
        'stream (water has no collision)': (-2600, -1750, 'SM_S01R_Terrain', -60),
        'hill top': (L.HILL[0] - 300, L.HILL[1] + 250, 'SM_S01R_Terrain', 180),
        'start meadow': (L.START[0], L.START[1] - 150, 'SM_S01R_Terrain', -10),
    }
    for name, (x, y, expect, zmin) in probes.items():
        z, label = trace_down(world, x, y, top=L.height(x, y) + 300)
        check('Collision ' + name, label is not None and label.startswith(expect) and z >= zmin, {'z': z, 'hit': label})


def run():
    BACKUP.mkdir(parents=True, exist_ok=True)
    for p in SAVES.glob('*.sav'):
        shutil.copy2(p, BACKUP / p.name)
    res['values']['saves_backed_up'] = [p.name for p in SAVES.glob('*.sav')]
    assert les.load_level(LEVEL)
    yield 300  # let async collision cooking settle before tracing
    editor_checks(ues.get_editor_world())
    les.editor_request_begin_play()
    while not les.is_in_play_in_editor():
        yield 1
    while True:
        world = ues.get_game_world()
        pc = unreal.GameplayStatics.get_player_controller(world, 0) if world else None
        if pc and pc.get_controlled_pawn():
            break
        yield 1
    mode = unreal.GameplayStatics.get_game_mode(world)
    t0 = time.monotonic()
    while mode.get_stage_state() != unreal.BoarStageState.PLAYING and time.monotonic() - t0 < 25:
        yield 10
    if mode.get_stage_state() != unreal.BoarStageState.PLAYING:
        mode.complete_stage_introduction()
        yield 30
    check('Stage reaches Playing', mode.get_stage_state() == unreal.BoarStageState.PLAYING, str(mode.get_stage_state()))
    check('Initialization error empty', str(mode.get_initialization_error()) == '', str(mode.get_initialization_error()))
    pawn = pc.get_controlled_pawn()
    p0 = pawn.get_actor_location()
    check('Player spawned at the start meadow', dist(p0, V(L.START[0], L.START[1], p0.z)) < 200, [round(p0.x), round(p0.y), round(p0.z)])
    rot = pc.get_control_rotation()
    res['values']['start_control_yaw'] = round(rot.yaw, 1)
    boars = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.BoarBase)
    cage = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Cage)[0]
    near = sorted(round(dist(p0, b.get_actor_location())) for b in boars)
    check('Three boars spawned, first one near the start', len(boars) == 3 and near[0] < 1200, near)
    cam = unreal.GameplayStatics.get_player_camera_manager(world, 0)
    cl = cam.get_camera_location()
    hit = unreal.SystemLibrary.line_trace_single(world, cl, cage.get_actor_location() + V(0, 0, 150), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                 False, [pawn], unreal.DrawDebugTrace.NONE, True)
    blocker = hit.to_tuple()[9] if hit else None
    check('Cage visible from the start camera (no blocker in between)', blocker is None or blocker == cage,
          blocker.get_actor_label() if blocker else 'clear')
    fwd = cam.get_actor_forward_vector()
    to_cage = cage.get_actor_location() - cl
    ang = math.degrees(math.acos(max(-1, min(1, (fwd.x * to_cage.x + fwd.y * to_cage.y) / (math.hypot(fwd.x, fwd.y) * math.hypot(to_cage.x, to_cage.y))))))
    check('Cage is ahead of the start camera', ang < 30, round(ang, 1))
    lib = getattr(unreal, 'RetroRenderEditorLibrary', None)
    for _ in range(40):  # never shoot the default material while shaders compile
        if lib and lib.get_remaining_shader_jobs() > 0:
            lib.finish_shader_compilation()
        yield 10
    unreal.GameplayStatics.set_game_paused(world, True)
    yield 30
    yield from screenshot(world, 'pie_01_start')
    unreal.GameplayStatics.set_game_paused(world, False)
    boar_start = [b.get_actor_location() for b in boars]
    yield from walk(pawn, V(0, -1, 0), 1.5)
    check('Player moves with movement input (harness-limited, same metric as RetroStage1Gameplay)', dist(p0, pawn.get_actor_location()) > 50,
          round(dist(p0, pawn.get_actor_location())))
    yield from walk_to(pc, pawn, V(L.CAGE[0], L.CAGE[1] + 760, 0), 20)
    check('Player walks from the start meadow to the cage plaza (no jump needed)', dist(pawn.get_actor_location(), V(L.CAGE[0], L.CAGE[1] + 760, pawn.get_actor_location().z)) < 150,
          [round(pawn.get_actor_location().x), round(pawn.get_actor_location().y)])
    z0 = pawn.get_actor_location().z
    pawn.jump()
    peak = z0
    for _ in range(40):
        yield 1
        peak = max(peak, pawn.get_actor_location().z)
    check('Player can jump', peak - z0 > 80, round(peak - z0))
    for _ in range(200):
        yield 1
    moved = [round(dist(a, b.get_actor_location())) for a, b in zip(boar_start, boars)]
    check('Boar AI moves', max(moved) > 50, moved)
    # bridge crossing (east -> west) on foot: path following from the east bank to the windmill side
    pawn.set_actor_location(V(L.BRIDGE[0] + 520, L.BRIDGE[1], 120), False, True)
    yield 20
    track = []
    yield from walk_to(pc, pawn, V(L.BRIDGE[0] - 480, L.BRIDGE[1], 0), 20, track)
    on_bridge = [p.z for p in track if abs(p.x - L.BRIDGE[0]) < 150]
    pl = pawn.get_actor_location()
    check('Walk across the wooden bridge', pl.x < L.BRIDGE[0] - 300 and on_bridge and max(on_bridge) > 110,
          {'x': round(pl.x), 'z_on_deck_max': round(max(on_bridge)) if on_bridge else None})
    # ramp to the watchtower deck
    tz = L.height(*L.TOWER)
    pawn.set_actor_location(V(L.TOWER[0] - 900, L.TOWER[1], tz + 100), False, True)
    yield 20
    yield from walk_to(pc, pawn, V(L.TOWER[0] + 60, L.TOWER[1], tz + L.TOWER_DECK_RISE), 20)
    pl = pawn.get_actor_location()
    check('Walk up the ramp onto the watchtower deck (coin candidate)', pl.z > tz + L.TOWER_DECK_RISE + 60, {'z': round(pl.z), 'x': round(pl.x)})
    unreal.AIHelperLibrary.simple_move_to_location(pc, pl)
    look(pc, -18, 180)
    yield 30
    yield from screenshot(world, 'pie_02_tower_view')
    # walk from the hill top down the slope to the ring path and back up from the start meadow
    yield from walk_to(pc, pawn, V(1700, -1300, 0), 20)
    check('Hill slope walkable down to the ring path', pawn.get_actor_location().x < 1800, round(pawn.get_actor_location().x))
    yield from walk_to(pc, pawn, V(L.HILL[0] - 300, L.HILL[1] + 300, tz), 20)
    check('Hill slope walkable up from the ring path', pawn.get_actor_location().z > tz + 60, round(pawn.get_actor_location().z))
    # solid props: push the player straight into them with a sweep; it must stop at the surface
    targets = {}
    for act in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor):
        m = act.static_mesh_component.static_mesh
        n = m.get_name() if m else ''
        for key in ('SM_S01R_ResearchCrate', 'SM_S01R_LPRock03', 'SM_S01R_LPRock05', 'SM_S01R_FarmHouse_Barrel_001',
                    'SM_S01R_Signpost', 'SM_S01R_GuideBeacon', 'SM_S01R_Bollard', 'SM_LPRock01', 'SM_LPTree01'):
            if n == key and key not in targets:
                targets[key] = act
    for key, act in sorted(targets.items()):
        o, ext = act.get_actor_bounds(True)
        start = V(o.x + ext.x + 150, o.y, L.height(o.x + ext.x + 150, o.y) + 95)
        pawn.set_actor_location(start, False, True)
        yield 5
        res_t = pawn.set_actor_location(V(o.x, o.y, start.z), True, False)
        end = pawn.get_actor_location()
        blocked = end.x > o.x + 10
        nav = unreal.NavigationSystemV1.project_point_to_navigation(world, V(o.x, o.y, o.z), None, None, V(10, 10, 300))
        hole = nav is None or (abs(nav.x) + abs(nav.y)) == 0 or abs(nav.z - (o.z + ext.z)) < 30
        check('Player blocked by ' + key, blocked, {'stop_x_from_centre': round(end.x - o.x), 'nav_hole_or_on_top': hole})
    # views for the report
    for name, (x, y, z, pitch, yaw) in {'pie_03_pond': (-1300, -300, 0, -15, -150), 'pie_04_windmill': (-1900, -1700, 0, -10, -150),
                                        'pie_05_cage': (120, -300, 0, -12, -90)}.items():
        pawn.set_actor_location(V(x, y, L.height(x, y) + 100), False, True)
        look(pc, pitch, yaw)
        yield 40
        yield from screenshot(world, name)
    # capture all three boars -> cage -> clear (save files restored afterwards)
    if '-S01NoClear' not in args:
        for b in boars:
            cc = b.get_component_by_class(unreal.CaptureComponent)
            before = mode.get_captured_boar_count()
            ok = cc.capture(None) if cc and not cc.get_editor_property('is_captured') else False
            if ok:
                mode.handle_boar_captured(b)
            t0 = time.monotonic()
            while mode.get_captured_boar_count() == before and time.monotonic() - t0 < 15:
                yield 10
            check('Capture -> cage count', ok and mode.get_captured_boar_count() == before + 1, {'before': before, 'after': mode.get_captured_boar_count()})
        t0 = time.monotonic()
        while mode.get_stage_state() != unreal.BoarStageState.CLEARED and time.monotonic() - t0 < 20:
            yield 10
        check('Stage clear after 3 in the cage', mode.get_stage_state() == unreal.BoarStageState.CLEARED,
              {'state': str(mode.get_stage_state()), 'cage_count': cage.get_captured_boar_count()})
        t0 = time.monotonic()
        while time.monotonic() - t0 < 12:
            yield 10
        widgets = sorted({w.get_class().get_name() for w in unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.UserWidget, True) if w.is_in_viewport()})
        check('Result UI shown', any('Result' in w for w in widgets), widgets)
        unreal.SystemLibrary.execute_console_command(world, 'shot showui')
        yield 60
        pc.call_method('ReturnToLobby')
        t0 = time.monotonic()
        name = ''
        while time.monotonic() - t0 < 60:
            yield 10
            w = ues.get_game_world()
            name = w.get_name() if w else ''
            if 'Lobby' in name:
                break
        check('Return to lobby travel', 'Lobby' in name, name)
    les.editor_request_end_play()
    while les.is_in_play_in_editor():
        yield 1


def restore_saves():
    if not BACKUP.exists():
        return
    current = {p.name for p in SAVES.glob('*.sav')}
    backed = {p.name for p in BACKUP.glob('*.sav')}
    for n in current - backed:
        (SAVES / n).unlink()
    for n in backed:
        shutil.copy2(BACKUP / n, SAVES / n)
    same = all((SAVES / n).read_bytes() == (BACKUP / n).read_bytes() for n in backed)
    res['values']['saves_restored'] = same
    unreal.log('[S01Check] saves restored=%s' % same)


def finish(err=None):
    if state['h'] is not None:
        unreal.unregister_slate_post_tick_callback(state['h'])
        state['h'] = None
    if les.is_in_play_in_editor():
        les.editor_request_end_play()
    restore_saves()
    res['error'] = err
    (HERE.parent / 'Logs' / 'gameplay_check.json').write_text(json.dumps(res, ensure_ascii=False, indent=1, default=str), encoding='utf-8')
    unreal.log('[S01Check] %s passed=%d failed=%d' % ('ERROR ' + err if err else 'COMPLETE', len(res['checks']), len(res['failed'])))
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
        state['wait'] = next(state['gen']) or 0
    except StopIteration:
        finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        state['busy'] = False


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['gen'] = run()
state['h'] = unreal.register_slate_post_tick_callback(tick)
