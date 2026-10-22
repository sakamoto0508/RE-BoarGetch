"""Stage01 Redesign set dressing: every placed mesh (new and reused) in UE coordinates. Pure Python."""
import math
from s01r_layout import *  # noqa: F401,F403  (layout constants and terrain functions)
from s01r_layout import _hash

LP = '/Game/InportAssets/LPRiverForest/Meshes/'
FH = '/Game/InportAssets/Farm_House/Meshes/KitBash/'
RD = '/Game/InportAssets/Stage01Redesign/Meshes/'
TREES = ['Trees/SM_LPTree01', 'Trees/SM_LPTree02', 'Trees/SM_LPTree03', 'Trees/SM_LPTree04', 'Trees/SM_LPTree05']
PINES = ['Trees/SM_LPPineTree01', 'Trees/SM_LPPineTree02', 'Trees/SM_LPPineTree03']
# Pack meshes that ship without simple collision (characters walk through them). Placed as copies with collision
# (made by ue_fix_collision.py) so the pack originals stay untouched.
REUSED_DIR = RD + 'Reused/'
REUSED = {src: REUSED_DIR + 'SM_S01R_' + src.split('/SM_')[-1] for src in
          [LP + 'Rocks/SM_LPRock03', LP + 'Rocks/SM_LPRock04', LP + 'Rocks/SM_LPRock05', LP + 'Rocks/SM_LPRock08',
           LP + 'Cliffs/SM_LPCliffRock01', FH + 'SM_FarmHouse_Barrel_001', FH + 'SM_FarmHouse_Bucket_001',
           FH + 'SM_FarmHouse_WoodenCrate_001', FH + 'SM_FarmHouse_Logs_Pyramid_001']}


def _r(i, s):
    return _hash(i, s, 1234)


def clear_of_gameplay(x, y, margin=0.0):
    """True when a solid prop at (x, y) keeps paths, plaza, start, spawn points, water and the start->cage sightline free."""
    if path_dist((x, y)) < 160 + margin:
        return False
    if math.hypot(x - CAGE[0], y - CAGE[1]) < PLAZA_R + 350 + margin:
        return False
    if math.hypot(x - START[0], y - START[1]) < 800 + margin:
        return False
    if abs(x - CAGE[0]) < 650 and CAGE[1] < y < START[1]:
        return False
    if any(math.hypot(x - p[0], y - p[1]) < 450 for p in SPAWN_POINTS.values()):
        return False
    if ellipse_r((x, y), POND) < 1.25 or poly_dist((x, y), STREAM) < STREAM_W:
        return False
    if math.hypot(x - TOWER[0], y - TOWER[1]) < 750 or math.hypot(x - WINDMILL[0], y - WINDMILL[1]) < 420:
        return False
    if math.hypot(x - BRIDGE[0], y - BRIDGE[1]) < 450:
        return False
    return True


def windmill_yaw():
    return math.degrees(math.atan2(CAGE[1] - WINDMILL[1], CAGE[0] - WINDMILL[0]))


