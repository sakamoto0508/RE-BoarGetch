"""Stage 1 Stage Select diorama builder (Blender 5.x, background). Same method and look as the Stage 2 one:
blender -b --factory-startup --python SourceArt/Stage01Redesign/Diorama/build_diorama_s01.py -- [--no-render]

Reuses SourceArt/Stage02/Diorama/build_diorama.py as a library (mesh helpers, palette, FBX source import, export,
preview camera) and Scripts/s01r_layout.py / s01r_dressing.py as the single source of the real stage:
  * island: s01r_layout.height() on a ~150 cm grid, play area + the research yard, heights x2.2, thick soil sides,
    paths painted from s01r_layout.PATHS, pond / stream as clear blue water, the back-left cliff with its waterfall,
  * landmarks: the project's own Stage01Redesign FBX (cage plaza, research facility, dome, antenna, research fence +
    gate, watchtower, windmill, wood bridge, bollards, research crates) and SM_Cage, at the same absolute miniature
    scale as Stage 2 so both cages / windmills / towers read the same size; trees and rocks as simple low-poly shapes
    at the s01r_dressing positions.
Orientation: the player faces -Y at the start, so the BP rotates the island by yaw 135 (stage -Y = away from the
capture camera): start meadow in front, cage in the middle, facility at the back, pond / windmill left, hill right.
Outputs FBX/SM_S01_Diorama_{Island,Props}.fbx, Screenshots/*.png (git-ignored), ../Logs/diorama_stats.json.
"""
import bpy, json, math, os, sys
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
STAGE = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(STAGE))
sys.path.insert(0, os.path.join(STAGE, 'Scripts'))
sys.path.insert(0, os.path.join(ROOT, 'SourceArt', 'Stage02', 'Diorama'))
import build_diorama as BD      # noqa: E402  (library: the Stage 2 builder; its main() only runs as a script)
import s01r_layout as L         # noqa: E402
import s01r_dressing as D       # noqa: E402

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
RENDER = '--no-render' not in argv

# ---------------------------------------------------------------- configuration of the shared library
BD.S, BD.E = 0.11, 2.2
BD.CX, BD.CY = 0.0, -1500.0           # island centre: play area + research yard (cage at (120, -1510))
BD.OUT_A, BD.OUT_B, BD.OUT_N = 3800.0, 3300.0, 5.0     # half extents x (E-W), y (front-back)
BD.YAW_UE = 135.0
BD.OUT_FBX = os.path.join(HERE, 'FBX')
BD.OUT_PNG = os.path.join(HERE, 'Screenshots')
NX, NY = 50, 44                        # grid cells along x / y (~150 cm)
ISLAND_Z = -80.0                       # = ue_apply_diorama_s01.ISLAND_Z (front bottom edge on the same screen row as Stage 2)
ABS = 0.229                            # absolute miniature scale of the cage (= Stage 2: S 0.088 x k 2.6)
STATS = {}
DV, DZ = BD.DV, BD.DZ
STREAM_HALF, STREAM_FLAT, STREAM_BANK = 190.0, 300.0, 460.0


def k(abs_scale):
    """Landmark factor for BD.place() that gives the same absolute scale as Stage 2."""
    return abs_scale / BD.S


def grid_point(i, j):
    u, v = -1 + 2 * i / NX, -1 + 2 * j / NY
    m = max(abs(u), abs(v))
    if m < 1e-9:
        return BD.CX, BD.CY
    n = (abs(u) ** BD.OUT_N + abs(v) ** BD.OUT_N) ** (1.0 / BD.OUT_N)
    f = m / n
    return BD.CX + u * f * BD.OUT_A, BD.CY + v * f * BD.OUT_B


def outline_norm(x, y):
    return (abs((x - BD.CX) / BD.OUT_A) ** BD.OUT_N + abs((y - BD.CY) / BD.OUT_B) ** BD.OUT_N) ** (1.0 / BD.OUT_N)


def dio_height(x, y):
    h = L.height(x, y)
    d = L.poly_dist((x, y), L.STREAM)
    if d < STREAM_BANK and y > -3250:
        bed = L.STREAM_WATER_Z - 22.0
        h = min(h, bed) if d < STREAM_FLAT else min(h, bed + (h - bed) * L.smoothstep(STREAM_FLAT, STREAM_BANK, d))
    return h


def face_colour(pts, n):
    cx = sum(p[0] for p in pts) / 3
    cy = sum(p[1] for p in pts) / 3
    slope = math.degrees(math.acos(max(-1.0, min(1.0, n.z))))
    if slope > 40:
        return 'Cliff'
    if L.ellipse_r((cx, cy), L.POND) < 1.12 or (L.poly_dist((cx, cy), L.STREAM) < STREAM_FLAT and cy > -3250):
        return 'Shore'
    if L.path_dist((cx, cy)) < 0:
        return 'Dirt'
    s = L.surface(cx, cy)
    return {'Grass': 'Grass', 'GrassLight': 'GrassLight', 'Cliff': 'Cliff', 'Shore': 'Shore'}[s]


