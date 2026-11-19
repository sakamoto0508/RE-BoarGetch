"""Stage 2 set dressing: every placed mesh in UE coordinates (pure Python, deterministic).

Sources (all existing project assets, originals untouched):
  LP  LP River Forest (trees, pines, bushes, rocks, cliff rocks, fences, logs, plants, background mountains)
  FH  Farm House (barn, farmhouse, cart, well, barrels, crates, logs)
  RD  Stage01Redesign (windmill, watchtower, wooden bridge, signposts, cage plaza, bollards, beacons, research crates,
      and the collision-fixed copies of LP rocks / farm props in Meshes/Reused)
"""
import math
from s02_layout import *  # noqa: F401,F403
from s02_layout import _hash

LP = '/Game/InportAssets/LPRiverForest/Meshes/'
FH = '/Game/InportAssets/Farm_House/Meshes/'
RD = '/Game/InportAssets/Stage01Redesign/Meshes/'
S2 = '/Game/Stage02/Meshes/'
TREES = ['Trees/SM_LPTree01', 'Trees/SM_LPTree02', 'Trees/SM_LPTree03', 'Trees/SM_LPTree04', 'Trees/SM_LPTree05']
PINES = ['Trees/SM_LPPineTree01', 'Trees/SM_LPPineTree02', 'Trees/SM_LPPineTree03']
# pack meshes whose simple collision does not stop characters -> Stage 1's collision-fixed copies
REUSED = {LP + 'Rocks/SM_LPRock03': RD + 'Reused/SM_S01R_LPRock03', LP + 'Rocks/SM_LPRock04': RD + 'Reused/SM_S01R_LPRock04',
          LP + 'Rocks/SM_LPRock05': RD + 'Reused/SM_S01R_LPRock05', LP + 'Rocks/SM_LPRock08': RD + 'Reused/SM_S01R_LPRock08',
          LP + 'Cliffs/SM_LPCliffRock01': RD + 'Reused/SM_S01R_LPCliffRock01',
          FH + 'KitBash/SM_FarmHouse_Barrel_001': RD + 'Reused/SM_S01R_FarmHouse_Barrel_001',
          FH + 'KitBash/SM_FarmHouse_Bucket_001': RD + 'Reused/SM_S01R_FarmHouse_Bucket_001',
          FH + 'KitBash/SM_FarmHouse_WoodenCrate_001': RD + 'Reused/SM_S01R_FarmHouse_WoodenCrate_001',
          FH + 'KitBash/SM_FarmHouse_Logs_Pyramid_001': RD + 'Reused/SM_S01R_FarmHouse_Logs_Pyramid_001'}


def _r(i, s):
    return _hash(i, s, 4321)


def landing_zone(x, y, margin=0.0):
    for c in SHORTCUTS.values():
        rx = ridge_x(c)
        reach = 1500 if c == SHORTCUTS['NW'] else 1100    # A: ledge + valley landing, B: terrace bench
        if rx - reach - margin < x < rx + 40 and abs(y - c) < max(SHORTCUT_HALF, LEDGE_HALF) + 250 + margin:
            return True
    return False


def clear_of_gameplay(x, y, margin=0.0):
    """True when a solid prop at (x, y) keeps paths, the cage clearing, the start, landings, water and sightlines free."""
    if outside_dist(x, y) > 0:
        return True
    if path_dist((x, y)) < 170 + margin:
        return False
    if math.hypot(x - CAGE[0], y - CAGE[1]) < 1000 + margin or math.hypot(x - START[0], y - START[1]) < 700 + margin:
        return False
    if landing_zone(x, y, margin):
        return False
    if any(math.hypot(x - p[0], y - p[1]) < 450 for p in SPAWN_HINTS.values()):
        return False
    if poly_dist((x, y), RIVER_S) < RIVER_W * 0.7 or ellipse_r((x, y), POND) < 1.2:
        return False
    if math.hypot(x - BRIDGE[0], y - BRIDGE[1]) < 550:
        return False
    # start -> cage sightline corridor
    ax, ay = START; bx, by = CAGE
    if seg_dist((x, y), (ax, ay), (bx, by)) < 450:
        return False
    # ridge cliff edge band stays for the edge rocks only
    if abs(x - ridge_x(y)) < 120 and trail_mask(y) < 0.5:
        return False
    for k, (lx, ly) in LANDMARKS.items():
        if math.hypot(x - lx, y - ly) < 700:
            return False
    return True


