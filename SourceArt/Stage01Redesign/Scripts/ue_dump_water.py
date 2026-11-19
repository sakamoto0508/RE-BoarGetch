"""Headless: dump water-related actors of L_Stage01 and check NavMesh in the pond / stream. Writes Logs/water_dump.json."""
import json, os, sys, unreal
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import s01r_layout as L
w = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/L_Stage01')
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
out = {'world': w.get_path_name(), 'actors': []}
for a in eas.get_all_level_actors():
    lab = a.get_actor_label()
    mesh = ''
    if isinstance(a, unreal.StaticMeshActor) and a.static_mesh_component.static_mesh:
        mesh = a.static_mesh_component.static_mesh.get_name()
    key = (lab + mesh).lower()
    if any(k in key for k in ('water', 'bridge', 'pond', 'stream', 'block')):
        l = a.get_actor_location(); r = a.get_actor_rotation(); s = a.get_actor_scale3d()
        o, e = a.get_actor_bounds(False)
        out['actors'].append({'label': lab, 'mesh': mesh, 'cls': a.get_class().get_name(), 'loc': [round(l.x), round(l.y), round(l.z)],
                              'yaw': round(r.yaw, 1), 'scale': [round(s.x, 2), round(s.y, 2), round(s.z, 2)],
                              'bounds_o': [round(o.x), round(o.y), round(o.z)], 'bounds_e': [round(e.x), round(e.y), round(e.z)]})
pts = {'pond_center': L.POND[:2], 'pond_e': (L.POND[0] + 500, L.POND[1] - 100), 'pond_w': (L.POND[0] - 500, L.POND[1] + 100),
       'stream_a': (-2720, -2650), 'stream_b': (-2570, -1500), 'stream_c': (-2470, -1150), 'bridge': L.BRIDGE}
nav = {}
for k, (x, y) in pts.items():
    hits = []
    for z in (-60, 0, 40, 100):
        p = unreal.NavigationSystemV1.project_point_to_navigation(w, unreal.Vector(x, y, z), None, None, unreal.Vector(30, 30, 30))
        if p is not None and (abs(p.x) + abs(p.y)) > 0:
            hits.append([z, round(p.z)])
    nav[k] = {'ground': round(L.height(x, y)), 'nav_hits': hits}
out['nav'] = nav
json.dump(out, open(os.path.join(os.path.dirname(HERE), 'Logs', 'water_dump.json'), 'w'), indent=1)
unreal.log_warning('[S01Water] dump done %d actors' % len(out['actors']))
