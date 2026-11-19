"""Stage 2 "段丘牧場" layout: single source of truth (pure Python, UE centimetres, UE axes).

Used by Blender (build_terrain.py / preview) and by UE (ue_build_level.py, s02_dressing.py).
Axes: +X = north, +Y = east.  UE -> Blender: (x, y, z) cm -> (x/100, -y/100, z/100) m.

Areas (play area 75 m N-S x 90 m E-W):
  centre  valley meadow around the Cage (height ~0)
  south   farm plateau (+70) with PlayerStart, barn, paddock; paths fork west / north / north-east
  west    S-curved shallow river with a bridge and a ford, woods, gentle hills rising to ~+230
  east    broad irregular terrace (+140) for Red's charge; reached by gentle ramps from the farm and the valley
  north   long curved ridge (+330) for Blue; cliff face toward the valley with two drop-down shortcuts,
          gentle trails at the west end (from the hills) and the east end (from the terrace)
  outside hills rise beyond an invisible boundary; background mountains far away
"""
import math

PLAY = (-3750.0, 3750.0, -4500.0, 4500.0)        # x0, x1, y0, y1 (walkable, invisible walls just outside)
TERRAIN = (-7300.0, 7300.0, -8100.0, 8100.0)     # terrain mesh extent
GRID = 150.0

CAGE = (300.0, 250.0)
START = (-3050.0, -350.0)
START_YAW = math.degrees(math.atan2(CAGE[1] - START[1], CAGE[0] - START[0]))

# Heights (2026-11-19 high-ground pass). Player: jump 700 cm/s (single ~250 cm, double ~500 cm), AirControl 0.35,
# no fall damage. Every player drop is kept at ~3.2 m: shortcut A uses an intermediate ledge, B lands on a bench.
RIDGE_H = 650.0
TERRACE_H = 260.0
TERRACE_BENCH = 70.0     # the terrace rises to ~330 at the ridge foot (shortcut B landing)
WEST_HILL_H = 420.0
FARM_H = 70.0
LEDGE_H = 330.0          # shortcut A intermediate ledge (player-only island on the cliff)
LEDGE_D = 650.0          # ledge depth from the cliff top edge (walk-off at 5 m/s travels ~4 m in a 0.8 s fall)
LEDGE_HALF = 550.0

# knolls (x, y, radius, height): valley sightline breakers; the windmill knoll on the east terrace
KNOLLS = [(-1150.0, 1250.0, 750.0, 85.0), (950.0, -1650.0, 600.0, 70.0), (-1700.0, -2000.0, 650.0, 60.0)]
TERRACE_KNOLLS = [(-450.0, 3650.0, 850.0, 110.0)]

# player drop-down shortcuts on the ridge cliff (y centre, half width)
#   NW = shortcut A: ridge -> ledge -> central valley (Cage defence, boars fleeing into the valley)
#   NE = shortcut B: ridge -> east terrace bench (cut off Blue / boars running along the east side)
SHORTCUTS = {'NW': -1300.0, 'NE': 2000.0}
SHORTCUT_HALF = 380.0

# river (south-flowing, from a spring under the west end of the ridge into the SW pond)
RIVER = [(2150, -2350), (1550, -2700), (800, -2450), (0, -2780), (-800, -3080), (-1500, -2860), (-2100, -3120), (-2500, -3420)]
RIVER_W, RIVER_DEPTH = 420.0, 42.0
POND = (-2800.0, -3650.0, 760.0, 560.0, math.radians(25))     # cx, cy, rx, ry, rot
POND_DEPTH = 55.0
# 2026-11-19: the pond has a fixed water level below its lowest rim (the farm side), and the bed is dug from that level,
# so the water never hangs above the bank when seen from the side
POND_WATER_Z = -25.0

# west S-curve trail crosses the river on the bridge; the valley trail fords it
# 2026-11-19 user edit: the bridge was moved by hand to the old ford crossing (only crossing; the water is impassable)
BRIDGE = (170.0, -2730.0)
BRIDGE_Z = 0.0
BRIDGE_YAW = 101.0
FORD = BRIDGE     # legacy name (the ford is gone; kept for scripts that still refer to it)