def flower_cluster(cx, cy, seed, n=5, spread=150.0):
    """A loose clump: uneven radii and angles, mixed species/scales, a couple of grass tufts. No straight lines."""
    out = []
    main = 'Plants/SM_LPFlower0%d' % (1 + int(_r(seed, 9) > .5))
    for j in range(n):
        a = 2 * math.pi * _r(seed, 10 + j)
        r = spread * (0.15 + 0.85 * _r(seed, 30 + j) ** 0.7)
        x, y = cx + r * math.cos(a), cy + r * math.sin(a)
        if path_dist((x, y)) < 60 or poly_dist((x, y), RIVER_S) < RIVER_W * 0.62 or ellipse_r((x, y), POND) < 1.15:
            continue
        m = main if _r(seed, 50 + j) < .7 else ('Plants/SM_LPFlower01' if main.endswith('2') else 'Plants/SM_LPFlower02')
        out.append((LP + m, x, y, 360 * _r(seed, 70 + j), 1.0 + .8 * _r(seed, 90 + j)))
    for j in range(2):
        a = 2 * math.pi * _r(seed, 110 + j)
        x, y = cx + spread * 1.1 * math.cos(a), cy + spread * 1.1 * math.sin(a)
        if path_dist((x, y)) > 60:
            out.append((LP + 'Plants/SM_LPGrass0%d' % (j + 2), x, y, 360 * _r(seed, 120 + j), 1.6 + .8 * _r(seed, 130 + j)))
    return out


# where flowers naturally gather: sunny meadow patches near tree/rock edges, field margins, the farm, the windmill knoll
FLOWER_SPOTS = [(-2600, -900), (-2350, 600), (-3150, 2300), (-1150, 1700), (-500, -1100), (1150, -1350),
                (700, 1300), (-650, 3150), (1700, 2600), (-300, -3700), (1700, -3800), (3350, 1300)]


def natural_flowers():
    out = []
    for i, (fx, fy) in enumerate(FLOWER_SPOTS):
        jx, jy = (_r(i, 201) - .5) * 400, (_r(i, 202) - .5) * 400
        cx, cy = fx + jx, fy + jy
        if path_dist((cx, cy)) < 150 or math.hypot(cx - CAGE[0], cy - CAGE[1]) < 700:
            continue
        out += flower_cluster(cx, cy, 500 + i, n=2 + int(_r(i, 203) * 3), spread=110 + 90 * _r(i, 204))
    return out


