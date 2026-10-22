"""Stage01 Redesign asset builder (Blender 5.x, background).
blender -b --factory-startup --python build_assets.py -- [--only Name,Name] [--no-render]

All geometry is authored in UE space (cm, UE axes) and converted once in MB.v():
UE (x, y, z) cm -> Blender (x/100, -y/100, z/100) m.  Default FBX export brings it back to UE unchanged.
Material slot names are the keys of UE_MATS in ue_build_level.py.
Writes Blend/S01R_Assets.blend, FBX/<asset>.fbx, Screenshots/blender_assets/<asset>_<view>.png.
"""
import bpy, bmesh, math, os, sys
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import s01r_layout as L

ROOT = os.path.dirname(HERE)
argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
ONLY = set(argv[argv.index('--only') + 1].split(',')) if '--only' in argv else None
RENDER = '--no-render' not in argv

COLORS = {
    'Grass': (0.20, 0.45, 0.07), 'GrassLight': (0.33, 0.58, 0.12), 'Cliff': (0.40, 0.42, 0.36), 'Shore': (0.33, 0.24, 0.13),
    'Path': (0.62, 0.43, 0.20), 'Ivory': (0.80, 0.86, 0.83), 'Blue': (0.05, 0.22, 0.48), 'Cyan': (0.10, 0.65, 0.82),
    'Yellow': (0.97, 0.66, 0.08), 'Glass': (0.12, 0.35, 0.45), 'Machine': (0.25, 0.32, 0.36), 'Wood': (0.62, 0.42, 0.17),
    'WoodDark': (0.33, 0.20, 0.09), 'WoodLight': (0.74, 0.53, 0.25), 'Water': (0.15, 0.60, 0.80), 'Rock': (0.42, 0.44, 0.40),
}