LANDMARKS = {
    'Barn': (-3150.0, -1750.0),
    'FarmHouse': (-3320.0, 1450.0),
    'Windmill': (-450.0, 3650.0),
    'Watchtower': (3250.0, 150.0),
}


# ---------------------------------------------------------------- helpers
def smoothstep(e0, e1, x):
    if e0 == e1:
        return 1.0 if x >= e1 else 0.0
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return a + (b - a) * t


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


def chaikin(pts, it=3):
    for _ in range(it):
        out = [pts[0]]
        for i in range(len(pts) - 1):
            p, q = pts[i], pts[i + 1]
            out += [(p[0] * .75 + q[0] * .25, p[1] * .75 + q[1] * .25), (p[0] * .25 + q[0] * .75, p[1] * .25 + q[1] * .75)]
        out.append(pts[-1])
        pts = out
    return pts


RIVER_S = chaikin(RIVER, 3)


# ---------------------------------------------------------------- big shapes
def ridge_x(y):
    """South edge (cliff top) of the north ridge."""
    return 2550 + 260 * math.sin((y + 600) / 1700) + 120 * math.sin((y - 300) / 700)


def ridge_face_w(y):
    """Horizontal width of the ridge's south face: rock cliff (150) to steep grass bank (360, still > 50 deg in the
    middle so boars cannot climb), forced to a cliff at the shortcuts, long gentle trails at both ends."""
    # The terrain is sampled on a 150 cm grid, so a step spreads over 1-2 cells. Keeping the face <= 220 cm wide and
    # the full ridge height keeps every face triangle above the 44.7 deg walkable limit (verified by nav descent checks).
    # 650 cm over the valley: up to 400 cm wide stays > 47 deg even when spread over 3-4 grid cells (rock + bank mix).
    w = 220.0 + 180.0 * smoothstep(0.35, 0.65, vnoise(y / 900.0, 3.7, 41))
    if y > 1300:            # above the east terrace bench the face is only ~320 cm: keep it narrow
        w = min(w, lerp(400.0, 190.0, smoothstep(1300, 1500, y)))
    for c in SHORTCUTS.values():
        w = lerp(w, 170.0, 1 - smoothstep(SHORTCUT_HALF, SHORTCUT_HALF + 500, abs(y - c)))
    return w


def ledge_mask(y):
    return 1 - smoothstep(LEDGE_HALF, LEDGE_HALF + 250, abs(y - SHORTCUTS['NW']))


TRAIL_W = 2000.0


def trail_mask(y):
    """1 where the ridge face is a gentle trail (west end from the hills, east end from the terrace)."""
    # east trailhead kept ~10 m east of shortcut B so the boar route from B's top to the bench is a real detour
    return max(smoothstep(-2600, -3300, y), smoothstep(3000, 3600, y))


def terrace_y(x):
    """West edge of the east terrace."""
    return 1500 + 380 * math.sin((x + 500) / 1100) + 150 * math.sin(x / 430)


def terrace_edge_w(x):
    w = 105.0 + 70.0 * smoothstep(0.35, 0.65, vnoise(x / 700.0, 1.3, 43))   # rock step / steep bank, > 50 deg
    w = lerp(w, 1000.0, smoothstep(-2000, -2500, x))        # south: gentle from the farm
    for c in (1000.0,):                                    # valley ramp
        w = lerp(w, 1100.0, 1 - smoothstep(250, 450, abs(x - c)))
    return w


def farm_x(y):
    return -2150 + 220 * math.sin(y / 1250)


def west_y(x):
    return -3150 + 260 * math.sin(x / 1300 + 0.6)


