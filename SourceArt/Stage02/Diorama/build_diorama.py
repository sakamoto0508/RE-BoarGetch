"""Stage 2 "段丘牧場" Stage Select diorama builder (Blender 5.x, background).
blender -b --factory-startup --python SourceArt/Stage02/Diorama/build_diorama.py -- [--no-render]

A hand-sized miniature of L_Stage02 for ABoarStagePreviewActor (BP_Stage02_Diorama). It is NOT the gameplay terrain:
  * the island is re-sampled from Scripts/s02_layout.py (the single source of the real terrain) on a coarse 200 cm grid,
    cropped to a rounded play-area outline, heights exaggerated x2 so the north ridge reads at a glance,
  * the river is widened into a clear blue ribbon, the island gets thick soil sides (no thin plane underneath),
  * landmarks reuse the project's own low-poly sources (SM_Cage, Stage01Redesign windmill / watchtower / wood bridge /
    cage plaza) at an exaggerated scale; trees, barn, farmhouse, fence and shortcut rocks are simple low-poly shapes
    placed where s02_dressing.py places the real ones (thinned out).
Every mesh carries one material slot ("Diorama") and vertex colours (palette below, linear). In UE they use
M_Preview_VertexColor, which applies the same fake key-light shading as the Stage 1 M_Preview_* materials.

Outputs (UE cm of the diorama actor, stage north = +X; the BP part rotates it by yaw 45 to face the capture camera):
  FBX/SM_S02_Diorama_Island.fbx   terrain top + soil sides + river / pond water
  FBX/SM_S02_Diorama_Props.fbx    cage, plaza, windmill, watchtower, bridge, farm, trees, rocks
The pedestal is NOT part of the meshes: WBP_LobbyStageSelect draws the shared holographic pedestal (T_SS_Pedestal) under
every diorama, so Stage 1 and Stage 2 stand on the same stand; BP_Stage02_Diorama lowers the island so that its front
bottom edge lands where Stage 1's does on screen.
  Screenshots/*.png (git-ignored), ../Logs/diorama_stats.json
"""
import bpy, bmesh, json, math, os, sys
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
STAGE = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(STAGE))   # SourceArt -> project
sys.path.insert(0, os.path.join(STAGE, 'Scripts'))
import s02_layout as L          # noqa: E402
import s02_dressing as D        # noqa: E402

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
RENDER = '--no-render' not in argv
OUT_FBX = os.path.join(HERE, 'FBX')
OUT_PNG = os.path.join(HERE, 'Screenshots')
STATS = {}

# ---------------------------------------------------------------- scale / shape
S = 0.088            # stage cm -> diorama cm (horizontal)
CX, CY = 0.0, 0.0    # stage point that becomes the diorama origin
YAW_UE = 45.0        # BP part yaw (preview only): stage 'back' must point along the capture camera's forward (1, 1)
E = 2.0              # vertical exaggeration on top of S
OUT_A, OUT_B, OUT_N = 4000.0, 4800.0, 5.0      # island outline: superellipse half extents (stage cm, x=N-S, y=E-W)
NV, NU = 52, 64      # grid cells (x, y) -> ~150 cm
ISLAND_Z = -87.0     # part offset in BP_Stage02_Diorama (front bottom edge on screen = Stage 1's)
SIDE_BAND = -20.0    # diorama cm: turf/earth -> dark soil boundary on the sides
BOTTOM = -42.0       # diorama cm: island underside
RIVER_HALF = 260.0   # widened river ribbon half width (stage cm; the real river is 420 wide)
RIVER_FLAT, RIVER_BANK = 460.0, 680.0