def placements():
    """List of dicts: mesh (UE path), x, y, z, yaw, scale, collision ('default'|'none'), tag."""
    out = []

    def put(mesh, x, y, z=None, yaw=0.0, s=1.0, col='default', tag='Decor', zoff=0.0):
        zz = (height(x, y) + zoff) if z is None else z
        if col != 'none':
            mesh = REUSED.get(mesh, mesh)
        out.append({'mesh': mesh, 'x': round(x, 1), 'y': round(y, 1), 'z': round(zz, 1), 'yaw': round(yaw, 1),
                    'scale': list(s) if isinstance(s, (list, tuple)) else [s, s, s], 'collision': col, 'tag': tag})

    # whole-map meshes (world-space pivots)
    for m in ['SM_S01R_Terrain', 'SM_S01R_Paths', 'SM_S01R_PondWater', 'SM_S01R_StreamWater', 'SM_S01R_Waterfall']:
        put(RD + m, 0, 0, 0, tag='Ground' if m in ('SM_S01R_Terrain', 'SM_S01R_Paths') else 'Water',
            col='default' if m == 'SM_S01R_Terrain' else 'none')
    # cage plaza and guidance
    put(RD + 'SM_S01R_CagePlaza', CAGE[0], CAGE[1], 0, tag='Facility')
    for k in range(4):
        for da in (-17, 17):
            a = math.radians(90 * k + da)
            put(RD + 'SM_S01R_Bollard', CAGE[0] + math.cos(a) * 760, CAGE[1] + math.sin(a) * 760, yaw=90 * k, tag='Facility')
    for y in (480, -380):
        for sx in (-1, 1):
            put(RD + 'SM_S01R_GuideBeacon', CAGE[0] + sx * 250, y, tag='Guide')
    for x, y, yaw in ((760, -820, 20), (860, -700, 55), (-560, -2280, -30), (-440, -2400, 10), (460, -3300, 0), (-220, -3330, 40)):
        put(RD + 'SM_S01R_ResearchCrate', x, y, yaw=yaw, tag='Facility')
    put(RD + 'SM_S01R_ResearchCrate', 800, -760, height(800, -760) + 78, yaw=40, s=0.8, tag='Facility')
    for x, y, yaw in ((380, 520, -60), (-1460, -1760, 130), (1980, -1330, 30), (-2240, -1840, 200)):
        put(RD + 'SM_S01R_Signpost', x, y, yaw=yaw, tag='Guide')
    # structures
    put(RD + 'SM_S01R_WoodBridge', BRIDGE[0], BRIDGE[1], 0, tag='Water')
    put(RD + 'SM_S01R_Watchtower', TOWER[0], TOWER[1], height(*TOWER), tag='Hill')
    wyaw = windmill_yaw()
    wz = height(*WINDMILL)
    put(RD + 'SM_S01R_WindmillBody', WINDMILL[0], WINDMILL[1], wz, yaw=wyaw, tag='Windmill')
    hx, hy = WINDMILL[0] + 250 * math.cos(math.radians(wyaw)), WINDMILL[1] + 250 * math.sin(math.radians(wyaw))
    put(RD + 'SM_S01R_WindmillRotor', hx, hy, wz + 720, yaw=wyaw, tag='WindmillRotor')
    put(RD + 'SM_S01R_FacilityMain', FACILITY[0], FACILITY[1], tag='Facility', zoff=-10)
    put(RD + 'SM_S01R_FacilityDome', FACILITY[0] + 1650, FACILITY[1] - 250, tag='Facility', zoff=-10)
    put(RD + 'SM_S01R_FacilityAntenna', FACILITY[0] - 1450, FACILITY[1] - 150, yaw=-20, tag='Facility', zoff=-5)
    put(RD + 'SM_S01R_FacilityDome', FACILITY[0] - 2350, FACILITY[1] - 700, s=0.7, tag='Facility', zoff=-10)
    # research fence along the back with the gate at the back spoke
    x = -2150.0
    while x < 3700:
        if abs(x + 200 - CAGE[0]) > 330:
            put(RD + 'SM_S01R_ResearchFence', x + 200, FENCE_Y, tag='Fence')
        x += 400
    put(RD + 'SM_S01R_ResearchFencePost', x, FENCE_Y, tag='Fence')
    put(RD + 'SM_S01R_ResearchGate', CAGE[0], FENCE_Y, tag='Fence')
    # farm props by the windmill (the meadow side of the research site)
    a = math.radians(wyaw)
    for dx, dy, m, yaw in ((260, 200, 'SM_FarmHouse_Barrel_001', 0), (300, 270, 'SM_FarmHouse_Barrel_001', 40),
                           (230, -260, 'SM_FarmHouse_WoodenCrate_001', 15), (330, -330, 'SM_FarmHouse_Logs_Pyramid_001', 70),
                           (180, 330, 'SM_FarmHouse_Bucket_001', 0)):
        put(FH + m, WINDMILL[0] + dx * math.cos(a) - dy * math.sin(a), WINDMILL[1] + dx * math.sin(a) + dy * math.cos(a),
            yaw=yaw + wyaw, tag='Windmill')
    for fx, fy, yaw in ((-3450, -2350, 80), (-3480, -2000, 95), (-3440, -1650, 85)):
        put(LP + 'HeroProps/SM_LPFence01', fx, fy, yaw=yaw, s=1.2, tag='Windmill')
    # waterfall dressing
    wx, wy = WATERFALL
    for dx, dy, m, s, yaw in ((-260, -10, 'Cliffs/SM_LPCliffRock01', 1.2, 10), (250, 20, 'Cliffs/SM_LPCliffRock02', 1.1, 190),
                              (-520, -60, 'Cliffs/SM_LPCliffRock03', 1.3, 60), (520, -40, 'Cliffs/SM_LPCliffRock01', 1.0, 140),
                              (-150, 260, 'Rocks/SM_LPRock02', 0.9, 30), (190, 240, 'Rocks/SM_LPRock03', 0.8, 200)):
        put(LP + m, wx + dx, wy + dy, s=s, yaw=yaw, tag='Water', zoff=-30)
    # rocks around the pond and stream banks (low, walk-around)
    cx, cy, rx, ry, rr = POND
    for i in range(10):
        t = 2 * math.pi * (i + _r(i, 1) * .5) / 10
        u, v = math.cos(t) * rx * 1.18, math.sin(t) * ry * 1.18
        x, y = cx + u * math.cos(rr) - v * math.sin(rr), cy + u * math.sin(rr) + v * math.cos(rr)
        if path_dist((x, y)) < 150:
            continue
        m = ['Rocks/SM_LPRock01', 'Rocks/SM_LPRock02', 'Rocks/SM_LPRock05', 'Rocks/SM_LPRock03'][i % 4]
        put(LP + m, x, y, s=0.6 + .5 * _r(i, 2), yaw=360 * _r(i, 3), tag='Water', zoff=-5)
    for i, (x, y) in enumerate(((-2400, -2700), (-2980, -2500), (-2350, -1650), (-2860, -1500), (-2290, -2350))):
        # small bank stones only (SM_LPRock06/07 are 4-8 m boulders)
        put(LP + ['Rocks/SM_LPRock03', 'Rocks/SM_LPRock05', 'Rocks/SM_LPRock02'][i % 3], x, y, s=0.75, yaw=70 * i, tag='Water', zoff=-4)
    # trees: berm ring outside the walkable rectangle + a few shade clusters inside
    x0, x1, y0, y1 = PLAY
    pts, k = [], 0
    for side in ('L', 'R', 'F'):
        n = 15 if side != 'F' else 17
        for i in range(n):
            t = (i + .5) / n
            off = 180 + 420 * _r(k, 4)
            pts.append({'L': (x0 - off, y0 + 150 + (y1 - y0) * t), 'R': (x1 + off, y0 + (y1 - y0) * t),
                        'F': (x0 + (x1 - x0) * t, y1 + off)}[side])
            k += 1
    for i, (x, y) in enumerate(pts):
        m = PINES[i % 3] if _r(i, 8) < .3 else TREES[i % 5]
        put(LP + m, x + (_r(i, 5) - .5) * 200, y + (_r(i, 6) - .5) * 200, yaw=360 * _r(i, 7), s=1.25 + .45 * _r(i, 9),
            tag='Trees', zoff=-10)
    clusters = [(-3050, 450, 3), (-1150, 1150, 2), (1700, 1150, 2), (3150, 700, 3), (-1100, -3250, 2), (1900, -3150, 3),
                (-3300, -1100, 2), (2050, -350, 1), (-200, -3250, 1)]
    k = 100
    for cx2, cy2, n in clusters:
        for _ in range(n):
            x, y = cx2 + (_r(k, 1) - .5) * 600, cy2 + (_r(k, 2) - .5) * 600
            if clear_of_gameplay(x, y, 60):
                put(LP + TREES[k % 5], x, y, yaw=360 * _r(k, 3), s=1.1 + .4 * _r(k, 4), tag='Trees', zoff=-8)
            k += 1
    # bushes along the berm foot and the fence line
    k = 300
    for i in range(70):
        if i < 40:
            side, t = i % 3, _r(k, 1)
            x = [x0 + 40 + 120 * _r(k, 2), x1 - 40 - 120 * _r(k, 2), x0 + (x1 - x0) * t][side]
            y = [y0 + (y1 - y0) * t, y0 + (y1 - y0) * t, y1 - 60 - 120 * _r(k, 2)][side]
            ok = clear_of_gameplay(x, y, -60)
        else:
            x, y = -2150 + 5600 * _r(k, 3), FENCE_Y + 130 + 80 * _r(k, 4)
            ok = abs(x - CAGE[0]) > 450 and path_dist((x, y)) > 120
        if ok:
            put(LP + ['Plants/SM_LPBush01', 'Plants/SM_LPBush02'][k % 2], x, y, yaw=360 * _r(k, 5), s=0.9 + .6 * _r(k, 6),
                col='none', tag='Plants', zoff=-4)
        k += 1
    # grass tufts and flowers (no collision)
    k = 500
    for i in range(260):
        x, y = x0 + (x1 - x0) * _r(k, 1), y0 + (y1 - y0) * _r(k, 2)
        k += 1
        if path_dist((x, y)) < 60 or math.hypot(x - CAGE[0], y - CAGE[1]) < PLAZA_R + 120 or ellipse_r((x, y), POND) < 1.05 \
                or poly_dist((x, y), STREAM) < STREAM_W * .6 or math.hypot(x - TOWER[0], y - TOWER[1]) < 680:
            continue
        near_start = math.hypot(x - START[0], y - START[1]) < 1500
        if i % 4 == 0 or (near_start and i % 3 == 0):
            m, s = ['Plants/SM_LPFlower01', 'Plants/SM_LPFlower02'][i % 2], 1.2 + .6 * _r(k, 3)
        else:
            m, s = ['Plants/SM_LPGrass01', 'Plants/SM_LPGrass02', 'Plants/SM_LPGrass03'][i % 3], 1.6 + 1.2 * _r(k, 3)
        put(LP + m, x, y, yaw=360 * _r(k, 4), s=s, col='none', tag='Plants', zoff=-2)
    for i, (x, y) in enumerate(((-3010, 330), (-2890, 560), (1830, -3060), (3260, 620))):
        put(LP + 'Plants/SM_LPMushroom0%d' % (i % 3 + 1), x, y, s=1.4, col='none', tag='Plants')
    # hill flank rocks and a log
    for i, (x, y, m) in enumerate(((2050, -2350, 'Rocks/SM_LPRock04'), (3550, -2500, 'Rocks/SM_LPRock08'),
                                   (2200, -900, 'Rocks/SM_LPRockSlab01'), (3450, -900, 'Trees/SM_LPTreeLog01'))):
        if clear_of_gameplay(x, y, -100):
            put(LP + m, x, y, yaw=50 * i, tag='Hill', zoff=-6)
    # distant mountains beyond the berms
    for x, y, s, yaw, m in ((-9000, -1500, 1.4, 90, 'SM_LPBGMountain01'), (-8000, -7500, 1.6, 40, 'SM_LPBGMountain02'),
                            (-2500, -9500, 1.5, 0, 'SM_LPBGMountain01'), (4500, -9800, 1.7, 180, 'SM_LPBGMountain02'),
                            (9500, -5000, 1.5, 250, 'SM_LPBGMountain01'), (9500, 1500, 1.3, 270, 'SM_LPBGMountain02'),
                            (-7000, 6500, 1.3, 120, 'SM_LPBGMountain02'), (5000, 7500, 1.3, 300, 'SM_LPBGMountain01')):
        put(LP + 'Cliffs/' + m, x, y, -250, yaw=yaw, s=s, col='none', tag='Backdrop')
    return out
