"""AKI-175 fresh-process verification (headless, saves nothing). Logs/verify_gameplay.json.
Checks the StageConfig / GameMode / SpawnPoints and NavMesh routes from each spawn on the SAVED navmesh."""
import json, math, os, sys, unreal
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import s02_layout as L
ROOT = os.path.dirname(HERE)
R = {}
cfg = unreal.load_asset('/Game/DataAssets/StageConfig/DA_Stage02StageConfig')
R['config'] = {'StageId': str(cfg.get_editor_property('stage_id')), 'Level': cfg.get_editor_property('level').get_path_name(),
               'Target': cfg.get_editor_property('target_capture_count'), 'Unlock': str(cfg.get_editor_property('unlock_condition').get_editor_property('required_cleared_stage_id')),
               'Total': cfg.get_total_boar_spawn_count(),
               'Boars': [[str(d.get_editor_property('boar_unique_id')), d.get_editor_property('boar_class').get_name(), str(d.get_editor_property('spawn_point_id'))]
                         for d in cfg.get_editor_property('boar_spawn_definitions')]}
s1 = unreal.load_asset('/Game/DataAssets/StageConfig/DA_TestStageConfig')
R['stage1_config'] = [str(s1.get_editor_property('stage_id')), s1.get_editor_property('target_capture_count'),
                      [str(d.get_editor_property('boar_unique_id')) for d in s1.get_editor_property('boar_spawn_definitions')]]
gm = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class('/Game/BP/GameMode/BP_GM_Stage02'))
R['gm_stage02'] = gm.get_editor_property('stage_config').get_path_name()
R['gm_base'] = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class('/Game/BP/GameMode/BP_BoarGameMode')).get_editor_property('stage_config').get_path_name()

w = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/L_Stage02')
R['world'] = w.get_path_name()
R['gm_override'] = str(w.get_world_settings().get_editor_property('default_game_mode'))
vs = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
st = unreal.ValidateAssetsSettings(); st.set_editor_property('skip_excluded_directories', False); st.set_editor_property('show_if_no_failures', True)
paths = ['/Game/DataAssets/StageConfig/DA_Stage02StageConfig', '/Game/BP/GameMode/BP_GM_Stage02', '/Game/Level/L_Stage02']
res = vs.validate_assets_with_settings([unreal.EditorAssetLibrary.find_asset_data(p) for p in paths], st)
d = res[1] if isinstance(res, tuple) else res
R['validation'] = {k: getattr(d, k) for k in ('num_requested', 'num_checked', 'num_valid', 'num_invalid', 'num_warnings', 'num_unable_to_validate')}

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
pts, boars, cages, ids = {}, [], 0, {}
for a in eas.get_all_level_actors():
    if isinstance(a, unreal.BoarSpawnPoint):
        pid = str(a.get_editor_property('spawn_point_id')); l = a.get_actor_location()
        pts[pid] = (l.x, l.y, l.z); ids[pid] = ids.get(pid, 0) + 1
    if isinstance(a, unreal.BoarBase):
        boars.append(a.get_actor_label())
    if isinstance(a, unreal.Cage):
        cages += 1
R['spawn_points'] = {k: [round(v) for v in p] for k, p in pts.items()}
R['duplicate_point_ids'] = [k for k, n in ids.items() if n > 1]
R['placed_boars'] = boars
R['cages'] = cages
R['config_points_resolved'] = all(b[2] in pts for b in R['config']['Boars'])


def gz(p):
    hit = unreal.SystemLibrary.line_trace_single(w, unreal.Vector(p[0], p[1], 3000), unreal.Vector(p[0], p[1], -1000),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    return hit.to_tuple()[4].z if hit else L.height(*p)


def path(a, b, za=None):
    pa = unreal.Vector(a[0], a[1], (za if za is not None else gz(a) + 60)); pb = unreal.Vector(b[0], b[1], gz(b) + 60)
    p = unreal.NavigationSystemV1.find_path_to_location_synchronously(w, pa, pb)
    if not p or not p.get_editor_property('path_points'):
        return 'none'
    q = p.get_editor_property('path_points')
    ln = sum((q[i + 1] - q[i]).length() for i in range(len(q) - 1))
    ok = math.hypot(q[-1].x - b[0], q[-1].y - b[1]) < 150 and not p.is_partial()
    return round(ln / 100, 1) if ok else 'blocked'


R['on_navmesh'] = {}
for k, (x, y, z) in pts.items():
    pr = unreal.NavigationSystemV1.project_point_to_navigation(w, unreal.Vector(x, y, z), None, None, unreal.Vector(50, 50, 150))
    R['on_navmesh'][k] = None if pr is None else round(math.hypot(pr.x - x, pr.y - y))
T = {'cage': (L.CAGE[0] + 400, L.CAGE[1]), 'start': L.START, 'ridge_west': (3250.0, -1500.0), 'watchtower': (3000.0, 150.0),
     'ridge_east': (3250.0, 2700.0), 'east_terrace': (1300.0, 3000.0), 'west_hill': (1300.0, -3600.0), 'bridge_w': (235.0, -3080.0)}
R['routes'] = {k: {t: path((x, y), tp, z) for t, tp in T.items()} for k, (x, y, z) in pts.items()}
# shortcut misuse: straight drop distance vs. nav route length (boars must detour or be blocked)
ya, yb = L.SHORTCUTS['NW'], L.SHORTCUTS['NE']; rxa, rxb = L.ridge_x(ya), L.ridge_x(yb)
R['shortcuts'] = {'A_top>A_ledge (drop 7m)': path((rxa + 300, ya), (rxa - 420, ya)),
                  'A_top>valley (drop 14m)': path((rxa + 300, ya), (rxa - 1150, ya)),
                  'B_top>B_landing (drop 9m)': path((rxb + 300, yb), (rxb - 600, yb))}
R['ridge_descent'] = {int(y): path((L.ridge_x(y) + 350, y), (L.ridge_x(y) - 450, y)) for y in range(-2400, 2401, 600)}
json.dump(R, open(os.path.join(ROOT, 'Logs', 'verify_gameplay.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
unreal.log_warning('[S02VerifyGameplay] COMPLETE')