def build_island():
    mb = BD.MB('SM_S01_Diorama_Island')
    P = {}
    for j in range(NY + 1):
        for i in range(NX + 1):
            x, y = grid_point(i, j)
            P[i, j] = (x, y, dio_height(x, y))
    counts = {}

    def tri(a, b, c):
        pa, pb, pc = P[a], P[b], P[c]
        va = Vector((pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]))
        vb = Vector((pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]))
        n = va.cross(vb)
        if n.length < 1e-6:
            return
        n.normalize()
        if n.z < 0:
            n = -n
        col = face_colour((pa, pb, pc), n)
        counts[col] = counts.get(col, 0) + 1
        mb.face([DV(*pa), DV(*pb), DV(*pc)], col, out=Vector((0, 0, 1)))

    for j in range(NY):
        for i in range(NX):
            a, b, c, d = (i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)
            if abs(P[a][2] - P[c][2]) < abs(P[b][2] - P[d][2]):
                tri(a, b, c); tri(a, c, d)
            else:
                tri(a, b, d); tri(b, c, d)
    ring = [(i, 0) for i in range(NX)] + [(NX, j) for j in range(NY)] + [(i, NY) for i in range(NX, 0, -1)] + [(0, j) for j in range(NY, 0, -1)]
    taper = 0.965
    bot = []
    for q in range(len(ring)):
        ta, tb = DV(*P[ring[q]]), DV(*P[ring[(q + 1) % len(ring)]])
        out = Vector(((ta.x + tb.x) / 2, (ta.y + tb.y) / 2, 0)).normalized()
        turf_a, turf_b = ta.z - DZ(4.0), tb.z - DZ(4.0)
        band_a, band_b = min(turf_a, DZ(BD.SIDE_BAND)), min(turf_b, DZ(BD.SIDE_BAND))
        mb.face([ta, tb, Vector((tb.x, tb.y, turf_b)), Vector((ta.x, ta.y, turf_a))], 'Turf', out=out)
        mb.face([Vector((ta.x, ta.y, turf_a)), Vector((tb.x, tb.y, turf_b)), Vector((tb.x, tb.y, band_b)), Vector((ta.x, ta.y, band_a))], 'Earth', out=out)
        ba = Vector((ta.x * taper, ta.y * taper, DZ(BD.BOTTOM)))
        bb = Vector((tb.x * taper, tb.y * taper, DZ(BD.BOTTOM)))
        mb.face([Vector((ta.x, ta.y, band_a)), Vector((tb.x, tb.y, band_b)), bb, ba], 'SoilDark', out=out)
        bot.append(ba)
    mb.face(list(reversed(bot)), 'SoilDark', out=Vector((0, 0, -1)))

    # stream ribbon (fixed level), pond disc (fixed level), waterfall sheet down the back-left cliff
    pts = L.STREAM
    left, right = [], []
    for q, (x, y) in enumerate(pts):
        a, b = pts[max(0, q - 1)], pts[min(len(pts) - 1, q + 1)]
        tx, ty = b[0] - a[0], b[1] - a[1]
        t = math.hypot(tx, ty) or 1
        nx, ny = -ty / t, tx / t
        left.append((x + nx * STREAM_HALF, y + ny * STREAM_HALF, L.STREAM_WATER_Z))
        right.append((x - nx * STREAM_HALF, y - ny * STREAM_HALF, L.STREAM_WATER_Z))
    for q in range(len(pts) - 1):
        mb.face([DV(*p) for p in (left[q], left[q + 1], right[q + 1], right[q])], 'Water', out=Vector((0, 0, 1)))
    cx, cy, rx, ry, rot = L.POND
    pr = []
    for q in range(28):
        a = 2 * math.pi * q / 28
        u, v = math.cos(a) * rx * 1.02, math.sin(a) * ry * 1.02
        pr.append((cx + u * math.cos(rot) - v * math.sin(rot), cy + u * math.sin(rot) + v * math.cos(rot), L.POND_WATER_Z))
    mb.face([DV(*p) for p in pr], 'Water', out=Vector((0, 0, 1)))
    top = (L.WATERFALL[0], L.WATERFALL[1] - 90)
    zt = dio_height(*top) + 6
    base = (L.STREAM[0][0], L.STREAM[0][1])
    steps = 4
    for q in range(steps):
        t0, t1 = q / steps, (q + 1) / steps
        def at(t, side):
            x = top[0] + (base[0] - top[0]) * t + side * 150
            y = top[1] + (base[1] - top[1]) * t
            z = zt + (L.STREAM_WATER_Z - zt) * (t ** 1.6)
            return (x, y, z)
        col = 'WaterLight' if q % 2 == 0 else 'Water'
        mb.face([DV(*at(t0, -1)), DV(*at(t0, 1)), DV(*at(t1, 1)), DV(*at(t1, -1))], col, out=Vector((0, 1, 0.3)).normalized())
    STATS['island_faces'] = counts
    STATS['top_dcm'] = round(max(p[2] for p in P.values()) * BD.S * BD.E, 1)
    return mb.finish(weld=False)