# Palette (linear). The first block matches the Stage 1 M_Preview_* constants so both dioramas share one colour set.
PAL = {
    'Grass': (.23, .68, .045), 'Leaf': (.10, .43, .025), 'Earth': (.28, .115, .035), 'Dirt': (.72, .40, .12),
    'Water': (.015, .48, .92), 'White': (.85, .92, 1.0), 'Navy': (.025, .065, .14), 'Cyan': (.015, .75, 1.0),
    'Yellow': (1.0, .72, .025),
    'GrassLight': (.36, .80, .085), 'Shore': (.40, .24, .08), 'Cliff': (.40, .33, .25), 'Rock': (.47, .44, .40),
    'SoilDark': (.13, .055, .02), 'Turf': (.16, .45, .03), 'Pine': (.04, .30, .07),
    'Wood': (.40, .20, .07), 'WoodDark': (.20, .09, .035), 'WoodLight': (.62, .40, .17), 'Ivory': (.86, .80, .62),
    'Blue': (.03, .20, .62), 'BarnRed': (.55, .07, .035), 'Roof': (.09, .11, .16), 'Gray': (.55, .60, .66),
    'GrayLight': (.70, .75, .80), 'WaterLight': (.10, .66, 1.0), 'Glass': (.10, .42, .62),
}
NAMES = list(PAL)
CID = {n: i for i, n in enumerate(NAMES)}
# imported source material slot -> palette
SLOT = {'M_Cage_Navy': 'Navy', 'M_Cage_White': 'White', 'M_Cage_Yellow': 'Yellow', 'M_Cage_Glow': 'Cyan',
        'Blue': 'Blue', 'Ivory': 'Ivory', 'Yellow': 'Yellow', 'Cyan': 'Cyan', 'Wood': 'Wood', 'WoodDark': 'WoodDark',
        'WoodLight': 'WoodLight', 'Glass': 'Glass', 'Machine': 'Gray', 'Water': 'Water'}
KEY = Vector((-.35, .45, .82))      # UE M_Preview key direction (-.35,-.45,.82) in Blender axes (y flipped)


def DV(x, y, z):
    """stage cm (UE axes) -> Blender metres of the diorama (diorama cm / 100)."""
    return Vector(((x - CX) * S / 100.0, -(y - CY) * S / 100.0, z * S * E / 100.0))


def DZ(zd):
    """diorama cm -> Blender z."""
    return zd / 100.0


def hsh(i, s=0):
    return L._hash(i, s, 977)


# ---------------------------------------------------------------- mesh helpers
class MB:
    def __init__(self, name):
        self.name, self.bm = name, bmesh.new()
        self.cl = self.bm.faces.layers.int.new('cidx')

    def face(self, pts, col, out=None):
        f = self.bm.faces.new([self.bm.verts.new(p) for p in pts])
        f[self.cl] = CID[col]
        f.normal_update()
        if out is not None and f.normal.dot(out) < 0:
            f.normal_flip()
        return f

    def finish(self, weld=True):
        bm = self.bm
        if weld:
            bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0002)
        me = bpy.data.meshes.new(self.name)
        bm.to_mesh(me)
        bm.free()
        ob = bpy.data.objects.new(self.name, me)
        bpy.context.scene.collection.objects.link(ob)
        return ob


def prism(mb, center, r, z0, z1, sides, col, r_top=None, rot=0.0, scale_xy=(1, 1)):
    """Closed n-gon prism / frustum (Blender coords). r_top=0 -> cone."""
    r_top = r if r_top is None else r_top
    ring = lambda rr, z: [Vector((center.x + rr * scale_xy[0] * math.cos(rot + 2 * math.pi * k / sides),
                                  center.y + rr * scale_xy[1] * math.sin(rot + 2 * math.pi * k / sides), z)) for k in range(sides)]
    lo, hi = ring(r, z0), ring(r_top, z1)
    c = Vector((center.x, center.y, (z0 + z1) / 2))
    for k in range(sides):
        a, b = k, (k + 1) % sides
        if r_top > 1e-6:
            mb.face([lo[a], lo[b], hi[b], hi[a]], col, out=((lo[a] + lo[b]) / 2 - c).normalized())
        else:
            mb.face([lo[a], lo[b], Vector((center.x, center.y, z1))], col, out=((lo[a] + lo[b]) / 2 - c).normalized())
    mb.face(list(reversed(lo)), col, out=Vector((0, 0, -1)))
    if r_top > 1e-6:
        mb.face(hi, col, out=Vector((0, 0, 1)))


