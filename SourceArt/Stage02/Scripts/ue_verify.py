"""Fresh-process verification of L_Stage02 (headless commandlet, saves nothing). Writes Logs/verify.json."""
import json, os, sys, unreal
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import s02_layout as L
ROOT = os.path.dirname(HERE)
r = {}
w = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/L_Stage02')
r['world'] = w.get_path_name()
r['gm_override'] = str(w.get_world_settings().get_editor_property('default_game_mode'))
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
acts = eas.get_all_level_actors()
cls = {}
for a in acts:
    cls[a.get_class().get_name()] = cls.get(a.get_class().get_name(), 0) + 1
r['classes'] = cls
for a in acts:
    if isinstance(a, unreal.PlayerStart):
        r['player_start'] = [round(v) for v in (a.get_actor_location().x, a.get_actor_location().y, a.get_actor_location().z, a.get_actor_rotation().yaw)]
    if isinstance(a, unreal.Cage):
        r['cage'] = [round(v) for v in (a.get_actor_location().x, a.get_actor_location().y, a.get_actor_location().z)]
r['old_cubes_left'] = [a.get_actor_label() for a in acts if a.get_actor_label().startswith('S2_')]

vs = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
st = unreal.ValidateAssetsSettings()
st.set_editor_property('skip_excluded_directories', False)
st.set_editor_property('show_if_no_failures', True)
paths = ['/Game/Level/L_Stage02', '/Game/BP/GameMode/BP_GM_Stage02', '/Game/Stage02/Meshes/SM_S02_Terrain',
         '/Game/Stage02/Meshes/SM_S02_RiverWater', '/Game/Stage02/Meshes/SM_S02_PondWater']
res = vs.validate_assets_with_settings([unreal.EditorAssetLibrary.find_asset_data(p) for p in paths], st)
d = res[1] if isinstance(res, tuple) else res
r['validation'] = {k: getattr(d, k) for k in ('num_requested', 'num_checked', 'num_valid', 'num_invalid', 'num_warnings', 'num_unable_to_validate')}

reg = unreal.AssetRegistryHelpers.get_asset_registry()
opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
deps = reg.get_dependencies('/Game/Level/L_Stage02', opts) or []
r['deps'] = sorted(str(x) for x in deps if str(x).startswith('/Game'))

pts = {'cage': L.CAGE, 'start': L.START, 'ridge_top': (3250, -1500), 'terrace': (300, 3300), 'west_bank': (450, -3300),
       'landing_NW': (L.ridge_x(L.SHORTCUTS['NW']) - 500, L.SHORTCUTS['NW']), 'landing_NE': (L.ridge_x(L.SHORTCUTS['NE']) - 500, L.SHORTCUTS['NE']),
       'top_NW': (L.ridge_x(L.SHORTCUTS['NW']) + 200, L.SHORTCUTS['NW']), 'top_NE': (L.ridge_x(L.SHORTCUTS['NE']) + 200, L.SHORTCUTS['NE']),
       'ford': L.FORD, 'bridge': L.BRIDGE}
r['traces'] = {}
for k, (x, y) in pts.items():
    hit = unreal.SystemLibrary.line_trace_single(w, unreal.Vector(x, y, 3000), unreal.Vector(x, y, -1000),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    if hit:
        t = hit.to_tuple()
        r['traces'][k] = [round(t[4].z), t[9].get_actor_label() if t[9] else None, round(L.height(x, y))]
    else:
        r['traces'][k] = None
def npath(a, b):
    pa = unreal.Vector(a[0], a[1], L.height(*a) + 60); pb = unreal.Vector(b[0], b[1], L.height(*b) + 60)
    pr = unreal.NavigationSystemV1.find_path_to_location_synchronously(w, pa, pb)
    if not pr:
        return None
    pts = pr.get_editor_property('path_points')
    if len(pts) < 2:
        return {'n': len(pts)}
    return {'len_m': round(sum((pts[i + 1] - pts[i]).length() for i in range(len(pts) - 1)) / 100, 1), 'partial': pr.is_partial(),
            'end_err': round(((pts[-1].x - b[0]) ** 2 + (pts[-1].y - b[1]) ** 2) ** .5)}
r['nav_saved_queries'] = {}
for y in (300, 500, 600, 700, 900):
    for off in (350, 550):
        top, foot = (L.ridge_x(y) + off, y), (L.ridge_x(y) - 450, y)
        r['nav_saved_queries']['%d_top%d' % (y, off)] = npath(top, foot)
json.dump(r, open(os.path.join(ROOT, 'Logs', 'verify.json'), 'w'), indent=1, default=str)
unreal.log_warning('[S02Verify] COMPLETE')