def placements():
    """List of dicts: mesh, x, y, z, yaw, scale (3), collision ('default'|'none'), tag."""
    out = []

    def put(mesh, x, y, z=None, yaw=0.0, s=1.0, col='default', tag='Decor', zoff=0.0):
        zz = (height(x, y) + zoff) if z is None else z
        if col != 'none':
            mesh = REUSED.get(mesh, mesh)
        out.append({'mesh': mesh, 'x': round(x, 1), 'y': round(y, 1), 'z': round(zz, 1), 'yaw': round(yaw % 360, 1),
                    'scale': list(s) if isinstance(s, (list, tuple)) else [s, s, s], 'collision': col, 'tag': tag})

    # ---------------------------------------------------------------- ground (world-space pivots)
    put(S2 + 'SM_S02_Terrain', 0, 0, 0, tag='Ground')
    put(S2 + 'SM_S02_RiverWater', 0, 0, 0, col='none', tag='Water')
    put(S2 + 'SM_S02_PondWater', 0, 0, 0, col='none', tag='Water')

    # ---------------------------------------------------------------- cage clearing (small research footing only)
    cx, cy = CAGE
    put(RD + 'SM_S01R_CagePlaza', cx, cy, height(cx, cy) - 1, s=(0.62, 0.62, 1.0), tag='CageArea')
    # (bollards around the cage removed by the user 2026-11-19)
    for i, (dx, dy, yaw, z) in enumerate(((-620, 520, 25, 0), (-560, 640, 70, 0), (-600, 580, 40, 78))):
        x, y = cx + dx, cy + dy
        put(RD + 'SM_S01R_ResearchCrate', x, y, height(x, y) + z, yaw=yaw, s=0.85 if z else 1.0, tag='CageArea')
    # (guide beacons on the start -> cage path removed by the user 2026-11-19)

    # ---------------------------------------------------------------- south farm (start area)
    bx, by = LANDMARKS['Barn']
    put(FH + 'House_Samples/SM_FarmHouse_Barn_Sample_001', bx, by, height(bx, by) - 8, yaw=55, s=1.35, tag='Farm')
    hx, hy = LANDMARKS['FarmHouse']
    put(FH + 'House_Samples/SM_FarmHouse_MediumHouse_Sample_001', hx, hy, height(hx, hy) - 10, yaw=200, tag='Farm')
    yard = [(FH + 'KitBash/SM_FarmHouse_Cart_001', 330, 420, 35, 0), (FH + 'KitBash/SM_FarmHouse_Barrel_001', 520, -120, 0, 0),
            (FH + 'KitBash/SM_FarmHouse_Barrel_001', 600, -40, 30, 0), (FH + 'KitBash/SM_FarmHouse_WoodenCrate_001', 540, 80, 10, 0),
            (FH + 'KitBash/SM_FarmHouse_Logs_Pyramid_001', -100, 520, 80, 0), (FH + 'KitBash/SM_FarmHouse_Bucket_001', 470, -200, 0, 0),
            (FH + 'KitBash/SM_FarmHouse_Crate_001', 420, 160, 60, 0)]
    for m, dx, dy, yaw, _ in yard:
        put(m, bx + dx, by + dy, yaw=yaw, tag='Farm', zoff=-2)
    put(FH + 'KitBash/SM_FarmHouse_Well_001', -3350, 600, yaw=30, tag='Farm', zoff=-4)
    for m, dx, dy, yaw in ((FH + 'KitBash/SM_FarmHouse_Barrel_001', 520, -380, 0), (FH + 'KitBash/SM_FarmHouse_WoodenCrate_001', 600, -260, 20)):
        put(m, hx + dx, hy + dy, yaw=yaw, tag='Farm', zoff=-2)
    # paddock east of the farmhouse (LP fence, gate gap toward the east loop path)
    px0, px1, py0, py1 = -3550.0, -2850.0, 2150.0, 3150.0
    seg = 166.0 * 1.1

    def fence_line(ax, ay, bx2, by2, gap=None):
        n = max(1, int(round(math.hypot(bx2 - ax, by2 - ay) / seg)))
        yaw = math.degrees(math.atan2(by2 - ay, bx2 - ax))
        for i in range(n):
            t = (i + 0.5) / n
            x, y = ax + (bx2 - ax) * t, ay + (by2 - ay) * t
            if gap and math.hypot(x - gap[0], y - gap[1]) < 200:
                continue
            put(LP + 'HeroProps/SM_LPFence01', x, y, yaw=yaw, s=1.1, tag='Fence', zoff=-3)
    fence_line(px0, py0, px1, py0)
    fence_line(px1, py0, px1, py1, gap=(px1, (py0 + py1) / 2))
    fence_line(px1, py1, px0, py1)
    fence_line(px0, py1, px0, py0)
    # short fence runs along the first metres of the paths out of the start (farm lane feel)
    fence_line(-2650, -1050, -2300, -1750)
    fence_line(-2900, 650, -2650, 1200)
    put(RD + 'SM_S01R_Signpost', START[0] + 430, START[1] - 330, yaw=START_YAW + 30, tag='Farm')

    # ---------------------------------------------------------------- west: bridge, ford, river banks, woods
    put(RD + 'SM_S01R_WoodBridge', BRIDGE[0], BRIDGE[1], BRIDGE_Z - 4, yaw=BRIDGE_YAW, tag='River')   # user-placed position
    rs = RIVER_S
    for i in range(0, len(rs) - 1, 2):
        (ax, ay), (bx2, by2) = rs[i], rs[i + 1]
        tx, ty = bx2 - ax, by2 - ay
        n = math.hypot(tx, ty) or 1
        side = 1 if i % 4 == 0 else -1
        x, y = ax - ty / n * RIVER_W * 0.62 * side, ay + tx / n * RIVER_W * 0.62 * side
        if path_dist((x, y)) < 120 or math.hypot(x - BRIDGE[0], y - BRIDGE[1]) < 500:
            continue
        put(LP + ['Rocks/SM_LPRock03', 'Rocks/SM_LPRock05', 'Rocks/SM_LPRock02'][i % 3], x, y, yaw=60 * i, s=0.8 + .5 * _r(i, 3), tag='River', zoff=-6)
        put(LP + 'Plants/SM_LPPlant0%d' % (i % 2 + 1), x + 120, y - 80, yaw=40 * i, s=1.6, col='none', tag='Plants', zoff=-2)
    # spring under the ridge's west cliff
    sx, sy = RIVER[0]
    for dx, dy, m, s, yaw in ((260, 40, 'Cliffs/SM_LPCliffRock02', 1.3, 15), (230, -330, 'Cliffs/SM_LPCliffRock03', 1.1, 160),
                              (60, 260, 'Rocks/SM_LPRock02', 1.0, 40), (-80, -280, 'Rocks/SM_LPRock05', 1.1, 200)):
        put(LP + m, sx + dx, sy + dy, yaw=yaw, s=s, tag='River', zoff=-25)
    # pond rim
    pcx, pcy, rx, ry, rr = POND
    for i in range(9):
        a = 2 * math.pi * (i + 0.4 * _r(i, 1)) / 9
        u, v = math.cos(a) * rx * 1.22, math.sin(a) * ry * 1.22
        x, y = pcx + u * math.cos(rr) - v * math.sin(rr), pcy + u * math.sin(rr) + v * math.cos(rr)
        if path_dist((x, y)) < 150 or outside_dist(x, y) > 300:
            continue
        put(LP + ['Rocks/SM_LPRock01', 'Rocks/SM_LPRock02', 'Rocks/SM_LPRock05', 'Rocks/SM_LPRock03'][i % 4], x, y,
            s=0.7 + .6 * _r(i, 2), yaw=360 * _r(i, 3), tag='River', zoff=-5)
    # west woods (between the river and the west edge): clusters with clearings, logs
    k = 0
    for cx2, cy2, n in ((-600, -3900, 4), (700, -3950, 3), (1600, -4050, 4), (2400, -3000, 2), (-1900, -3950, 2),
                        (1250, -2950, 2), (-300, -3150, 2)):
        for _ in range(n):
            x, y = cx2 + (_r(k, 1) - .5) * 900, cy2 + (_r(k, 2) - .5) * 700
            if clear_of_gameplay(x, y, 40):
                m = PINES[k % 3] if _r(k, 5) < .35 else TREES[k % 5]
                put(LP + m, x, y, yaw=360 * _r(k, 3), s=1.15 + .45 * _r(k, 4), tag='Trees', zoff=-8)
                if _r(k, 6) < .5:
                    put(LP + 'Plants/SM_LPBush0%d' % (k % 2 + 1), x + 220, y + 150, yaw=90 * k, s=1.0 + .5 * _r(k, 7), col='none', tag='Plants', zoff=-4)
            k += 1
    for i, (x, y) in enumerate(((300, -4150), (1950, -3950), (-1250, -3700))):
        if clear_of_gameplay(x, y):
            put(LP + 'Trees/SM_LPTreeLog0%d' % (i % 3 + 1), x, y, yaw=70 * i + 20, tag='Trees', zoff=-4)
    for i, (x, y) in enumerate(((900, -4250), (-150, -4300), (2200, -4250))):
        put(LP + 'Rocks/SM_LPRock04', x, y, yaw=50 * i, s=1.2, tag='Rocks', zoff=-10)

    # ---------------------------------------------------------------- north ridge: watchtower, cliff rocks, edge rocks, shortcuts
    tx, ty = LANDMARKS['Watchtower']
    put(RD + 'SM_S01R_Watchtower', tx, ty, height(tx, ty), yaw=90, tag='Ridge')
    # rocks on the cliff face (silhouette) where the face is a cliff, away from the shortcut landings
    y = -2450.0
    i = 0
    while y < 2550:
        w = ridge_face_w(y)
        if all(abs(y - c) > SHORTCUT_HALF + 350 for c in SHORTCUTS.values()) and ledge_mask(y) < 0.05 and trail_mask(y) < 0.1:
            rx = ridge_x(y)
            tan = math.degrees(math.atan2(ridge_x(y + 50) - ridge_x(y - 50), 100))
            m = ['Cliffs/SM_LPCliffRock02', 'Cliffs/SM_LPCliffRock03', 'Cliffs/SM_LPCliffRock01'][i % 3]
            base = height(rx - w - 60, y)
            sz = (top_h := height(rx + 60, y)) - base
            nat = {0: 275.0, 1: 417.0, 2: 604.0}[i % 3]      # mesh heights (cm)
            zs = max(0.8, min(2.2, (sz * (0.75 + .2 * _r(i, 2))) / nat))
            put(LP + m, rx - w * 0.5, y, base - 30, yaw=90 - tan + (180 if i % 2 else 0),
                s=(1.1 + .4 * _r(i, 1), 1.0 + .3 * _r(i, 5), zs), tag='Ridge')
        y += 420 + 240 * _r(i, 3)
        i += 1
    # cliff-top edge rocks / bushes (guide rails made of nature), open at the two shortcuts
    y = -2700.0
    i = 0
    while y < 2700:
        if all(abs(y - c) > SHORTCUT_HALF + 80 for c in SHORTCUTS.values()) and trail_mask(y) < 0.2:
            x = ridge_x(y) + 110 + 40 * _r(i, 1)
            if i % 3 == 2:
                put(LP + 'Plants/SM_LPBush0%d' % (i % 2 + 1), x + 40, y, yaw=37 * i, s=1.2, col='none', tag='Ridge', zoff=-4)
            else:
                put(LP + ['Rocks/SM_LPRock05', 'Rocks/SM_LPRock03', 'Rocks/SM_LPRock02'][i % 3], x, y, yaw=53 * i,
                    s=1.0 + .5 * _r(i, 2), tag='Ridge', zoff=-8)
        y += 300 + 160 * _r(i, 4)
        i += 1
    for name, c in SHORTCUTS.items():
        rx = ridge_x(c)
        for side in (-1, 1):   # flanking rocks frame the gap
            put(LP + 'Rocks/SM_LPRock04', rx + 170, c + side * (SHORTCUT_HALF + 120), yaw=90 + 20 * side, s=1.15, tag='Shortcut', zoff=-10)
        # (shortcut signposts removed by the user 2026-11-19: the gap and flanking rocks are the cue)
        # a few flowers mark where you land: A on the ledge and in the valley below, B on the terrace bench
        spots = [(rx - 420, c), (rx - 1150, c)] if name == 'NW' else [(rx - 600, c)]
        for j, (sx2, sy2) in enumerate(spots):
            for f in flower_cluster(sx2, sy2, 900 + 17 * j + int(c), n=4):
                put(*f[:3], yaw=f[3], s=f[4], col='none', tag='Flowers', zoff=-2)
    # pines along the back of the ridge (inside the edge) and a couple near the watchtower
    for i in range(16):
        y = -4100 + 8200 * (i + .5) / 16 + (_r(i, 1) - .5) * 300
        x = 3550 + 150 * _r(i, 2)
        if clear_of_gameplay(x, y, 0):
            put(LP + PINES[i % 3], x, y, yaw=360 * _r(i, 3), s=1.2 + .4 * _r(i, 4), tag='Trees', zoff=-8)

    # ---------------------------------------------------------------- east terrace: windmill on its knoll, edge rocks, open field
    wx, wy = LANDMARKS['Windmill']
    wyaw = math.degrees(math.atan2(CAGE[1] - wy, CAGE[0] - wx))
    wz = height(wx, wy)
    put(RD + 'SM_S01R_WindmillBody', wx, wy, wz, yaw=wyaw, tag='Windmill')
    put(RD + 'SM_S01R_WindmillRotor', wx + 250 * math.cos(math.radians(wyaw)), wy + 250 * math.sin(math.radians(wyaw)), wz + 720,
        yaw=wyaw, tag='WindmillRotor')
    a = math.radians(wyaw)
    for dx, dy, m, yaw in ((300, 220, 'KitBash/SM_FarmHouse_Barrel_001', 0), (250, -280, 'KitBash/SM_FarmHouse_WoodenCrate_001', 15),
                           (-320, 260, 'KitBash/SM_FarmHouse_Logs_Pyramid_001', 70)):
        put(FH + m, wx + dx * math.cos(a) - dy * math.sin(a), wy + dx * math.sin(a) + dy * math.cos(a), yaw=yaw + wyaw, tag='Windmill', zoff=-2)
    # terrace edge rocks (soft boundary of the step), skipping the ramps
    x = -1800.0
    i = 0
    while x < 2300:
        if terrace_edge_w(x) < 300:
            y = terrace_y(x) + 60
            if clear_of_gameplay(x, y, -60) or abs(y - terrace_y(x)) < 200:
                if path_dist((x, y)) > 200 and not landing_zone(x, y):
                    put(LP + ['Rocks/SM_LPRock04', 'Rocks/SM_LPRock02', 'Rocks/SM_LPRock05'][i % 3], x, y,
                        yaw=71 * i, s=0.9 + .4 * _r(i, 1), tag='Terrace', zoff=-20)
        x += 650 + 300 * _r(i, 2)
        i += 1
    for i, (x, y) in enumerate(((2100, 2400), (1800, 4150), (-1500, 4200), (300, 4250), (-2600, 3500))):
        if clear_of_gameplay(x, y, 0):
            put(LP + TREES[(i + 2) % 5], x, y, yaw=80 * i, s=1.3 + .3 * _r(i, 1), tag='Trees', zoff=-8)

    # ---------------------------------------------------------------- valley: knoll trees and rocks (sightline breakers)
    for i, (kx, ky, kr, kh) in enumerate(KNOLLS):
        for j in range(2):
            x, y = kx + (_r(i * 7 + j, 1) - .5) * kr * 0.6, ky + (_r(i * 7 + j, 2) - .5) * kr * 0.6
            if clear_of_gameplay(x, y, 20):
                put(LP + TREES[(i + j) % 5], x, y, yaw=360 * _r(i, 3 + j), s=1.25 + .3 * _r(i, 5 + j), tag='Trees', zoff=-8)
        x, y = kx + kr * 0.35, ky - kr * 0.3
        if clear_of_gameplay(x, y, 0):
            put(LP + 'Rocks/SM_LPRock04', x, y, yaw=40 * i, s=0.9, tag='Rocks', zoff=-10)

    # ---------------------------------------------------------------- outside: forest ring and big rocks (no gameplay)
    x0, x1, y0, y1 = PLAY
    k = 1000
    for ring_off, count in ((350, 70), (1150, 60), (2100, 46)):
        for i in range(count):
            t = (i + _r(k, 1)) / count
            per = 2 * ((x1 - x0) + (y1 - y0))
            d = t * per
            if d < (x1 - x0):
                x, y = x0 + d, y0 - ring_off
            elif d < (x1 - x0) + (y1 - y0):
                x, y = x1 + ring_off, y0 + d - (x1 - x0)
            elif d < 2 * (x1 - x0) + (y1 - y0):
                x, y = x1 - (d - (x1 - x0) - (y1 - y0)), y1 + ring_off
            else:
                x, y = x0 - ring_off, y1 - (d - 2 * (x1 - x0) - (y1 - y0))
            x += (_r(k, 2) - .5) * 500; y += (_r(k, 3) - .5) * 500
            if outside_dist(x, y) < 200:
                k += 1
                continue
            m = PINES[k % 3] if (_r(k, 4) < .45 or x > x1) else TREES[k % 5]
            put(LP + m, x, y, yaw=360 * _r(k, 5), s=1.3 + .7 * _r(k, 6), col='none', tag='ForestRing', zoff=-15)
            k += 1
    for i, (x, y, m, s) in enumerate(((4500, -2600, 'Rocks/SM_LPRock07', 1.4), (4300, 1800, 'Rocks/SM_LPRock08', 1.6),
                                      (-1500, 5200, 'Rocks/SM_LPRock07', 1.2), (-4400, -4800, 'Rocks/SM_LPRock08', 1.3),
                                      (1200, -5300, 'Rocks/SM_LPRock07', 1.3), (-4500, 2600, 'Rocks/SM_LPRock06', 1.6))):
        put(LP + m, x, y, yaw=67 * i, s=s, col='none', tag='ForestRing', zoff=-40)

    # ---------------------------------------------------------------- small plants (low count, meadow edges only)
    k = 2000
    for i in range(170):
        x, y = x0 + (x1 - x0) * _r(k, 1), y0 + (y1 - y0) * _r(k, 2)
        k += 1
        pd = path_dist((x, y))
        if pd < 40 or pd > 500 or math.hypot(x - CAGE[0], y - CAGE[1]) < 500 or poly_dist((x, y), RIVER_S) < RIVER_W * 0.6 \
                or ellipse_r((x, y), POND) < 1.1 or abs(x - ridge_x(y)) < 300:
            continue
        if i % 3 == 0:
            continue    # flowers come in natural clusters (natural_flowers) instead of single scattered stems
        m, s = 'Plants/SM_LPGrass0%d' % (i % 3 + 1), 1.8 + 1.0 * _r(k, 3)
        put(LP + m, x, y, yaw=360 * _r(k, 4), s=s, col='none', tag='Plants', zoff=-2)

    for f in natural_flowers():
        put(*f[:3], yaw=f[3], s=f[4], col='none', tag='Flowers', zoff=-2)

    # ---------------------------------------------------------------- far background mountains
    for i in range(10):
        a = 2 * math.pi * (i + 0.3 * _r(i, 1)) / 10
        r = 12500 + 2500 * _r(i, 2)
        m = 'Cliffs/SM_LPBGMountain0%d' % (i % 2 + 1)
        put(LP + m, r * math.cos(a), r * math.sin(a), -300, yaw=math.degrees(a) + 90 + 30 * _r(i, 3), s=1.7 + .8 * _r(i, 4),
            col='none', tag='Backdrop')
    return out


if __name__ == '__main__':
    ps = placements()
    tags = {}
    for p in ps:
        tags[p['tag']] = tags.get(p['tag'], 0) + 1
    print(len(ps), tags)
