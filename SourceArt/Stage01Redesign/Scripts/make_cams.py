"""Writes cams_after.json for capture_level.py: name -> [x, y, z, pitch, yaw, fov] (UE)."""
import json, math
VIEWS = {
    'overview_top': ((120, -1300, 9000), (120, -1301, 0), 70),
    'overview_oblique': ((120, 4300, 4300), (120, -1700, 0), 70),
    'start_view': ((120, 1300, 260), (120, -1510, 120), 80),
    'from_cage': ((120, -1050, 420), (-2300, -1500, 0), 85),
    'water_bridge': ((-1500, -1200, 380), (-2700, -2500, 60), 85),
    'hill_tower': ((1300, -400, 450), (3100, -1700, 250), 80),
    'from_tower': ((3060, -1650, 560), (100, -1500, 0), 85),
    'windmill': ((-1300, -1900, 330), (-3180, -2720, 450), 75),
    'back_to_start': ((120, -2700, 420), (120, 900, 60), 80),
}
out = {}
for k, (p, t, fov) in VIEWS.items():
    d = [t[i] - p[i] for i in range(3)]
    yaw = math.degrees(math.atan2(d[1], d[0]))
    pitch = math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
    out[k] = [p[0], p[1], p[2], round(pitch, 2), round(yaw, 2), fov]
json.dump(out, open(__file__.replace('make_cams.py', 'cams_after.json'), 'w'), indent=1)