BD.SRC.update({n: os.path.join(STAGE, 'FBX', 'SM_S01R_%s.fbx' % n) for n in
               ('FacilityMain', 'FacilityDome', 'FacilityAntenna', 'ResearchFence', 'ResearchFencePost', 'ResearchGate',
                'Bollard', 'ResearchCrate')})


def build_props():
    obs = []
    cx, cy = L.CAGE
    hz = dio_height(cx, cy)
    plaza = BD.import_source('CagePlaza')
    obs.append(BD.place(plaza, cx, cy, hz, 0, k(0.16), zoff_dcm=0.6))
    obs.append(BD.place(BD.import_source('Cage'), cx, cy, hz, 0, k(ABS), zoff_dcm=0.8))
    for q in range(4):          # bollard ring at the plaza rim (a research-site cue around the cage)
        for da in (-17, 17):
            a = math.radians(90 * q + da)
            x, y = cx + math.cos(a) * 1050, cy + math.sin(a) * 1050
            obs.append(BD.place(BD.import_source('Bollard'), x, y, dio_height(x, y), 90 * q, k(0.18), zoff_dcm=-0.5))
    # research facility at the back, fence + gate in front of it
    fx, fy = L.FACILITY
    obs.append(BD.place(BD.import_source('FacilityMain'), fx, fy, dio_height(fx, fy), 0, k(0.12), zoff_dcm=-1))
    obs.append(BD.place(BD.import_source('FacilityDome'), fx + 1650, fy - 250, dio_height(fx + 1650, fy - 250), 0, k(0.14), zoff_dcm=-1))
    obs.append(BD.place(BD.import_source('FacilityAntenna'), fx - 1450, fy - 150, dio_height(fx - 1450, fy - 150), -20, k(0.15), zoff_dcm=-1))
    x = -2150.0
    while x < 3600:
        if abs(x + 200 - cx) > 330 and outline_norm(x + 200, L.FENCE_Y) < 0.97:
            seg = BD.import_source('ResearchFence')
            seg.data.transform(Matrix.Diagonal((1 / 1.35, 1.0, 1.0, 1.0)))   # keep the 4 m spacing, taller fence
            obs.append(BD.place(seg, x + 200, L.FENCE_Y, dio_height(x + 200, L.FENCE_Y), 0, 1.35, zoff_dcm=-0.5))
        x += 400
    obs.append(BD.place(BD.import_source('ResearchGate'), cx, L.FENCE_Y, dio_height(cx, L.FENCE_Y), 0, 1.35, zoff_dcm=-0.5))
    for x, y, yaw in ((760, -820, 20), (860, -700, 55), (-560, -2280, -30), (460, -3300, 0), (-220, -3330, 40)):
        obs.append(BD.place(BD.import_source('ResearchCrate'), x, y, dio_height(x, y), yaw, k(0.20), zoff_dcm=-0.5))
    # hill watchtower (same absolute scale as Stage 2), windmill (rotor toward the camera / start), bridge
    tx, ty = L.TOWER
    obs.append(BD.place(BD.import_source('Watchtower'), tx, ty, dio_height(tx, ty), 0, k(0.211), zoff_dcm=-1))
    wx, wy = L.WINDMILL
    wz = dio_height(wx, wy)
    wyaw = 70.0
    obs.append(BD.place(BD.import_source('WindmillBody'), wx, wy, wz, wyaw, k(0.194), zoff_dcm=-1))
    obs.append(BD.place(BD.import_source('WindmillRotor'), wx, wy, wz, wyaw, k(0.194), extra=(2.5, 0, 7.2), zoff_dcm=-1))
    bx, by = L.BRIDGE
    obs.append(BD.place(BD.import_source('Bridge'), bx, by, L.STREAM_WATER_Z, 0, k(0.17), zoff_dcm=1.0))

    mb = BD.MB('Procedural')
    trees = [p for p in D.placements() if p['tag'] == 'Trees']
    kept = 0
    for i, p in enumerate(trees):
        x, y = p['x'], p['y']
        if outline_norm(x, y) > 0.96 or L.ellipse_r((x, y), L.POND) < 1.25 or L.poly_dist((x, y), L.STREAM) < STREAM_BANK:
            continue
        s = p['scale'][0]
        c = DV(x, y, dio_height(x, y))
        z0 = c.z - DZ(2)
        if 'Pine' in p['mesh']:
            hgt = DZ(58 + 14 * BD.hsh(i, 1)) * s / 1.3
            BD.prism(mb, c, DZ(3.2), z0, z0 + hgt * 0.3, 6, 'WoodDark')
            for lvl, (rr, zz, hh) in enumerate(((15, .18, .45), (11.5, .42, .38), (7.5, .66, .36))):
                BD.prism(mb, c, DZ(rr) * s / 1.3, z0 + hgt * zz, z0 + hgt * (zz + hh), 7, 'Pine', r_top=0.0, rot=BD.hsh(i, lvl) * 3)
        else:
            tree_crown(mb, c, z0, s, i)
        kept += 1
    # a few extra pines along the back corners and the windmill side so the island edge reads as woodland
    for i, (x, y) in enumerate(((-3500, -3900), (-3300, -4300), (-2700, -4500), (3300, -4300), (3550, -3800), (3500, -3100),
                                (-3550, -1250), (-3500, -650), (3550, 300), (-3350, 900), (2950, 1350), (-2600, 1400))):
        if outline_norm(x, y) > 0.96:
            continue
        c = DV(x, y, dio_height(x, y))
        z0 = c.z - DZ(2)
        hgt = DZ(60 + 12 * BD.hsh(i, 9))
        BD.prism(mb, c, DZ(3.2), z0, z0 + hgt * 0.3, 6, 'WoodDark')
        for lvl, (rr, zz, hh) in enumerate(((15, .18, .45), (11.5, .42, .38), (7.5, .66, .36))):
            BD.prism(mb, c, DZ(rr), z0 + hgt * zz, z0 + hgt * (zz + hh), 7, 'Pine', r_top=0.0, rot=BD.hsh(i, lvl + 4) * 3)
        kept += 1
    STATS['trees'] = kept
    rocks = [p for p in D.placements() if ('Rocks/' in p['mesh'] or 'Cliffs/' in p['mesh']) and p['tag'] in ('Water', 'Hill')]
    for i, p in enumerate(rocks):
        x, y = p['x'], p['y']
        big = 'Cliffs/' in p['mesh']
        c = DV(x, y, dio_height(x, y))
        rock(mb, c, DZ(13 if big else 7) * p['scale'][0], i)
    STATS['rocks'] = len(rocks)
    obs.append(mb.finish(weld=False))
    return BD.join(obs, 'SM_S01_Diorama_Props')