def box(mb, center, size, yaw, col, z0):
    """Axis box rotated by Blender yaw (rad); size = (sx, sy, sz) in Blender metres, base at z0."""
    sx, sy, sz = size
    c, s = math.cos(yaw), math.sin(yaw)
    P = lambda u, v, w: Vector((center.x + u * c - v * s, center.y + u * s + v * c, z0 + w))
    v = [P(u, w, z) for z in (0, sz) for (u, w) in ((-sx / 2, -sy / 2), (sx / 2, -sy / 2), (sx / 2, sy / 2), (-sx / 2, sy / 2))]
    mid = P(0, 0, sz / 2)
    for q in ((0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        pts = [v[i] for i in q]
        mb.face(pts, col, out=(sum(pts, Vector()) / 4 - mid).normalized())


# ---------------------------------------------------------------- island
def outline_norm(x, y):
    return (abs(x / OUT_A) ** OUT_N + abs(y / OUT_B) ** OUT_N) ** (1.0 / OUT_N)


def squircle(u, v):
    """Square grid (u, v in [-1, 1]) -> superellipse disk (stage cm). The square border lands on the outline."""
    m = max(abs(u), abs(v))
    if m < 1e-9:
        return 0.0, 0.0
    n = (abs(u) ** OUT_N + abs(v) ** OUT_N) ** (1.0 / OUT_N)
    k = m / n
    return v * k * OUT_A, u * k * OUT_B        # x (north) from v, y (east) from u


def nearest_on(pts, p):
    best = (1e18, pts[0])
    for i in range(len(pts) - 1):
        (ax, ay), (bx, by) = pts[i], pts[i + 1]
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - ay) * dy) / (dx * dx + dy * dy or 1)))
        q = (ax + t * dx, ay + t * dy)
        d = math.hypot(p[0] - q[0], p[1] - q[1])
        if d < best[0]:
            best = (d, q)
    return best


RIVER_LEVEL_CACHE = {}


def river_level(q):
    k = (round(q[0]), round(q[1]))
    if k not in RIVER_LEVEL_CACHE:
        RIVER_LEVEL_CACHE[k] = L.height(q[0], q[1]) + 22.0
    return RIVER_LEVEL_CACHE[k]


def dio_height(x, y):
    """Real terrain height (stage cm) with the diorama river carving."""
    h = L.height(x, y)
    d, q = nearest_on(L.RIVER_S, (x, y))
    if d < RIVER_BANK:
        lvl = min(river_level(q), L.POND_WATER_Z + 30 if L.ellipse_r(q, L.POND) < 1.6 else 1e9)
        bed = lvl - 28.0
        h = min(h, L.lerp(bed, h, L.smoothstep(RIVER_FLAT, RIVER_BANK, d))) if d > RIVER_FLAT else min(h, bed)
    return h


def face_colour(pts_stage, n_real):
    cx = sum(p[0] for p in pts_stage) / len(pts_stage)
    cy = sum(p[1] for p in pts_stage) / len(pts_stage)
    slope = math.degrees(math.acos(max(-1.0, min(1.0, n_real.z))))
    if slope > 40:
        return 'Cliff'
    if nearest_on(L.RIVER_S, (cx, cy))[0] < RIVER_FLAT or L.ellipse_r((cx, cy), L.POND) < 1.12:
        return 'Shore'
    s = L.surface(cx, cy)
    return {'Grass': 'Grass', 'GrassLight': 'GrassLight', 'Path': 'Dirt', 'Shore': 'Shore'}[s]


