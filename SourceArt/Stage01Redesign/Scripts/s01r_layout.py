"""Stage01 Redesign: single source of truth for the layout (pure Python, UE centimetres, UE axes).

Used by Blender (build_assets.py / layout_preview.py) and by UE (ue_build_level.py).
UE -> Blender conversion: (x, y, z) cm -> (x/100, -y/100, z/100) m.

Map orientation (player faces -Y at start):
  front (+Y)  start meadow           back (-Y)  research facility behind a fence
  left  (-X)  pond, stream, bridge, waterfall, windmill
  right (+X)  low hill + watchtower (special coin candidate)
"""
import math

CAGE = (120.0, -1510.0)          # BP_Cage stays where Stage1 has it
START = (120.0, 950.0)           # PlayerStart, yaw -90 (cage straight ahead)
PLAY = (-3600.0, 3600.0, -3500.0, 1500.0)   # walkable rectangle x0, x1, y0, y1
HILL = (2900.0, -1650.0)
HILL_H, HILL_TOP_R, HILL_BASE_R = 240.0, 520.0, 1400.0
TOWER = (3120.0, -1650.0)        # watchtower deck centre
TOWER_DECK_RISE = 150.0          # deck above hill top
POND = (-2350.0, -650.0, 800.0, 560.0, math.radians(-12))   # cx, cy, rx, ry, rotation
POND_DEPTH, POND_WATER_Z = 50.0, -16.0
STREAM = [(-2820, -3180), (-2720, -2650), (-2640, -2000), (-2570, -1500), (-2470, -1150)]
STREAM_W, STREAM_DEPTH, STREAM_WATER_Z = 300.0, 28.0, -10.0
BRIDGE = (-2640.0, -2000.0)      # spans along X across the stream
BRIDGE_LEN, BRIDGE_W = 600.0, 240.0
WATERFALL = (-2860.0, -3330.0)   # lip of the back-left cliff
CLIFF_TOP = 400.0
WINDMILL = (-3180.0, -2720.0)
FACILITY = (700.0, -4350.0)
FENCE_Y = -3560.0
RING = (1700.0, 1400.0)          # loop path radii around the cage
PLAZA_R = 650.0