def rot(p, yaw):
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return (p[0] * c - p[1] * s, p[0] * s + p[1] * c, p[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


class MB:
    """Mesh builder in UE space. Faces get an outward hint so winding survives the Y mirror."""

    def __init__(self, name):
        self.name, self.bm, self.mats = name, bmesh.new(), []

    @staticmethod
    def v(p):
        return Vector((p[0] / 100.0, -p[1] / 100.0, p[2] / 100.0))

    def face(self, pts, mat, out):
        """pts: UE points. out: UE direction the face should look at."""
        if mat not in self.mats:
            self.mats.append(mat)
        f = self.bm.faces.new([self.bm.verts.new(self.v(p)) for p in pts])
        f.material_index = self.mats.index(mat)
        f.normal_update()
        if f.normal.dot(Vector((out[0], -out[1], out[2]))) < 0:
            f.normal_flip()
        return f

    def poly_auto(self, pts, mat, inside):
        """Face oriented away from a UE point that lies inside the solid."""
        c = [sum(p[i] for p in pts) / len(pts) for i in range(3)]
        return self.face(pts, mat, (c[0] - inside[0], c[1] - inside[1], c[2] - inside[2]))

    def box(self, c, s, mat, yaw=0.0, mats=None, top=None, skip_bottom=False):
        """Axis box (in local yaw) centred at c with size s. mats: per-side override dict."""
        hx, hy, hz = s[0] / 2, s[1] / 2, s[2] / 2
        P = lambda x, y, z: add(rot((x, y, 0), yaw), (c[0], c[1], c[2] + z))
        sides = {
            'top': ([(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)], (0, 0, 1)),
            'bottom': ([(-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz), (hx, -hy, -hz)], (0, 0, -1)),
            '+x': ([(hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz), (hx, -hy, hz)], (1, 0, 0)),
            '-x': ([(-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz)], (-1, 0, 0)),
            '+y': ([(-hx, hy, -hz), (-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz)], (0, 1, 0)),
            '-y': ([(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)], (0, -1, 0)),
        }
        for k, (pts, n) in sides.items():
            if skip_bottom and k == 'bottom':
                continue
            m = (mats or {}).get(k, top if (k == 'top' and top) else mat)
            self.face([P(*p) for p in pts], m, rot(n, yaw))

    def beam(self, a, b, w, h, mat):
        """Box from point a to point b (UE), cross-section w (horizontal) x h (vertical-ish)."""
        d = Vector((b[0] - a[0], b[1] - a[1], b[2] - a[2]))
        ln = d.length
        f = d.normalized()
        side = f.cross(Vector((0, 0, 1)))
        side = side.normalized() if side.length > 1e-4 else Vector((0, 1, 0))
        up = side.cross(f).normalized()
        A = Vector(a)
        corners = []
        for t in (0, ln):
            for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                q = A + f * t + side * (sx * w / 2) + up * (sz * h / 2)
                corners.append(tuple(q))
        c = tuple(A + f * (ln / 2))
        idx = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        for q in idx:
            self.poly_auto([corners[i] for i in q], mat, c)

    def prism(self, c, r0, r1, z0, z1, seg, mat, cap_top=True, cap_bot=False, top_mat=None, yaw0=0.0, axis='z'):
        """Frustum along local axis. c: base centre (UE). axis 'x' lays it along +X."""
        def P(r, a, z):
            p = (r * math.cos(a), r * math.sin(a), z)
            if axis == 'x':
                p = (p[2], p[1], p[0])
            return add(p, c)
        ins = P(0, 0, (z0 + z1) / 2)
        for i in range(seg):
            a0 = math.radians(yaw0) + 2 * math.pi * i / seg
            a1 = math.radians(yaw0) + 2 * math.pi * (i + 1) / seg
            self.poly_auto([P(r0, a0, z0), P(r0, a1, z0), P(r1, a1, z1), P(r1, a0, z1)], mat, ins)
        angs = [math.radians(yaw0) + 2 * math.pi * i / seg for i in range(seg)]
        if cap_top and r1 > 0:
            self.poly_auto([P(r1, a, z1) for a in angs], top_mat or mat, P(0, 0, z1 - 1))
        if cap_bot and r0 > 0:
            self.poly_auto([P(r0, a, z0) for a in angs], mat, P(0, 0, z0 + 1))

    def finish(self, smooth=False, weld=True):
        bm = self.bm
        if weld:
            bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0005)
        uv = bm.loops.layers.uv.new('UVMap')
        for f in bm.faces:
            n = f.normal
            ax = max(range(3), key=lambda i: abs(n[i]))
            for loop in f.loops:
                co = loop.vert.co
                u, w = [(co.y, co.z), (co.x, co.z), (co.x, co.y)][ax]
                loop[uv].uv = (u, w)
            f.smooth = smooth
        me = bpy.data.meshes.new(self.name)
        bm.to_mesh(me)
        bm.free()
        for m in self.mats:
            me.materials.append(material(m))
        ob = bpy.data.objects.new(self.name, me)
        col = bpy.data.collections.new(self.name)
        bpy.context.scene.collection.children.link(col)
        col.objects.link(ob)
        return ob


def material(name):
    m = bpy.data.materials.get(name)
    if not m:
        m = bpy.data.materials.new(name)
        m.diffuse_color = COLORS.get(name, (1, 0, 1)) + (1.0,)
        m.use_nodes = True
        bsdf = m.node_tree.nodes.get('Principled BSDF')
        if bsdf:
            bsdf.inputs['Base Color'].default_value = COLORS.get(name, (1, 0, 1)) + (1.0,)
            bsdf.inputs['Roughness'].default_value = 0.85
    return m


# ------------------------------------------------------------------ terrain & ground
def build_terrain():
    mb = MB('SM_S01R_Terrain')
    X0, X1, Y0, Y1, S = -6000, 6000, -5600, 3800, 100
    nx, ny = (X1 - X0) // S, (Y1 - Y0) // S
    g = {}
    for j in range(ny + 1):
        for i in range(nx + 1):
            x, y = X0 + i * S, Y0 + j * S
            if 0 < i < nx and 0 < j < ny:
                x += (L._hash(i, j, 91) - .5) * 34
                y += (L._hash(i, j, 92) - .5) * 34
            g[i, j] = (x, y, L.height(x, y))
    for j in range(ny):
        for i in range(nx):
            a, b, c, d = g[i, j], g[i + 1, j], g[i + 1, j + 1], g[i, j + 1]
            tris = [(a, b, c), (a, c, d)] if L._hash(i, j, 5) > .5 else [(a, b, d), (b, c, d)]
            for t in tris:
                n = (Vector(t[1]) - Vector(t[0])).cross(Vector(t[2]) - Vector(t[0])).normalized()
                cx, cy = sum(p[0] for p in t) / 3, sum(p[1] for p in t) / 3
                mat = 'Cliff' if abs(n.z) < 0.78 else L.surface(cx, cy)
                mb.face(list(t), mat, (0, 0, 1))
    return mb.finish()


def chaikin(pts, it=3, closed=False):
    for _ in range(it):
        out = [] if closed else [pts[0]]
        rng = range(len(pts) - (0 if closed else 1))
        for i in rng:
            p, q = pts[i], pts[(i + 1) % len(pts)]
            out += [(p[0] * .75 + q[0] * .25, p[1] * .75 + q[1] * .25), (p[0] * .25 + q[0] * .75, p[1] * .25 + q[1] * .75)]
        if not closed:
            out.append(pts[-1])
        pts = out
    return pts


def resample(pts, step):
    out = [pts[0]]
    acc = 0.0
    for i in range(len(pts) - 1):
        a, b = Vector(pts[i]), Vector(pts[i + 1])
        seg = (b - a).length
        while acc + step <= seg:
            acc += step
            out.append(tuple(a + (b - a) * (acc / seg)))
        acc -= seg
    if (Vector(out[-1]) - Vector(pts[-1])).length > step * .3:
        out.append(pts[-1])
    return out


def ribbon(mb, pts, width, zoff, mat, seed, zfunc=None, cols=3):
    n = len(pts)
    rows = []
    for i, p in enumerate(pts):
        a, b = Vector(pts[max(i - 1, 0)]), Vector(pts[min(i + 1, n - 1)])
        t = (b - a).normalized()
        nrm = Vector((-t.y, t.x))
        w = width / 2 + (L._hash(i, seed, 3) - .5) * 26
        row = []
        for k in range(cols):
            s = -1 + 2 * k / (cols - 1)
            x, y = p[0] + nrm.x * w * s, p[1] + nrm.y * w * s
            z = zfunc(x, y, i / (n - 1)) if zfunc else L.height(x, y) + zoff
            row.append((x, y, z))
        rows.append(row)
    for i in range(n - 1):
        for k in range(cols - 1):
            mb.face([rows[i][k], rows[i][k + 1], rows[i + 1][k + 1], rows[i + 1][k]], mat, (0, 0, 1))


def build_paths():
    mb = MB('SM_S01R_Paths')
    for idx, (name, (pts, w, z)) in enumerate(L.PATHS.items()):
        closed = name == 'Ring'
        p = pts if closed else chaikin(list(pts), 3)
        ribbon(mb, resample(p, 55), w, z, 'Path', idx + 1)
    return mb.finish()


def build_plaza():
    mb = MB('SM_S01R_CagePlaza')
    seg, top = 64, 5.0
    rings = [0, 150, 320, 480, 505, 600, 690]

    def mat_for(ri, i):
        r0 = rings[ri]
        entry = min((i - e) % seg for e in (0, 16, 32, 48)) in (0,) or min((e - i - 1) % seg for e in (0, 16, 32, 48)) == 0
        if r0 == 320 and i % 8 in (3, 4):
            return 'Cyan'
        if r0 == 480:
            return 'Cyan'
        if r0 == 505:
            return 'Yellow' if entry else 'Blue'
        if r0 == 600:
            return 'Blue'
        return 'Ivory'
    zr = lambda r: top if r <= 600 else -3.0
    for ri in range(len(rings) - 1):
        for i in range(seg):
            a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
            r0, r1 = rings[ri], rings[ri + 1]
            P = lambda r, a: (math.cos(a) * r, math.sin(a) * r, zr(r))
            pts = [P(r0, a0), P(r1, a0), P(r1, a1), P(r0, a1)] if r0 > 0 else [(0, 0, top), P(r1, a0), P(r1, a1)]
            mb.face(pts, mat_for(ri, i), (0, 0, 1))
    return mb.finish()


def build_water():
    obs = []
    mb = MB('SM_S01R_PondWater')
    cx, cy, rx, ry, r = L.POND
    seg = 56
    ring = []
    for i in range(seg):
        a = 2 * math.pi * i / seg
        u, v = math.cos(a) * rx * 1.08, math.sin(a) * ry * 1.08
        ring.append((cx + u * math.cos(r) - v * math.sin(r), cy + u * math.sin(r) + v * math.cos(r), L.POND_WATER_Z))
    for i in range(seg):
        mb.face([(cx, cy, L.POND_WATER_Z), ring[i], ring[(i + 1) % seg]], 'Water', (0, 0, 1))
    obs.append(mb.finish())

    mb = MB('SM_S01R_StreamWater')
    pts = resample(chaikin(list(L.STREAM), 3), 60)
    zf = lambda x, y, t: L.STREAM_WATER_Z + (L.POND_WATER_Z - L.STREAM_WATER_Z) * t
    ribbon(mb, pts, 400, 0, 'Water', 77, zfunc=zf)
    sx, sy = L.STREAM[0]
    pool = [(sx + math.cos(2 * math.pi * i / 20) * 230, sy + math.sin(2 * math.pi * i / 20) * 200, L.STREAM_WATER_Z + .3) for i in range(20)]
    for i in range(20):
        mb.face([(sx, sy, L.STREAM_WATER_Z + .3), pool[i], pool[(i + 1) % 20]], 'Water', (0, 0, 1))
    obs.append(mb.finish())

    # waterfall: a sheet hugging the back-left cliff face, slightly in front of it
    mb = MB('SM_S01R_Waterfall')
    wx = L.WATERFALL[0]
    ys = [-3460 + 20 * k for k in range(15)]
    xs = [wx - 120, wx - 40, wx + 40, wx + 120]
    grid = []
    for y in ys:
        row = []
        for k, x in enumerate(xs):
            bulge = 6 if k in (1, 2) else 0
            z = max(L.height(x, y), L.STREAM_WATER_Z) + 10 + bulge
            row.append((x, y + 14, z))
        grid.append(row)
    for i in range(len(ys) - 1):
        for k in range(len(xs) - 1):
            mb.face([grid[i][k], grid[i][k + 1], grid[i + 1][k + 1], grid[i + 1][k]], 'Water', (0, .7, .7))
    obs.append(mb.finish())
    return obs


# ------------------------------------------------------------------ structures
def build_bridge():
    mb = MB('SM_S01R_WoodBridge')
    half, hw, n = L.BRIDGE_LEN / 2, L.BRIDGE_W / 2, 20
    top = lambda x: 6 + 34 * math.cos(math.pi * x / L.BRIDGE_LEN)
    xs = [-half + L.BRIDGE_LEN * i / n for i in range(n + 1)]
    for i in range(n):
        x0, x1 = xs[i], xs[i + 1]
        t0, t1 = top(x0), top(x1)
        b0, b1 = t0 - 14, t1 - 14
        mb.face([(x0, -hw, t0), (x1, -hw, t1), (x1, hw, t1), (x0, hw, t0)], 'Wood' if i % 2 else 'WoodLight', (0, 0, 1))
        mb.face([(x0, -hw, b0), (x0, hw, b0), (x1, hw, b1), (x1, -hw, b1)], 'WoodDark', (0, 0, -1))
        mb.face([(x0, hw, b0), (x0, hw, t0), (x1, hw, t1), (x1, hw, b1)], 'WoodDark', (0, 1, 0))
        mb.face([(x0, -hw, b0), (x1, -hw, b1), (x1, -hw, t1), (x0, -hw, t0)], 'WoodDark', (0, -1, 0))
    for x in (-half, half):
        mb.face([(x, -hw, top(x) - 14), (x, hw, top(x) - 14), (x, hw, top(x)), (x, -hw, top(x))], 'WoodDark', (1 if x > 0 else -1, 0, 0))
    # stringers under the deck edges
    for y in (-hw + 10, hw - 10):
        for i in range(0, n, 2):
            a, b = xs[i], xs[i + 2]
            mb.beam((a, y, top(a) - 24), (b, y, top(b) - 24), 16, 20, 'WoodDark')
    # piles and rails
    for x in (-200, 200):
        for y in (-hw + 12, hw - 12):
            mb.box((x, y, (top(x) - 14 - 50) / 2), (16, 16, top(x) - 14 + 50), 'WoodDark')
    posts = [-280, -140, 0, 140, 280]
    for y in (-hw + 6, hw - 6):
        for x in posts:
            mb.box((x, y, top(x) + 44), (12, 12, 88), 'WoodDark', mats={'top': 'Wood'})
        for a, b in zip(posts, posts[1:]):
            mb.beam((a, y, top(a) + 84), (b, y, top(b) + 84), 10, 10, 'Wood')
            mb.beam((a, y, top(a) + 44), (b, y, top(b) + 44), 8, 8, 'Wood')
    return mb.finish()


def build_watchtower():
    mb = MB('SM_S01R_Watchtower')
    D, H, P = 160.0, L.TOWER_DECK_RISE, 145.0
    mb.box((0, 0, H - 8), (2 * D, 2 * D, 16), 'WoodDark', top='Wood')
    for sx in (-1, 1):
        for sy in (-1, 1):
            mb.box((sx * P, sy * P, (H + 240 - 20) / 2 - 10), (18, 18, H + 240 + 20), 'WoodDark')
    for a, b in [((-P, -P), (P, -P)), ((-P, P), (P, P)), ((P, -P), (P, P))]:
        mb.beam((a[0], a[1], 10), (b[0], b[1], H - 30), 10, 10, 'WoodDark')
        mb.beam((b[0], b[1], 10), (a[0], a[1], H - 30), 10, 10, 'WoodDark')
    # railings on three sides, west (-X) open for the ramp
    rails = [((-D + 6, -D + 6), (D - 6, -D + 6)), ((-D + 6, D - 6), (D - 6, D - 6)), ((D - 6, -D + 6), (D - 6, D - 6))]
    for a, b in rails:
        for k in range(5):
            t = k / 4
            mb.box((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, H + 48), (10, 10, 96), 'WoodDark', top='Wood')
        mb.beam((a[0], a[1], H + 92), (b[0], b[1], H + 92), 10, 10, 'Wood')
        mb.beam((a[0], a[1], H + 50), (b[0], b[1], H + 50), 8, 8, 'Wood')
    # roof: eave slab + pyramid + cyan beacon (research touch)
    R, Z = 200.0, H + 240.0
    mb.box((0, 0, Z + 6), (2 * R, 2 * R, 12), 'Blue')
    apex = (0, 0, Z + 92)
    cs = [(-R, -R, Z + 12), (R, -R, Z + 12), (R, R, Z + 12), (-R, R, Z + 12)]
    for i in range(4):
        mb.poly_auto([cs[i], cs[(i + 1) % 4], apex], 'Blue', (0, 0, Z))
    mb.prism((0, 0, Z + 88), 18, 0, 0, 30, 4, 'Cyan', cap_top=False, cap_bot=True)
    # ramp from the open west side down to the hill top
    RL, RW = 480.0, 80.0
    x0, x1 = -D, -D - RL
    n = 12
    for i in range(n):
        xa, xb = x0 + (x1 - x0) * i / n, x0 + (x1 - x0) * (i + 1) / n
        za, zb = H - (H + 4) * i / n, H - (H + 4) * (i + 1) / n
        mb.face([(xa, -RW, za), (xa, RW, za), (xb, RW, zb), (xb, -RW, zb)], 'Wood' if i % 2 else 'WoodLight', (0, 0, 1))
        mb.face([(xa, RW, za - 12), (xb, RW, zb - 12), (xb, RW, zb), (xa, RW, za)], 'WoodDark', (0, 1, 0))
        mb.face([(xa, -RW, za - 12), (xa, -RW, za), (xb, -RW, zb), (xb, -RW, zb - 12)], 'WoodDark', (0, -1, 0))
        mb.face([(xa, -RW, za - 12), (xb, -RW, zb - 12), (xb, RW, zb - 12), (xa, RW, za - 12)], 'WoodDark', (0, 0, -1))
    for y in (-RW + 5, RW - 5):
        pts = [x0 + (x1 - x0) * k / 3 for k in range(4)]
        zz = [H - (H + 4) * k / 3 for k in range(4)]
        for x, z in zip(pts, zz):
            mb.box((x, y, z + 45 - (z + 12) / 2), (10, 10, 90 + z + 12), 'WoodDark', top='Wood')
        mb.beam((pts[0], y, zz[0] + 88), (pts[-1], y, zz[-1] + 88), 9, 9, 'Wood')
    return mb.finish()


def build_windmill():
    mb = MB('SM_S01R_WindmillBody')
    mb.prism((0, 0, 0), 262, 250, -30, 26, 8, 'Blue', yaw0=22.5)
    rz = [(26, 225), (300, 198), (334, 194), (640, 165)]
    mats = ['Ivory', 'Blue', 'Ivory']
    for (za, ra), (zb, rb), m in zip(rz, rz[1:], mats):
        mb.prism((0, 0, 0), ra, rb, za, zb, 8, m, cap_top=False, yaw0=22.5)
    mb.prism((0, 0, 0), 205, 200, 630, 652, 8, 'Ivory', cap_bot=True, yaw0=22.5)
    mb.prism((0, 0, 0), 200, 0, 652, 860, 8, 'Blue', cap_top=False, yaw0=22.5)
    mb.prism((0, 0, 860), 16, 0, 0, 40, 6, 'Yellow', cap_top=False, cap_bot=True)
    # door, windows (front faces +X), hub mount
    mb.box((210, 0, 115), (24, 100, 180), 'Blue', mats={'top': 'Yellow'})
    mb.box((206, 0, 212), (28, 128, 14), 'Yellow')
    for z, y in ((420, 0), (540, 0)):
        mb.box((186 - (z - 400) * .05, y, z), (16, 52, 60), 'Yellow')
    for side in (-1, 1):
        mb.box((0, side * 196, 420), (52, 16, 60), 'Cyan')
    mb.box((175, 0, 720), (110, 70, 70), 'Ivory', mats={'top': 'Blue'})
    return mb.finish()


def build_windmill_rotor():
    mb = MB('SM_S01R_WindmillRotor')
    mb.prism((-20, 0, 0), 36, 36, 0, 50, 8, 'Ivory', cap_bot=True, axis='x')
    mb.prism((30, 0, 0), 30, 0, 0, 38, 8, 'Cyan', cap_top=False, axis='x')
    for k in range(4):
        a = math.radians(45 + 90 * k)
        d = (0, math.cos(a), math.sin(a))
        side = (0, -math.sin(a), math.cos(a))
        P = lambda r, s, x=10: (x, d[1] * r + side[1] * s, d[2] * r + side[2] * s)
        mb.beam(P(30, 0), P(440, 0), 14, 14, 'WoodDark')
        sail = 'Cyan' if k % 2 == 0 else 'Ivory'
        q = [P(120, 8, 4), P(410, 8, 4), P(410, 92, 4), P(120, 92, 4)]
        mb.face(q, sail, (1, 0, 0))
        mb.face(list(reversed([P(120, 8, -2), P(410, 8, -2), P(410, 92, -2), P(120, 92, -2)])), sail, (-1, 0, 0))
        for r in (120, 215, 315, 410):
            mb.beam(P(r, 0, 6), P(r, 96, 6), 8, 8, 'Ivory')
        mb.beam(P(120, 96, 6), P(410, 96, 6), 8, 8, 'Ivory')
        mb.box(P(425, 0, 10), (16, 26, 26), 'Yellow')
    return mb.finish()


def build_facility():
    obs = []
    mb = MB('SM_S01R_FacilityMain')
    mb.box((0, 0, 20), (1700, 760, 40), 'Blue')
    mb.box((0, 0, 250), (1200, 520, 420), 'Ivory')
    mb.box((0, 263, 310), (1000, 8, 110), 'Glass')
    mb.box((0, 266, 250), (1040, 10, 12), 'Cyan')
    mb.box((0, 266, 372), (1040, 10, 12), 'Cyan')
    mb.box((0, 0, 480), (1280, 600, 40), 'Blue')
    mb.box((-300, -60, 540), (260, 200, 80), 'Machine', top='Ivory')
    mb.box((260, -40, 530), (180, 180, 60), 'Machine', top='Cyan')
    mb.box((0, 266, 160), (180, 10, 240), 'Blue')
    for x in (-110, 110):
        mb.box((x, 270, 160), (30, 20, 250), 'Yellow')
    mb.box((0, 270, 295), (250, 20, 26), 'Yellow')
    mb.box((870, 0, 180), (520, 440, 280), 'Ivory')
    mb.box((870, 223, 230), (520, 8, 22), 'Cyan')
    mb.box((870, 0, 330), (560, 480, 24), 'Blue')
    mb.prism((-800, 0, 40), 160, 150, 0, 600, 12, 'Ivory', cap_top=False)
    mb.prism((-800, 0, 40), 156, 156, 480, 520, 12, 'Cyan', cap_top=False)
    mb.prism((-800, 0, 640), 175, 0, 0, 90, 12, 'Blue', cap_top=False, cap_bot=True)
    obs.append(mb.finish())

    mb = MB('SM_S01R_FacilityDome')
    mb.prism((0, 0, 0), 340, 340, 0, 60, 16, 'Blue', cap_top=False)
    mb.prism((0, 0, 0), 346, 346, 60, 82, 16, 'Cyan', cap_top=False, cap_bot=True)
    rings = [(330, 82), (305, 200), (235, 300), (130, 370), (0, 395)]
    for (ra, za), (rb, zb), m in zip(rings, rings[1:], ['Ivory', 'Ivory', 'Ivory', 'Cyan']):
        mb.prism((0, 0, 0), ra, rb, za, zb, 16, m, cap_top=False)
    obs.append(mb.finish())

    mb = MB('SM_S01R_FacilityAntenna')
    mb.box((0, 0, 20), (130, 130, 40), 'Blue')
    mb.prism((0, 0, 40), 20, 14, 0, 560, 8, 'Machine')
    tilt = math.radians(35)
    centre = (0, 30, 610)
    dish = [(0, 0), (60, 10), (120, 34), (165, 66)]
    def T(r, a, d):  # dish local (radial, angle, depth) -> UE; axis tilted toward +Y and up
        x, y, z = r * math.cos(a), r * math.sin(a), d
        y2 = y * math.cos(tilt) + z * math.sin(tilt)
        z2 = -y * math.sin(tilt) + z * math.cos(tilt)
        return (centre[0] + x, centre[1] + y2, centre[2] + z2)
    seg = 16
    for (ra, da), (rb, db) in zip(dish, dish[1:]):
        for i in range(seg):
            a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
            q = [T(ra, a0, da), T(rb, a0, db), T(rb, a1, db), T(ra, a1, da)]
            if ra == 0:
                q = [T(0, 0, da), T(rb, a0, db), T(rb, a1, db)]
            mb.face(q, 'Ivory', (0, math.sin(tilt), math.cos(tilt)))
            mb.face(list(reversed([(p[0], p[1], p[2] - 2) for p in q])), 'Blue', (0, -math.sin(tilt), -math.cos(tilt)))
    mb.beam(T(0, 0, 0), T(0, 0, 120), 8, 8, 'Machine')
    mb.box(T(0, 0, 126), (18, 18, 18), 'Cyan')
    obs.append(mb.finish())
    return obs


def build_fence():
    obs = []
    mb = MB('SM_S01R_ResearchFence')
    mb.box((-200, 0, 90), (22, 22, 180), 'Ivory')
    mb.box((-200, 0, 187), (26, 26, 14), 'Cyan')
    mb.box((0, 0, 15), (380, 16, 30), 'Blue')
    mb.box((0, 0, 85), (380, 6, 110), 'Cyan')
    mb.box((0, 0, 152), (380, 12, 14), 'Ivory')
    obs.append(mb.finish())
    mb = MB('SM_S01R_ResearchFencePost')
    mb.box((0, 0, 90), (22, 22, 180), 'Ivory')
    mb.box((0, 0, 187), (26, 26, 14), 'Cyan')
    obs.append(mb.finish())
    mb = MB('SM_S01R_ResearchGate')
    for x in (-230, 230):
        mb.box((x, 0, 140), (60, 60, 280), 'Ivory', top='Cyan')
        mb.box((x, 0, 12), (74, 74, 24), 'Blue')
    mb.box((0, 0, 305), (540, 60, 50), 'Blue')
    mb.box((0, 31, 305), (300, 4, 14), 'Cyan')
    mb.box((0, 0, 125), (400, 12, 230), 'Blue')
    for z in (70, 120, 170):
        mb.box((0, 7, z), (320, 4, 10), 'Cyan')
    for k in range(8):
        mb.box((-175 + 50 * k, 8, 20), (24, 4, 22), 'Yellow')
    obs.append(mb.finish())
    return obs


def build_props():
    obs = []
    mb = MB('SM_S01R_GuideBeacon')
    mb.box((0, 0, 5), (44, 44, 10), 'Blue')
    mb.prism((0, 0, 10), 15, 12, 0, 100, 6, 'Ivory', cap_top=False)
    mb.prism((0, 0, 110), 17, 17, 0, 22, 6, 'Cyan', cap_bot=True)
    mb.prism((0, 0, 132), 12, 0, 0, 10, 6, 'Ivory', cap_top=False)
    obs.append(mb.finish())

    mb = MB('SM_S01R_Bollard')
    mb.box((0, 0, 5), (40, 40, 10), 'Blue')
    mb.prism((0, 0, 10), 14, 14, 0, 58, 8, 'Ivory', cap_top=False)
    mb.prism((0, 0, 68), 16, 16, 0, 12, 8, 'Cyan', cap_bot=True)
    obs.append(mb.finish())

    mb = MB('SM_S01R_ResearchCrate')
    mb.box((0, 0, 40), (96, 96, 76), 'Ivory')
    for sx in (-1, 1):
        for sy in (-1, 1):
            mb.box((sx * 46, sy * 46, 40), (12, 12, 80), 'Blue')
    mb.box((0, 0, 40), (100, 100, 10), 'Cyan', mats={'top': 'Ivory', 'bottom': 'Ivory'})
    mb.box((0, 49, 58), (30, 3, 16), 'Yellow')
    obs.append(mb.finish())

    mb = MB('SM_S01R_Signpost')
    mb.box((0, 0, 90), (14, 14, 180), 'WoodDark', top='Wood')
    for z, yaw in ((150, 0), (112, 140)):
        c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        R = lambda x, y, zz: (x * c - y * s, x * s + y * c, zz)
        pts = [(10, -5), (90, -5), (110, 15), (90, 35), (10, 35)]
        top = [R(px, -6, z + py - 15) for px, py in pts]
        bot = [R(px, 6, z + py - 15) for px, py in pts]
        mb.face(top, 'Blue', R(0, -1, 0))
        mb.face(bot, 'Blue', R(0, 1, 0))
        for i in range(len(pts)):
            a, b = i, (i + 1) % len(pts)
            mb.poly_auto([top[a], top[b], bot[b], bot[a]], 'Ivory', R(50, 0, z))
        mb.box(R(104, 0, z), (6, 14, 6), 'Cyan', yaw=yaw)
    obs.append(mb.finish())
    return obs


BUILDERS = [('Terrain', build_terrain), ('Paths', build_paths), ('Plaza', build_plaza), ('Water', build_water),
            ('Bridge', build_bridge), ('Watchtower', build_watchtower), ('Windmill', build_windmill),
            ('WindmillRotor', build_windmill_rotor), ('Facility', build_facility), ('Fence', build_fence), ('Props', build_props)]


def export_fbx(ob):
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=os.path.join(ROOT, 'FBX', ob.name + '.fbx'), use_selection=True,
                             object_types={'MESH'}, mesh_smooth_type='FACE', use_mesh_modifiers=True,
                             add_leaf_bones=False, bake_anim=False, path_mode='STRIP')