def build_island():
    mb = MB('SM_S02_Diorama_Island')
    P = {}
    for j in range(NV + 1):
        for i in range(NU + 1):
            u, v = -1 + 2 * i / NU, -1 + 2 * j / NV
            x, y = squircle(u, v)
            P[i, j] = (x, y, dio_height(x, y))
    counts = {}

    def tri(a, b, c):
        pa, pb, pc = P[a], P[b], P[c]
        # real-scale normal (UE axes) for the slope / material decision
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

    for j in range(NV):
        for i in range(NU):
            a, b, c, d = (i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)
            # split along the diagonal that follows the terrain better (keeps ridges / cliffs crisp)
            if abs(P[a][2] - P[c][2]) < abs(P[b][2] - P[d][2]):
                tri(a, b, c); tri(a, c, d)
            else:
                tri(a, b, d); tri(b, c, d)
    # border ring (counter-clockwise seen from above in stage axes)
    ring = [(i, 0) for i in range(NU)] + [(NU, j) for j in range(NV)] + [(i, NV) for i in range(NU, 0, -1)] + [(0, j) for j in range(NV, 0, -1)]
    taper = 0.965
    bot_ring = []
    for k in range(len(ring)):
        a, b = ring[k], ring[(k + 1) % len(ring)]
        pa, pb = P[a], P[b]
        ta, tb = DV(*pa), DV(*pb)
        out = Vector(((ta.x + tb.x) / 2, (ta.y + tb.y) / 2, 0)).normalized()
        turf_a, turf_b = ta.z - DZ(4.0), tb.z - DZ(4.0)
        band_a, band_b = min(turf_a, DZ(SIDE_BAND)), min(turf_b, DZ(SIDE_BAND))
        mb.face([ta, tb, Vector((tb.x, tb.y, turf_b)), Vector((ta.x, ta.y, turf_a))], 'Turf', out=out)
        mb.face([Vector((ta.x, ta.y, turf_a)), Vector((tb.x, tb.y, turf_b)), Vector((tb.x, tb.y, band_b)), Vector((ta.x, ta.y, band_a))], 'Earth', out=out)
        ba = Vector((ta.x * taper, ta.y * taper, DZ(BOTTOM)))
        bb = Vector((tb.x * taper, tb.y * taper, DZ(BOTTOM)))
        mb.face([Vector((ta.x, ta.y, band_a)), Vector((tb.x, tb.y, band_b)), bb, ba], 'SoilDark', out=out)
        bot_ring.append(ba)
    mb.face(list(reversed(bot_ring)), 'SoilDark', out=Vector((0, 0, -1)))

    # river ribbon: centre line levels, flat cross-sections, clipped to the outline
    rs = L.RIVER_S
    left, right = [], []
    for k, (x, y) in enumerate(rs):
        a = rs[max(0, k - 1)]; b = rs[min(len(rs) - 1, k + 1)]
        tx, ty = b[0] - a[0], b[1] - a[1]
        t = math.hypot(tx, ty) or 1
        nx, ny = -ty / t, tx / t
        lvl = min(river_level((x, y)), L.POND_WATER_Z + 30 if L.ellipse_r((x, y), L.POND) < 1.6 else 1e9)
        left.append((x + nx * RIVER_HALF, y + ny * RIVER_HALF, lvl))
        right.append((x - nx * RIVER_HALF, y - ny * RIVER_HALF, lvl))
    for k in range(len(rs) - 1):
        quad = [left[k], left[k + 1], right[k + 1], right[k]]
        if max(outline_norm(p[0], p[1]) for p in quad) > 0.985:
            continue
        mb.face([DV(*p) for p in quad], 'Water', out=Vector((0, 0, 1)))
    # pond (fixed level), slightly larger than the real water so the rim reads in the miniature
    cx, cy, rx, ry, rot = L.POND
    pr = []
    for k in range(28):
        a = 2 * math.pi * k / 28
        u, v = math.cos(a) * rx * 1.05, math.sin(a) * ry * 1.05
        pr.append((cx + u * math.cos(rot) - v * math.sin(rot), cy + u * math.sin(rot) + v * math.cos(rot), L.POND_WATER_Z))
    mb.face([DV(*p) for p in pr], 'Water', out=Vector((0, 0, 1)))
    STATS['island_faces'] = counts
    STATS['ridge_top_dcm'] = round(max(p[2] for p in P.values()) * S * E, 1)
    STATS['valley_min_dcm'] = round(min(p[2] for p in P.values()) * S * E, 1)
    return mb.finish(weld=False)