def smoothstep(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def _hash(ix, iy, seed=0):
    n = (ix * 374761393 + iy * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def vnoise(x, y, seed=0):
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    sx, sy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = _hash(ix, iy, seed), _hash(ix + 1, iy, seed)
    c, d = _hash(ix, iy + 1, seed), _hash(ix + 1, iy + 1, seed)
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy


def fbm(x, y, seed=0):
    return (vnoise(x, y, seed) - .5) + .5 * (vnoise(2 * x, 2 * y, seed + 7) - .5)


def seg_dist(p, a, b):
    ax, ay = a; bx, by = b; px, py = p
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy or 1)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def poly_dist(p, pts):
    return min(seg_dist(p, pts[i], pts[i + 1]) for i in range(len(pts) - 1))


def ellipse_r(p, e):
    cx, cy, rx, ry, rot = e
    dx, dy = p[0] - cx, p[1] - cy
    c, s = math.cos(-rot), math.sin(-rot)
    u, v = dx * c - dy * s, dx * s + dy * c
    return math.hypot(u / rx, v / ry)


# ---------------------------------------------------------------- paths
def ring_point(theta, r=RING):
    return (CAGE[0] + r[0] * math.cos(theta), CAGE[1] + r[1] * math.sin(theta))


def ring_points(n=96):
    return [ring_point(2 * math.pi * i / n) for i in range(n + 1)]


def _deg(a):
    return math.radians(a)


PATHS = {
    # name: (points, width, z offset above terrain)
    'Main': ([START, (120, 300), (120, -820)], 320.0, 3.0),
    'Ring': (ring_points(), 250.0, 2.0),
    'SpokeWest': ([(-480, -1510), (-1580, -1510), (-1950, -1780), (-2300, -2000)], 230.0, 4.0),
    'WindmillTrail': ([(-2960, -2000), (-3120, -2300), (-3170, -2500)], 210.0, 4.0),
    'SpokeEast': ([(720, -1510), (1820, -1510), (2150, -1600), (2460, -1650)], 230.0, 4.0),
    'SpokeBack': ([(120, -2120), (120, -2910), (120, -3440)], 230.0, 4.0),
    'PondTrail': ([(120, 560), (-650, 430), (-1250, 60), ring_point(_deg(150))], 200.0, 5.0),
    'HillTrail': ([(120, 560), (880, 420), (1450, 60), ring_point(_deg(30))], 200.0, 5.0),
}
BRIDGE_ENDS = ((BRIDGE[0] + BRIDGE_LEN / 2, BRIDGE[1]), (BRIDGE[0] - BRIDGE_LEN / 2, BRIDGE[1]))


def path_dist(p):
    return min(poly_dist(p, pts) - w / 2 for pts, w, _ in PATHS.values())


# ---------------------------------------------------------------- terrain
def outside_play(x, y):
    x0, x1, y0, y1 = PLAY
    dx = max(x0 - x, x - x1, 0.0)
    dyf = max(y - y1, 0.0)
    dyb = max(y0 - y, 0.0)
    return dx, dyf, dyb


def height(x, y):
    p = (x, y)
    flat = max(1 - smoothstep(PLAZA_R + 150, PLAZA_R + 650, math.hypot(x - CAGE[0], y - CAGE[1])),
               1 - smoothstep(350, 800, math.hypot(x - START[0], y - START[1])),
               1 - smoothstep(0, 220, path_dist(p)))
    er = ellipse_r(p, POND)
    sd = poly_dist(p, STREAM)
    flat = max(flat, 1 - smoothstep(1.1, 1.6, er), 1 - smoothstep(STREAM_W, STREAM_W * 2, sd),
               1 - smoothstep(HILL_TOP_R, HILL_TOP_R + 350, math.hypot(x - HILL[0], y - HILL[1])),
               1 - smoothstep(450, 800, math.hypot(x - WINDMILL[0], y - WINDMILL[1])))
    h = fbm(x / 1500, y / 1500, 3) * 46 * (1 - flat)
    # low hill with a flat top for the watchtower
    h += HILL_H * (1 - smoothstep(HILL_TOP_R, HILL_BASE_R, math.hypot(x - HILL[0], y - HILL[1])))
    # natural berms on the left, right and front; back stays open for the facility yard
    dx, dyf, dyb = outside_play(x, y)
    d = math.hypot(dx, dyf)
    if d > 0:
        h += 520 * smoothstep(0, 900, d) + 90 * fbm(x / 700, y / 700, 11) * smoothstep(0, 400, d)
    # back-left cliff the waterfall drops from (steep on purpose: not walkable)
    cliff = smoothstep(-2150, -2550, x) * smoothstep(-3230, -3420, y)
    h += (CLIFF_TOP - h) * cliff if cliff > 0 else 0
    if dyb > 0 and x > -2300:
        h += 25 * fbm(x / 900, y / 900, 5)   # facility yard, gentle
    # water: pond basin and stream channel
    if er < 1.25:
        h -= POND_DEPTH * (1 - smoothstep(0.72, 1.18, er))
    if sd < STREAM_W and cliff < 0.5:
        h -= STREAM_DEPTH * (1 - smoothstep(STREAM_W * 0.3, STREAM_W * 0.75, sd))
    return h


def surface(x, y):
    """Material zone for a terrain face centre: Grass, GrassLight, Cliff, Shore."""
    h = height(x, y)
    if ellipse_r((x, y), POND) < 1.12 or (poly_dist((x, y), STREAM) < STREAM_W * 0.7 and y > -3250):
        return 'Shore'
    if h > 0 and (x < -2150 and y < -3150):
        return 'Cliff'
    n = fbm(x / 900, y / 900, 21)
    hill = math.hypot(x - HILL[0], y - HILL[1]) < HILL_TOP_R + 120
    return 'GrassLight' if (n > 0.18 or hill) else 'Grass'


# ---------------------------------------------------------------- gameplay actors
SPAWN_POINTS = {
    # Stage01_Spawn_001..003 are referenced by DA_TestStageConfig; 004/005 kept as spare points.
    'Stage01_Spawn_001': (560.0, 260.0, -120.0),     # first boar: right in front of the start, open meadow
    'Stage01_Spawn_002': (-1150.0, -380.0, -60.0),   # pond side
    'Stage01_Spawn_003': (1450.0, -2650.0, 150.0),   # back right, hill foot
    'Stage01_Spawn_004': (-1050.0, -2750.0, 30.0),   # back left, near the stream
    'Stage01_Spawn_005': (2250.0, -350.0, -150.0),   # front right, hill foot
    'Stage01_CoinSpawn_001': (TOWER[0] + 40.0, TOWER[1], 180.0),   # watchtower deck (special coin candidate)
}
