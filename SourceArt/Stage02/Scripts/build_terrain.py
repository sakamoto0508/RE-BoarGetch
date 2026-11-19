"""Stage 2 terrain + water builder (Blender 5.x, background).
blender -b --factory-startup --python build_terrain.py -- [--no-render]

Geometry is authored in UE space (cm, UE axes) and converted once in MB.v():
UE (x, y, z) cm -> Blender (x/100, -y/100, z/100) m. Default FBX export brings it back to UE unchanged.
Material slot names map to existing UE material instances in ue_import_assets.py (UE_MATS).
Writes Blend/S02_Terrain.blend, FBX/SM_S02_*.fbx, Screenshots/blender/*.png (+ Logs/terrain_stats.json).
"""
import bpy, bmesh, json, math, os, sys
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import s02_layout as L

ROOT = os.path.dirname(HERE)
argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
RENDER = '--no-render' not in argv

COLORS = {
    'Grass': (0.20, 0.45, 0.07), 'GrassLight': (0.36, 0.60, 0.14), 'Cliff': (0.46, 0.43, 0.36), 'Shore': (0.36, 0.27, 0.15),
    'Path': (0.66, 0.47, 0.24), 'Water': (0.15, 0.60, 0.80),
}


class MB:
    def __init__(self, name):
        self.name, self.bm, self.mats = name, bmesh.new(), []

    @staticmethod
    def v(p):
        return Vector((p[0] / 100.0, -p[1] / 100.0, p[2] / 100.0))

    def face(self, pts, mat, out=(0, 0, 1)):
        if mat not in self.mats:
            self.mats.append(mat)
        f = self.bm.faces.new([self.bm.verts.new(self.v(p)) for p in pts])
        f.material_index = self.mats.index(mat)
        f.normal_update()
        if f.normal.dot(Vector((out[0], -out[1], out[2]))) < 0:
            f.normal_flip()
        return f

    def finish(self, weld=True):
        bm = self.bm
        if weld:
            bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0005)
        uv = bm.loops.layers.uv.new('UVMap')
        for f in bm.faces:
            n = f.normal
            ax = max(range(3), key=lambda i: abs(n[i]))
            for loop in f.loops:
                co = loop.vert.co
                loop[uv].uv = [(co.y, co.z), (co.x, co.z), (co.x, co.y)][ax]
            f.smooth = False
        me = bpy.data.meshes.new(self.name)
        bm.to_mesh(me)
        bm.free()
        for m in self.mats:
            me.materials.append(material(m))
        ob = bpy.data.objects.new(self.name, me)
        bpy.context.scene.collection.objects.link(ob)
        return ob


def material(name):
    m = bpy.data.materials.get(name)
    if not m:
        m = bpy.data.materials.new(name)
        m.diffuse_color = COLORS.get(name, (1, 0, 1)) + (1.0,)
    return m


STATS = {'faces': {}, 'steep_on_path': 0, 'max_path_slope_deg': 0.0}


