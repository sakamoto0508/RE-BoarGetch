"""Headless re-check of the key NavMesh routes on the SAVED navmesh (no build, saves nothing). Logs/routes_check.json."""
import json, math, os, sys, unreal
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import s02_layout as L
w = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/L_Stage02')
def path(a, b):
    pa = unreal.Vector(a[0], a[1], L.height(*a) + 60); pb = unreal.Vector(b[0], b[1], L.height(*b) + 60)
    p = unreal.NavigationSystemV1.find_path_to_location_synchronously(w, pa, pb)
    if not p:
        return 'none'
    pts = p.get_editor_property('path_points')
    if not pts:
        return 'none'
    ln = sum((pts[i + 1] - pts[i]).length() for i in range(len(pts) - 1))
    reached = math.hypot(pts[-1].x - b[0], pts[-1].y - b[1]) < 150
    return round(ln / 100, 1) if (reached and not p.is_partial()) else 'blocked'
ya, yb = L.SHORTCUTS['NW'], L.SHORTCUTS['NE']; rxa, rxb = L.ridge_x(ya), L.ridge_x(yb)
P = {'cage': L.CAGE, 'start': L.START, 'ridge_top': (3250.0, -1500.0), 'ridge_east': (3250.0, 2700.0), 'terrace': (300.0, 3300.0),
     'A_top': (rxa + 300, ya), 'A_ledge': (rxa - 420, ya), 'landing_NW': (rxa - 1150, ya), 'B_top': (rxb + 300, yb),
     'landing_NE': (rxb - 600, yb), 'east_mid': (1300.0, 3000.0), 'watchtower': (3000.0, 150.0), 'west_hill': (1000.0, -4000.0)}
pairs = ['cage>ridge_top', 'cage>ridge_east', 'cage>terrace', 'cage>west_hill', 'start>cage', 'A_top>A_ledge', 'A_ledge>landing_NW',
         'A_top>cage', 'B_top>landing_NE', 'B_top>east_mid', 'watchtower>cage', 'watchtower>east_mid', 'east_mid>ridge_east']
r = {k: path(P[k.split('>')[0]], P[k.split('>')[1]]) for k in pairs}
r['ridge_descent'] = {int(y): path((L.ridge_x(y) + 350, y), (L.ridge_x(y) - 450, y)) for y in range(-2400, 2401, 600)}
json.dump(r, open(os.path.join(os.path.dirname(HERE), 'Logs', 'routes_check.json'), 'w'), indent=1)
unreal.log_warning('[S02Routes] ' + json.dumps(r))