# ---------------------------------------------------------------- landmarks from project FBX sources
SRC = {
    'Cage': os.path.join(ROOT, 'SourceArt/Cage/FBX/SM_Cage.fbx'),
    'CagePlaza': os.path.join(ROOT, 'SourceArt/Stage01Redesign/FBX/SM_S01R_CagePlaza.fbx'),
    'WindmillBody': os.path.join(ROOT, 'SourceArt/Stage01Redesign/FBX/SM_S01R_WindmillBody.fbx'),
    'WindmillRotor': os.path.join(ROOT, 'SourceArt/Stage01Redesign/FBX/SM_S01R_WindmillRotor.fbx'),
    'Watchtower': os.path.join(ROOT, 'SourceArt/Stage01Redesign/FBX/SM_S01R_Watchtower.fbx'),
    'Bridge': os.path.join(ROOT, 'SourceArt/Stage01Redesign/FBX/SM_S01R_WoodBridge.fbx'),
}


def import_source(key):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=SRC[key])
    obs = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
    for o in [o for o in bpy.data.objects if o not in before and o.type != 'MESH']:
        bpy.data.objects.remove(o)
    assert len(obs) == 1, (key, obs)
    ob = obs[0]
    ob.parent = None
    me = ob.data
    me.transform(ob.matrix_world)
    ob.matrix_world = Matrix.Identity(4)
    attr = me.attributes.new('cidx', 'INT', 'FACE')
    for p in me.polygons:
        mat = me.materials[p.material_index] if me.materials else None
        name = mat.name.split('.')[0] if mat else ''
        attr.data[p.index].value = CID[SLOT.get(name, 'White')]
    me.materials.clear()
    return ob


def place(ob, x, y, z_stage, yaw_ue, k, extra=(0.0, 0.0, 0.0), zoff_dcm=0.0):
    """Scale a real-size source by S*k, rotate by the UE yaw and put it on the diorama terrain at stage (x, y)."""
    sc = S * k
    base = DV(x, y, z_stage)
    m = Matrix.Translation(base + Vector((0, 0, DZ(zoff_dcm)))) @ Matrix.Rotation(math.radians(-yaw_ue), 4, 'Z') @ \
        Matrix.Translation(Vector(extra) * sc) @ Matrix.Scale(sc, 4)
    ob.data.transform(m)
    return ob


