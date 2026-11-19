"""Headless check on the saved NavMesh: is there navmesh in the river / pond, and can a path reach into the water?"""
import json, math, os, sys, unreal
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import s02_layout as L
w = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/L_Stage02')
r = {}
pts = {'pond_center': L.POND[:2], 'pond_n': (L.POND[0] + 300, L.POND[1] + 140), 'pond_s': (L.POND[0] - 300, L.POND[1] - 140),
       'river_mid': (800.0, -2450.0), 'river_old_bridge': (-1500.0, -2860.0), 'river_south': (-2300.0, -3250.0)}
for k, (x, y) in pts.items():
    res = []
    for z in (-60, 0, 40, 80, 150, 330):
        p = unreal.NavigationSystemV1.project_point_to_navigation(w, unreal.Vector(x, y, z), None, None, unreal.Vector(30, 30, 30))
        if p is not None and (abs(p.x) + abs(p.y)) > 0:
            res.append([z, round(p.x), round(p.y), round(p.z)])
    r[k] = {'ground': round(L.height(x, y)), 'water': round(L.water_z(x, y)), 'nav_hits': res}
json.dump(r, open(os.path.join(os.path.dirname(HERE), 'Logs', 'water_nav.json'), 'w'), indent=1)
unreal.log_warning('[S02Water] %s' % json.dumps(r))