def build_terrain():
    mb = MB('SM_S02_Terrain')
    X0, X1, Y0, Y1 = L.TERRAIN
    S = L.GRID
    nx, ny = int((X1 - X0) // S), int((Y1 - Y0) // S)
    g = {}
    for j in range(ny + 1):
        for i in range(nx + 1):
            x, y = X0 + i * S, Y0 + j * S
            if 0 < i < nx and 0 < j < ny:
                x += (L._hash(i, j, 91) - .5) * 50
                y += (L._hash(i, j, 92) - .5) * 50
            g[i, j] = (x, y, L.height(x, y))
    for j in range(ny):
        for i in range(nx):
            a, b, c, d = g[i, j], g[i + 1, j], g[i + 1, j + 1], g[i, j + 1]
            tris = [(a, b, c), (a, c, d)] if L._hash(i, j, 5) > .5 else [(a, b, d), (b, c, d)]
            for t in tris:
                n = (Vector(t[1]) - Vector(t[0])).cross(Vector(t[2]) - Vector(t[0])).normalized()
                cx, cy = sum(p[0] for p in t) / 3, sum(p[1] for p in t) / 3
                slope = math.degrees(math.acos(min(1.0, abs(n.z))))
                mat = 'Cliff' if slope > 50 else ('Grass' if slope > 30 else L.surface(cx, cy))
                if L.path_dist((cx, cy)) < 0 and L.outside_dist(cx, cy) == 0:
                    STATS['max_path_slope_deg'] = max(STATS['max_path_slope_deg'], slope)
                    if slope > 25:
                        STATS['steep_on_path'] += 1
                        STATS.setdefault('steep_path_at', []).append((round(cx), round(cy), round(slope)))
                STATS['faces'][mat] = STATS['faces'].get(mat, 0) + 1
                mb.face(list(t), mat)
    return mb.finish()


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
    out.append(pts[-1])
    return out


def build_water():
    mb = MB('SM_S02_RiverWater')
    pts = resample(L.RIVER_S, 120)
    n = len(pts)
    rows = []
    # surface height: banks minus a lip, monotonic downstream, never above the pond level inside the pond
    zs = []
    cap = 1e9
    for p in pts:
        z = min(L.water_z(p[0], p[1]), cap)
        if L.ellipse_r(p, L.POND) < 1.3:
            z = min(z, L.POND_WATER_Z - 1.5)   # just under the pond sheet where they overlap (no z-fighting)
        cap = z
        zs.append(z)
    for i, p in enumerate(pts):
        if L.ellipse_r(p, L.POND) < 0.9:        # inside the pond: the pond mesh covers it
            break
        a, b = Vector(pts[max(i - 1, 0)]), Vector(pts[min(i + 1, n - 1)])
        t = (b - a).normalized()
        nrm = Vector((-t.y, t.x))
        z = zs[i]
        row = []
        for s in (-1, 1):
            # walk out from the channel until the bank is above the water, then bury the edge 15 cm into it
            d = L.RIVER_W * 0.25
            while d < L.RIVER_W * 1.2 and L.height(p[0] + nrm.x * d * s, p[1] + nrm.y * d * s) < z + 3:
                d += 8
            d = max(d + 15, L.RIVER_W * 0.36)
            STATS.setdefault('river_edge_w', []).append(round(d))
            row.append((p[0] + nrm.x * d * s, p[1] + nrm.y * d * s, z))
        rows.append([row[0], (p[0], p[1], z), row[1]])
    n = len(rows)
    for i in range(n - 1):
        for k in range(2):
            mb.face([rows[i][k], rows[i][k + 1], rows[i + 1][k + 1], rows[i + 1][k]], 'Water')
    river = mb.finish()
    # check: every edge vertex must sit on or under the bank (no air under the water sheet)
    gaps = [round(q[2] - L.height(q[0], q[1]), 1) for r in rows for q in (r[0], r[2])]
    STATS['river_edge_max_gap'] = max(gaps)
    mb = MB('SM_S02_PondWater')
    cx, cy, rx, ry, rot = L.POND
    z = L.POND_WATER_Z
    ring = []
    for k in range(18):
        a = 2 * math.pi * k / 18
        r = 1.04 + (L._hash(k, 1, 4) - .5) * 0.04
        u, v = math.cos(a) * rx * r, math.sin(a) * ry * r
        ring.append((cx + u * math.cos(rot) - v * math.sin(rot), cy + u * math.sin(rot) + v * math.cos(rot), z))
    for k in range(18):
        mb.face([(cx, cy, z), ring[k], ring[(k + 1) % 18]], 'Water')
    STATS['pond_edge_max_gap'] = max(round(z - L.height(q[0], q[1]), 1) for q in ring)
    return [river, mb.finish()]


def export_fbx(ob):
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=os.path.join(ROOT, 'FBX', ob.name + '.fbx'), use_selection=True,
                             object_types={'MESH'}, mesh_smooth_type='FACE', use_mesh_modifiers=True,
                             add_leaf_bones=False, bake_anim=False, path_mode='STRIP')


# ---------------------------------------------------------------- preview (proxies only, not exported)
def proxy_box(name, ue_xy, size, z, color, yaw=0):
    bpy.ops.mesh.primitive_cube_add(size=1)
    o = bpy.context.object
    o.name = name
    o.scale = (size[0] / 100, size[1] / 100, size[2] / 100)
    o.location = MB.v((ue_xy[0], ue_xy[1], z + size[2] / 2))
    o.rotation_euler = (0, 0, -math.radians(yaw))
    m = material('P_' + name); m.diffuse_color = color + (1,)
    o.data.materials.append(m)
    o['proxy'] = 1