def build_props():
    obs = []
    cx, cy = L.CAGE
    hz = dio_height(cx, cy)
    plaza = import_source('CagePlaza')
    plaza.data.transform(Matrix.Diagonal((0.62, 0.62, 1.0, 1.0)))
    obs.append(place(plaza, cx, cy, hz, 0, 2.6, zoff_dcm=0.6))
    obs.append(place(import_source('Cage'), cx, cy, hz, 0, 2.6, zoff_dcm=0.8))
    # windmill on its terrace knoll, facing the cage (rotor offset as in s02_dressing, scaled with the body)
    wx, wy = L.LANDMARKS['Windmill']
    wyaw = 200.0     # rotor faces the capture camera (south-south-west); the real one faces the cage
    wz = dio_height(wx, wy)
    obs.append(place(import_source('WindmillBody'), wx, wy, wz, wyaw, 2.2, zoff_dcm=-1))
    obs.append(place(import_source('WindmillRotor'), wx, wy, wz, wyaw, 2.2, extra=(2.5, 0, 7.2), zoff_dcm=-1))
    tx, ty = L.LANDMARKS['Watchtower']
    obs.append(place(import_source('Watchtower'), tx, ty, dio_height(tx, ty), 90, 2.4, zoff_dcm=-1))
    bx, by = L.BRIDGE
    lvl = river_level(nearest_on(L.RIVER_S, (bx, by))[1])
    obs.append(place(import_source('Bridge'), bx, by, lvl, L.BRIDGE_YAW, 1.9, zoff_dcm=0.5))

    mb = MB('Procedural')
    # farm: barn + farmhouse (two buildings only) and the paddock fence
    def house(x, y, yaw_ue, size, wall_col, roof_col, k):
        sx, sy, wall, ridge = [v * S * k / 100.0 for v in size]
        c = DV(x, y, dio_height(x, y))
        yaw = math.radians(-yaw_ue)
        box(mb, c, (sx, sy, wall), yaw, wall_col, c.z - DZ(2))
        co, si = math.cos(yaw), math.sin(yaw)
        P = lambda u, v, w: Vector((c.x + u * co - v * si, c.y + u * si + v * co, c.z - DZ(2) + w))
        ov = sy * 0.12
        e0, e1 = -sx / 2 - ov, sx / 2 + ov
        r0, r1 = -sy / 2 - ov, sy / 2 + ov
        top = ridge
        # roof slabs (ridge along u) with a little thickness, gable triangles in the wall colour
        th = 0.012 * k
        for sgn in (-1, 1):
            eave = r0 if sgn < 0 else r1
            q = [P(e0, eave, wall - th), P(e1, eave, wall - th), P(e1, 0, top - th), P(e0, 0, top - th)]
            q2 = [P(e0, eave, wall), P(e1, eave, wall), P(e1, 0, top), P(e0, 0, top)]
            mb.face(q2, roof_col, out=Vector((0, 0, 1)))
            mb.face(list(reversed(q)), roof_col, out=Vector((0, 0, -1)))
            mb.face([q[0], q[1], q2[1], q2[0]], roof_col, out=(q[0] - P(0, 0, wall)).normalized())
        for eu in (-sx / 2, sx / 2):
            out = (P(eu, 0, wall) - P(0, 0, wall)).normalized()
            mb.face([P(eu, -sy / 2, wall), P(eu, sy / 2, wall), P(eu, 0, top - th)], wall_col, out=out)
        for eu in (e0, e1):
            out = (P(eu, 0, wall) - P(0, 0, wall)).normalized()
            mb.face([P(eu, r0, wall - th), P(eu, 0, top - th), P(eu, 0, top), P(eu, r0, wall)], roof_col, out=out)
            mb.face([P(eu, 0, top - th), P(eu, r1, wall - th), P(eu, r1, wall), P(eu, 0, top)], roof_col, out=out)
        return P, sx, sy, wall

    bx, by = L.LANDMARKS['Barn']
    P, sx, sy, wall = house(bx, by, 55, (950, 650, 430, 760), 'BarnRed', 'Roof', 1.25)
    # white barn door on the long side facing the yard
    d0 = P(-sx * 0.18, -sy / 2 - 0.003, 0); d1 = P(sx * 0.18, -sy / 2 - 0.003, 0)
    mb.face([d0, d1, d1 + Vector((0, 0, wall * 0.78)), d0 + Vector((0, 0, wall * 0.78))], 'White',
            out=(P(0, -sy, 0) - P(0, 0, 0)).normalized())
    hx, hy = L.LANDMARKS['FarmHouse']
    house(hx, hy, 200, (720, 560, 330, 560), 'Ivory', 'Blue', 1.25)
    px0, px1, py0, py1 = -3550.0, -2850.0, 2150.0, 3150.0
    def fence(ax, ay, bx2, by2, gap=None):
        n = max(2, int(math.hypot(bx2 - ax, by2 - ay) / 250))
        pts = [(ax + (bx2 - ax) * t / n, ay + (by2 - ay) * t / n) for t in range(n + 1)]
        for i, (x, y) in enumerate(pts):
            c = DV(x, y, dio_height(x, y))
            box(mb, c, (0.035, 0.035, DZ(12)), 0, 'WoodLight', c.z - DZ(1))
            if i < n:
                x2, y2 = pts[i + 1]
                if gap and math.hypot((x + x2) / 2 - gap[0], (y + y2) / 2 - gap[1]) < 220:
                    continue
                c2 = DV(x2, y2, dio_height(x2, y2))
                mid = (c + c2) / 2
                ln = (c2 - c).length
                yaw = math.atan2(c2.y - c.y, c2.x - c.x)
                for zr in (5.0, 9.5):
                    box(mb, Vector((mid.x, mid.y, 0)), (ln, 0.018, DZ(1.6)), yaw, 'WoodLight', mid.z + DZ(zr - 0.8))
    fence(px0, py0, px1, py0); fence(px1, py0, px1, py1, gap=(px1, (py0 + py1) / 2)); fence(px1, py1, px0, py1); fence(px0, py1, px0, py0)

    # trees: the real placements, thinned; deciduous = faceted crown, pines = stacked cones
    trees = [p for p in D.placements() if p['tag'] == 'Trees' and 'Log' not in p['mesh']]
    kept = 0
    for i, p in enumerate(trees):
        x, y = p['x'], p['y']
        if outline_norm(x, y) > 0.95:
            continue
        if nearest_on(L.RIVER_S, (x, y))[0] < RIVER_BANK or L.ellipse_r((x, y), L.POND) < 1.25:
            continue
        s = p['scale'][0]
        c = DV(x, y, dio_height(x, y))
        z0 = c.z - DZ(2)
        if 'Pine' in p['mesh']:
            hgt = DZ(58 + 14 * hsh(i, 1)) * s / 1.3
            prism(mb, c, DZ(3.2), z0, z0 + hgt * 0.3, 6, 'WoodDark')
            for lvl, (rr, zz, hh) in enumerate(((15, .18, .45), (11.5, .42, .38), (7.5, .66, .36))):
                prism(mb, c, DZ(rr) * s / 1.3, z0 + hgt * zz, z0 + hgt * (zz + hh), 7, 'Pine', r_top=0.0, rot=hsh(i, lvl) * 3)
        else:
            hgt = DZ(26 + 8 * hsh(i, 1)) * s / 1.3
            prism(mb, c, DZ(3.6), z0, z0 + hgt, 6, 'Wood')
            bm = bmesh.new()
            bmesh.ops.create_icosphere(bm, subdivisions=1, radius=1.0)
            rad = DZ(17 + 6 * hsh(i, 2)) * s / 1.3
            for v in bm.verts:
                v.co = Vector((c.x + v.co.x * rad, c.y + v.co.y * rad, z0 + hgt + rad * 0.75 + v.co.z * rad * 0.92))
            for f in bm.faces:
                mb.face([v.co.copy() for v in f.verts], 'Leaf' if (f.index + i) % 4 else 'Grass',
                        out=(f.calc_center_median() - Vector((c.x, c.y, z0 + hgt + rad * 0.75))).normalized())
            bm.free()
        kept += 1
    STATS['trees'] = kept
    # rocks that frame the two drop-down shortcuts + a few on the terrace edge
    rocks = [p for p in D.placements() if p['tag'] in ('Shortcut', 'Terrace')]
    for i, p in enumerate(rocks):
        x, y = p['x'], p['y']
        c = DV(x, y, dio_height(x, y))
        bm = bmesh.new()
        bmesh.ops.create_icosphere(bm, subdivisions=1, radius=1.0)
        r = DZ(11 if p['tag'] == 'Shortcut' else 8)
        for v in bm.verts:
            j = 0.75 + 0.5 * hsh(i * 13 + v.index, 5)
            v.co = Vector((c.x + v.co.x * r * j * 1.2, c.y + v.co.y * r * j, c.z - DZ(3) + max(-0.3, v.co.z) * r * j * 0.9))
        for f in bm.faces:
            mb.face([v.co.copy() for v in f.verts], 'Rock', out=(f.calc_center_median() - c).normalized())
        bm.free()
    STATS['rocks'] = len(rocks)
    obs.append(mb.finish(weld=False))
    return join(obs, 'SM_S02_Diorama_Props')