def tree_crown(mb, c, z0, s, i):
    import bmesh
    hgt = DZ(26 + 8 * BD.hsh(i, 1)) * s / 1.3
    BD.prism(mb, c, DZ(3.6), z0, z0 + hgt, 6, 'Wood')
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=1, radius=1.0)
    rad = DZ(17 + 6 * BD.hsh(i, 2)) * s / 1.3
    ctr = Vector((c.x, c.y, z0 + hgt + rad * 0.75))
    for v in bm.verts:
        v.co = Vector((ctr.x + v.co.x * rad, ctr.y + v.co.y * rad, ctr.z + v.co.z * rad * 0.92))
    for f in bm.faces:
        mb.face([v.co.copy() for v in f.verts], 'Leaf' if (f.index + i) % 4 else 'Grass', out=(f.calc_center_median() - ctr).normalized())
    bm.free()


def rock(mb, c, r, i):
    import bmesh
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=1, radius=1.0)
    for v in bm.verts:
        j = 0.75 + 0.5 * BD.hsh(i * 13 + v.index, 5)
        v.co = Vector((c.x + v.co.x * r * j * 1.2, c.y + v.co.y * r * j, c.z - DZ(3) + max(-0.3, v.co.z) * r * j * 0.9))
    for f in bm.faces:
        mb.face([v.co.copy() for v in f.verts], 'Rock', out=(f.calc_center_median() - c).normalized())
    bm.free()


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    island = build_island()
    props = build_props()
    for ob in (island, props):
        BD.apply_colours(ob)
        STATS[ob.name] = {'tris': sum(len(p.vertices) - 2 for p in ob.data.polygons), 'verts': len(ob.data.vertices)}
        BD.export_fbx(ob)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, 'S01_Diorama.blend'))
    if RENDER:
        BD.preview([(island.name, DZ(ISLAND_Z)), (props.name, DZ(ISLAND_Z))])
    os.makedirs(os.path.join(STAGE, 'Logs'), exist_ok=True)
    with open(os.path.join(STAGE, 'Logs', 'diorama_stats.json'), 'w') as f:
        json.dump(STATS, f, indent=1)
    print('[S01Diorama] COMPLETE', json.dumps(STATS))


main()