def render_views(ob):
    """Four oblique views around the object plus a top view (360-degree check)."""
    sc = bpy.context.scene
    for o in bpy.data.objects:
        if o.type == 'MESH':
            o.hide_render = o is not ob
    bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    lo = Vector((min(v.x for v in bb), min(v.y for v in bb), min(v.z for v in bb)))
    hi = Vector((max(v.x for v in bb), max(v.y for v in bb), max(v.z for v in bb)))
    ctr, rad = (lo + hi) / 2, max((hi - lo).length / 2, 0.5)
    cam = bpy.data.objects.get('S01R_Cam')
    if not cam:
        cam = bpy.data.objects.new('S01R_Cam', bpy.data.cameras.new('S01R_Cam'))
        sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.lens = 50
    out = os.path.join(ROOT, 'Screenshots', 'blender_assets')
    os.makedirs(out, exist_ok=True)
    views = [('a000', 0, 25), ('a090', 90, 25), ('a180', 180, 25), ('a270', 270, 25), ('top', 0, 89)]
    for tag, az, el in views:
        d = rad * 2.6
        a, e = math.radians(az - 60), math.radians(el)
        cam.location = ctr + Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))) * d
        cam.rotation_euler = (ctr - cam.location).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = os.path.join(out, '%s_%s.png' % (ob.name, tag))
        bpy.ops.render.render(write_still=True)