def proxies():
    for k, (x, y) in L.LANDMARKS.items():
        size = {'Barn': (500, 400, 330), 'FarmHouse': (860, 810, 590), 'Windmill': (480, 480, 930), 'Watchtower': (400, 400, 540)}[k]
        proxy_box(k, (x, y), size, L.height(x, y), (0.75, 0.25, 0.15) if k in ('Barn', 'FarmHouse') else (0.85, 0.85, 0.9))
    proxy_box('Cage', L.CAGE, (300, 300, 350), L.height(*L.CAGE), (0.9, 0.1, 0.1))
    proxy_box('Start', L.START, (80, 80, 180), L.height(*L.START), (0.05, 0.05, 0.05))
    for k, y in L.SHORTCUTS.items():
        x = L.ridge_x(y) + 60
        proxy_box('SC_' + k, (x, y), (80, 80, 250), L.height(x, y), (1.0, 0.6, 0.0))


def render_previews():
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading
    sh.light, sh.color_type = 'STUDIO', 'MATERIAL'
    sh.show_shadows, sh.show_cavity = True, True
    sc.display.shadow_shift = 0.1
    sc.world = sc.world or bpy.data.worlds.new('World')
    sc.world.color = (0.55, 0.72, 0.9)
    sc.view_settings.view_transform = 'Standard'
    out = os.path.join(ROOT, 'Screenshots', 'blender')
    os.makedirs(out, exist_ok=True)
    cam = bpy.data.objects.new('Cam', bpy.data.cameras.new('Cam'))
    sc.collection.objects.link(cam)
    sc.camera = cam
    views = {
        # name: (ue eye, ue target, lens, ortho)
        'top': ((0, 0, 9000), (0, 0, 0), None, 105),
        'oblique_from_south': ((-8000, -900, 4200), (600, 300, 0), 28, None),
        'start_eye': ((L.START[0] - 350, L.START[1] - 60, L.height(*L.START) + 330), (L.CAGE[0] + 1500, L.CAGE[1] + 150, 120), 24, None),
        'cage_eye': ((L.CAGE[0] - 500, L.CAGE[1] - 300, 320), (2900, 400, 250), 24, None),
        'ridge_eye': ((3300, 600, L.RIDGE_H + 330), (-600, 200, 0), 24, None),
    }
    for name, (eye, tgt, lens, ortho) in views.items():
        cam.location = MB.v(eye)
        d = MB.v(tgt) - cam.location
        cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
        if ortho:
            cam.data.type = 'ORTHO'; cam.data.ortho_scale = ortho
            cam.rotation_euler = (0, 0, math.radians(-90))   # UE north (+X) up
            sc.render.resolution_x, sc.render.resolution_y = 1400, 1180
        else:
            cam.data.type = 'PERSP'; cam.data.lens = lens
            sc.render.resolution_x, sc.render.resolution_y = 1280, 720
        cam.data.clip_end = 1000
        sc.render.filepath = os.path.join(out, name + '.png')
        bpy.ops.render.render(write_still=True)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    made = [build_terrain()] + build_water()
    for ob in made:
        export_fbx(ob)
        print('[S02] exported %s tris=%d mats=%s dims_cm=%s' % (ob.name, sum(len(p.vertices) - 2 for p in ob.data.polygons),
              [m.name for m in ob.data.materials], [round(d * 100) for d in ob.dimensions]))
    STATS['start_h'] = L.height(*L.START); STATS['cage_h'] = L.height(*L.CAGE)
    STATS['shortcut_drop'] = {k: [round(L.height(L.ridge_x(y) + 120, y)), round(L.height(L.ridge_x(y) - 400, y))] for k, y in L.SHORTCUTS.items()}
    json.dump(STATS, open(os.path.join(ROOT, 'Logs', 'terrain_stats.json'), 'w'), indent=1)
    print('[S02] stats', STATS)
    if RENDER:
        proxies()
        render_previews()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT, 'Blend', 'S02_Terrain.blend'))
    print('[S02] COMPLETE')


main()