def join(obs, name):
    bpy.ops.object.select_all(action='DESELECT')
    for o in obs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = ob.data.name = name
    return ob


# ---------------------------------------------------------------- colours, export, preview
def apply_colours(ob, jitter=0.035):
    me = ob.data
    ci = me.attributes['cidx']
    col = me.color_attributes.new('Col', 'BYTE_COLOR', 'CORNER')
    for p in me.polygons:
        c = PAL[NAMES[ci.data[p.index].value]]
        j = 1.0 + jitter * (hsh(p.index, 3) - 0.5) * 2
        rgba = (min(1, c[0] * j), min(1, c[1] * j), min(1, c[2] * j), 1.0)
        for li in p.loop_indices:
            col.data[li].color = rgba
    me.color_attributes.active_color = col
    me.attributes.active_color = col
    mat = bpy.data.materials.get('Diorama') or bpy.data.materials.new('Diorama')
    me.materials.clear()
    me.materials.append(mat)
    for p in me.polygons:
        p.use_smooth = False


def export_fbx(ob):
    os.makedirs(OUT_FBX, exist_ok=True)
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT_FBX, ob.name + '.fbx'), use_selection=True, object_types={'MESH'},
                             mesh_smooth_type='FACE', add_leaf_bones=False, bake_anim=False, path_mode='STRIP',
                             colors_type='LINEAR')   # UE's VertexColor node reads the stored bytes as-is (no sRGB decode)