def setup_render():
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading
    sh.light, sh.color_type = 'STUDIO', 'MATERIAL'
    sh.show_shadows, sh.show_cavity, sh.show_object_outline = True, True, True
    sh.cavity_type = 'BOTH'
    sc.display.shadow_shift = 0.1
    sc.render.resolution_x, sc.render.resolution_y = 800, 560
    sc.render.film_transparent = False
    sc.world = sc.world or bpy.data.worlds.new('World')
    sc.world.color = (0.55, 0.72, 0.9)
    sc.unit_settings.system, sc.unit_settings.scale_length = 'METRIC', 1.0


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    setup_render()
    made = []
    for key, fn in BUILDERS:
        if ONLY and key not in ONLY:
            continue
        r = fn()
        made += r if isinstance(r, list) else [r]
        print('[S01R] built', key)
    for ob in made:
        export_fbx(ob)
        tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
        print('[S01R] exported %s tris=%d mats=%s dims_cm=%s' % (ob.name, tris, [m.name for m in ob.data.materials],
              [round(d * 100) for d in ob.dimensions]))
    if RENDER:
        for ob in made:
            if ob.name not in ('SM_S01R_Terrain', 'SM_S01R_Paths', 'SM_S01R_StreamWater', 'SM_S01R_PondWater'):
                render_views(ob)
    name = 'S01R_Assets.blend' if not ONLY else 'S01R_Assets_partial.blend'
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT, 'Blend', name))
    print('[S01R] COMPLETE')


main()