# ---------------------------------------------------------------- paths (terrain material zone, slightly dug in)
PATHS = {
    # name: (control points, width)
    'Main': ([START, (-2300, -120), (-1300, 120), (-450, 230)], 300.0),
    # east bank: start -> along the river to the bridge (no crossing at the old bridge spot any more)
    'WestS': ([START, (-2750, -1150), (-2250, -2000), (-1500, -2350), (-700, -2450), (120, -2420)], 260.0),
    # west bank: bridge -> woods -> west trail up the ridge; a short branch south into the woods by the pond
    'WestN': ([(235, -3080), (700, -3250), (1250, -3850), (1900, -3500), (2600, -4000), (3150, -3700)], 260.0),
    'WestBank': ([(235, -3080), (-300, -3380), (-900, -3500), (-1600, -3600)], 220.0),
    # the ridge top keeps only short worn trails near the trail heads and the watchtower; the rest stays grass
    'RidgeW': ([(3150, -3700), (3050, -2900), (3200, -2200)], 220.0),
    'RidgeMid': ([(3000, -350), (3250, 150), (3150, 700)], 200.0),
    'RidgeE': ([(3300, 2300), (3150, 3000), (3150, 3550)], 220.0),
    'EastLoop': ([(3150, 3550), (2400, 3950), (1700, 3500), (1000, 3750), (500, 3200), (-300, 2700), (-1300, 2950), (-2200, 2050),
                  (-2700, 900), (-2950, 200), START], 270.0),
    'ValleyEast': ([(700, 700), (950, 1250), (1050, 1800), (1100, 2400), (900, 3000)], 230.0),
    'ValleyWest': ([(150, -450), (250, -1300), (130, -2150), (120, -2420)], 220.0),
    'Barnyard': ([(-2750, -1150), (-3050, -1550)], 220.0),
}
PATHS_S = {k: (chaikin(list(p), 3), w) for k, (p, w) in PATHS.items()}


def path_dist(p):
    return min(poly_dist(p, pts) - w / 2 for pts, w in PATHS_S.values())


# ---------------------------------------------------------------- terrain
def outside_dist(x, y):
    x0, x1, y0, y1 = PLAY
    dx = max(x0 - x, x - x1, 0.0)
    dy = max(y0 - y, y - y1, 0.0)
    return math.hypot(dx, dy)


def base_height(x, y):
    """Height before the river/pond/path carving."""
    p = (x, y)
    # valley floor: rolling ground and a few knolls that break the sightlines
    h = 45 * fbm(x / 1600, y / 1600, 3)
    for kx, ky, kr, kh in KNOLLS:
        h += kh * (1 - smoothstep(kr * 0.25, kr, math.hypot(x - kx, y - ky)))
    flat = max(1 - smoothstep(900, 1500, math.hypot(x - CAGE[0], y - CAGE[1])),
               1 - smoothstep(0, 260, path_dist(p)))
    h *= (1 - flat)
    # south farm plateau (gentle rise from the valley)
    t = smoothstep(farm_x(y) + 500, farm_x(y) - 500, x)
    h = lerp(h, FARM_H + 18 * fbm(x / 1000, y / 1000, 5) * (1 - smoothstep(0, 600, -path_dist(p) + 600)), t)
    # east terrace
    w = terrace_edge_w(x)
    t = smoothstep(-w / 2, w / 2, y - terrace_y(x))
    # the terrace rises gently into a bench at the ridge foot (shortcut B lands here, ~3.2 m below the ridge)
    th = TERRACE_H + 30 * fbm(x / 1300, y / 1300, 9) + TERRACE_BENCH * smoothstep(-1600, -500, x - ridge_x(y))
    for kx, ky, kr, kh in TERRACE_KNOLLS:
        th += kh * (1 - smoothstep(kr * 0.3, kr, math.hypot(x - kx, y - ky)))
    h = lerp(h, th, t)
    # west hills
    d = west_y(x) - y
    hw = WEST_HILL_H * smoothstep(-200, 1700, d) + 30 * fbm(x / 900, y / 900, 13) * smoothstep(0, 400, d)
    h = max(h, hw) if d > -200 else h
    # north ridge (with lowered notches at the shortcuts so they read as natural gaps)
    dx = x - ridge_x(y)
    t = lerp(smoothstep(-ridge_face_w(y), 0, dx), smoothstep(-TRAIL_W, 0, dx), trail_mask(y))
    top =RIDGE_H + 45 * fbm(x / 1100, y / 1100, 17) + 40 * math.sin(y / 1900 + 1.0) + 60 * smoothstep(3300, 4200, x)
    for c in SHORTCUTS.values():
        notch = (1 - smoothstep(SHORTCUT_HALF * 0.7, SHORTCUT_HALF * 1.3, abs(y - c))) * (1 - smoothstep(150, 650, x - ridge_x(y)))
        top -= 45 * notch
    h_ridge = lerp(h, top, t)
    lm = ledge_mask(y)
    if lm > 0:
        # shortcut A: cliff top -> flat ledge (LEDGE_H, LEDGE_D deep) -> cliff -> valley
        lower = lerp(h, LEDGE_H, smoothstep(-LEDGE_D - 170, -LEDGE_D, dx))
        h_ledge = lerp(lower, top, smoothstep(-170, 0, dx))
        h_ridge = lerp(h_ridge, h_ledge, lm)
    h = h_ridge
    # outside the play area: hills that close the view (start rising 5 m beyond the invisible wall)
    d = outside_dist(x, y)
    if d > 0:
        h += (650 + 350 * vnoise(x / 2600, y / 2600, 23)) * smoothstep(500, 2800, d) + 260 * fbm(x / 900, y / 900, 21) * smoothstep(300, 1400, d)
    return h