def preview(objs):
    """Workbench render from the same orthographic camera as ABoarStagePreviewActor (fake key light baked per face)."""
    sc = bpy.context.scene
    root = bpy.data.objects.new('DioramaYaw', None)
    sc.collection.objects.link(root)
    for name, z in objs:
        o = bpy.data.objects[name]
        o.parent = root
        o.location.z = z
        me = o.data
        src = me.color_attributes['Col']
        prev = me.color_attributes.new('Prev', 'FLOAT_COLOR', 'CORNER')
        mw = Matrix.Rotation(math.radians(-YAW_UE), 4, 'Z')
        for p in me.polygons:
            n = (mw.to_3x3() @ p.normal)
            shade = 0.7 + 0.3 * n.dot(KEY)
            for li in p.loop_indices:
                c = src.data[li].color
                prev.data[li].color = (c[0] * shade, c[1] * shade, c[2] * shade, 1)
        me.color_attributes.active_color = prev
    root.rotation_euler.z = math.radians(-YAW_UE)   # UE part yaw
    cam = bpy.data.objects.new('Cam', bpy.data.cameras.new('Cam'))
    sc.collection.objects.link(cam)
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = 12.5
    cam.location = (-11.0, 11.0, 11.0)
    d = Vector((0, 0, 0.8)) - cam.location
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    cam.data.clip_end = 100
    sc.camera = cam
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading
    sh.light, sh.color_type = 'FLAT', 'VERTEX'
    sc.view_settings.view_transform = 'Standard'
    sc.render.resolution_x, sc.render.resolution_y = 1024, 768
    sc.render.film_transparent = True
    os.makedirs(OUT_PNG, exist_ok=True)
    sc.render.filepath = os.path.join(OUT_PNG, 'diorama_capture_view.png')
    bpy.ops.render.render(write_still=True)
    # top view (north up) for layout review
    root.rotation_euler.z = 0
    cam.location = (0, 0, 30)
    cam.rotation_euler = (0, 0, math.radians(-90))
    cam.data.ortho_scale = 11
    sc.render.filepath = os.path.join(OUT_PNG, 'diorama_top.png')
    bpy.ops.render.render(write_still=True)
    # low south view (player side)
    cam.location = (-14, 0, 3.2)
    d = Vector((0, 0, 0.6)) - cam.location
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    cam.data.ortho_scale = 11
    sc.render.filepath = os.path.join(OUT_PNG, 'diorama_south_low.png')
    bpy.ops.render.render(write_still=True)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    island = build_island()
    props = build_props()
    for ob in (island, props):
        apply_colours(ob)
        STATS[ob.name] = {'tris': sum(len(p.vertices) - 2 for p in ob.data.polygons), 'verts': len(ob.data.vertices)}
        export_fbx(ob)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, 'S02_Diorama.blend'))
    if RENDER:
        preview([(island.name, DZ(ISLAND_Z)), (props.name, DZ(ISLAND_Z))])
    os.makedirs(os.path.join(STAGE, 'Logs'), exist_ok=True)
    with open(os.path.join(STAGE, 'Logs', 'diorama_stats.json'), 'w') as f:
        json.dump(STATS, f, indent=1)
    print('[S02Diorama] COMPLETE', json.dumps(STATS))


if __name__ == '__main__':      # also imported as a library by the Stage 1 diorama builder
    main()