def river_depth(x, y):
    d = poly_dist((x, y), RIVER_S)
    return RIVER_DEPTH * (1 - smoothstep(RIVER_W * 0.25, RIVER_W * 0.7, d))


def pond_bed(x, y, h):
    """Dig the pond from its fixed water level: bank lip just above the water at the rim, bed POND_DEPTH below."""
    er = ellipse_r((x, y), POND)
    if er >= 1.4:
        return h
    bed = POND_WATER_Z + 10 - (POND_DEPTH + 10) * (1 - smoothstep(0.7, 1.08, er))
    return min(h, lerp(bed, max(h, POND_WATER_Z + 10), smoothstep(1.08, 1.4, er)))


def height(x, y):
    h = base_height(x, y)
    # bridge abutments: flatten the banks where the bridge sits
    bd = math.hypot(x - BRIDGE[0], y - BRIDGE[1])
    if bd < 700:
        h = lerp(BRIDGE_Z, h, smoothstep(320, 700, bd))
    h -= river_depth(x, y)
    h = pond_bed(x, y, h)
    if path_dist((x, y)) < 0:
        h -= 6.0
    return h


def water_z(x, y):
    """Water surface height near a river/pond point (banks minus a lip)."""
    if ellipse_r((x, y), POND) < 1.2:
        return POND_WATER_Z
    return base_height(x, y) - 14


def surface(x, y):
    """Material zone for a flat-enough terrain face: Grass, GrassLight, Shore, Path."""
    if poly_dist((x, y), RIVER_S) < RIVER_W * 0.62 or ellipse_r((x, y), POND) < 1.1:
        return 'Shore'
    if path_dist((x, y)) < 0:
        return 'Path'
    if math.hypot(x - LANDMARKS['Barn'][0] - 250, y - LANDMARKS['Barn'][1] - 300) < 520 + 120 * fbm(x / 300, y / 300, 2):
        return 'Path'       # barnyard dirt
    n = fbm(x / 1700, y / 1700, 31)
    h = base_height(x, y)
    light = n > 0.12 or (h > RIDGE_H - 60 and x > 2000) or (y > terrace_y(x) + 300 and n > -0.05)
    return 'GrassLight' if light else 'Grass'


# ---------------------------------------------------------------- gameplay reference points (not spawned in Phase A)
SPAWN_HINTS = {   # Notion §3 roles; used only to keep these spots clear and reachable
    'Normal1_WestHigh': (1300.0, -3600.0),
    'Normal2_SouthWest': (-1900.0, -1500.0),
    'Red_East': (300.0, 3300.0),
    'Blue_NorthEast': (3300.0, 2600.0),
}
