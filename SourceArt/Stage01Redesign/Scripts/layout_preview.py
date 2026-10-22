"""Stage01 Redesign layout preview in Blender (composition check before UE).
blender -b --factory-startup --python layout_preview.py
Appends the built assets from Blend/S01R_Assets.blend, instances them from s01r_dressing.placements(),
uses simple proxies for reused UE meshes (trees, rocks, cage, characters) and renders the key views.
Writes Blend/S01R_LayoutPreview.blend and Screenshots/blender_layout/<view>.png.
"""
import bpy, math, os, sys
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import s01r_dressing as D

ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'Screenshots', 'blender_layout')
os.makedirs(OUT, exist_ok=True)


def U(x, y, z):
    return Vector((x / 100, -y / 100, z / 100))


def mat(name, rgb):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.diffuse_color = rgb + (1,)
    return m


def proxy_mesh(name, kind, rgb, size):
    """Proxy meshes in metres, pivot at base."""
    if name in bpy.data.meshes:
        return bpy.data.meshes[name]
    sx, sy, sz = size
    if kind == 'tree':
        bpy.ops.mesh.primitive_cone_add(vertices=7, radius1=sx, depth=sz * .75, location=(0, 0, sz * .25 + sz * .375))
        crown = bpy.context.object
        bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=sx * .12, depth=sz * .3, location=(0, 0, sz * .15))
        trunk = bpy.context.object
        crown.data.materials.append(mat('PX_Leaf_' + name, rgb))
        trunk.data.materials.append(mat('PX_Trunk', (0.35, 0.22, 0.1)))
        bpy.ops.object.select_all(action='DESELECT')
        crown.select_set(True); trunk.select_set(True)
        bpy.context.view_layer.objects.active = crown
        bpy.ops.object.join()
    elif kind == 'blob':
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=1, location=(0, 0, 0))
        bpy.context.object.scale = (sx, sy, sz)
        bpy.ops.object.transform_apply(scale=True)
        bpy.context.object.data.materials.append(mat('PX_' + name, rgb))
    else:
        bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, sz / 2))
        bpy.context.object.scale = (sx, sy, sz)
        bpy.ops.object.transform_apply(scale=True)
        bpy.context.object.data.materials.append(mat('PX_' + name, rgb))
    ob = bpy.context.object
    me = ob.data
    me.name = name
    bpy.data.objects.remove(ob)
    return me


def proxy_for(path):
    n = path.split('/')[-1]
    if 'BGMountain' in n:
        return proxy_mesh(n, 'tree', (0.45, 0.5, 0.42), (30, 30, 30))
    if 'Pine' in n:
        return proxy_mesh(n, 'tree', (0.15, 0.4, 0.2), (1.4, 1.4, 7))
    if 'Tree' in n and 'Log' not in n:
        return proxy_mesh(n, 'tree', (0.25, 0.55, 0.15), (2.0, 2.0, 5.5))
    if 'Bush' in n:
        return proxy_mesh('PX_Bush', 'blob', (0.2, 0.5, 0.15), (0.9, 0.7, 0.5))
    if 'Cliff' in n:
        return proxy_mesh('PX_Cliff', 'blob', (0.42, 0.44, 0.38), (2.5, 2.0, 3.0))
    if 'Rock' in n or 'Log' in n:
        return proxy_mesh('PX_Rock', 'blob', (0.5, 0.52, 0.48), (0.9, 0.7, 0.5))
    if 'Grass' in n or 'Mushroom' in n:
        return proxy_mesh('PX_Grass', 'blob', (0.3, 0.6, 0.15), (0.2, 0.2, 0.2))
    if 'Flower' in n:
        return proxy_mesh('PX_Flower', 'blob', (0.95, 0.85, 0.3), (0.18, 0.18, 0.2))
    if 'Fence' in n:
        return proxy_mesh('PX_Fence', 'box', (0.5, 0.35, 0.18), (1.8, 0.1, 0.8))
    return proxy_mesh('PX_Prop', 'box', (0.55, 0.38, 0.2), (0.7, 0.7, 0.8))


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    lib = os.path.join(ROOT, 'Blend', 'S01R_Assets.blend')
    with bpy.data.libraries.load(lib, link=False) as (src, dst):
        dst.meshes = [m for m in src.meshes if m.startswith('SM_S01R_')]
    meshes = {m.name: m for m in bpy.data.meshes if m.name.startswith('SM_S01R_')}
    col = bpy.data.collections.new('Layout')
    bpy.context.scene.collection.children.link(col)
    for i, p in enumerate(D.placements()):
        name = p['mesh'].split('/')[-1]
        me = meshes.get(name) or proxy_for(p['mesh'])
        ob = bpy.data.objects.new('%s_%03d' % (name, i), me)
        ob.location = U(p['x'], p['y'], p['z'])
        ob.rotation_euler = (0, 0, -math.radians(p['yaw']))
        ob.scale = p['scale']
        col.objects.link(ob)
    # gameplay proxies: cage, player, boars
    cage = proxy_mesh('PX_Cage', 'box', (0.55, 0.6, 0.62), (2.6, 2.6, 2.8))
    ob = bpy.data.objects.new('PX_BP_Cage', cage); ob.location = U(D.CAGE[0], D.CAGE[1], 5); col.objects.link(ob)
    pl = proxy_mesh('PX_Player', 'box', (0.95, 0.4, 0.2), (0.45, 0.45, 1.76))
    ob = bpy.data.objects.new('PX_Player', pl); ob.location = U(D.START[0], D.START[1], 0); col.objects.link(ob)
    boar = proxy_mesh('PX_Boar', 'box', (0.45, 0.28, 0.18), (1.1, 0.6, 0.9))
    for k, (x, y, yaw) in D.SPAWN_POINTS.items():
        if 'Coin' in k:
            m = proxy_mesh('PX_Coin', 'blob', (1.0, 0.8, 0.1), (0.3, 0.3, 0.3))
            ob = bpy.data.objects.new('PX_' + k, m); ob.location = U(x, y, D.height(x, y) + D.TOWER_DECK_RISE + 60)
        else:
            ob = bpy.data.objects.new('PX_' + k, boar); ob.location = U(x, y, D.height(x, y))
            ob.rotation_euler = (0, 0, -math.radians(yaw))
        col.objects.link(ob)

    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading
    sh.light, sh.color_type = 'STUDIO', 'MATERIAL'
    sh.show_shadows, sh.show_cavity = True, True
    sc.display.light_direction = (0.45, -0.35, 0.82)
    sc.display.shadow_shift = 0.05
    sc.world = bpy.data.worlds.new('Sky')
    sc.world.color = (0.55, 0.75, 0.95)
    sc.render.resolution_x, sc.render.resolution_y = 1600, 900
    cam = bpy.data.objects.new('Cam', bpy.data.cameras.new('Cam'))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.clip_end = 600
    # name: (UE camera position, UE look-at point, lens mm)
    views = {
        'overview_top': ((120, -1300, 9000), (120, -1300, 0), 22),
        'overview_oblique': ((120, 4300, 4300), (120, -1700, 0), 24),
        'start_view': ((120, 1300, 260), (120, -1510, 120), 24),
        'from_cage': ((120, -1050, 420), (-2300, -1500, 0), 22),
        'water_bridge': ((-1500, -1200, 380), (-2700, -2500, 60), 22),
        'hill_tower': ((1300, -400, 450), (3100, -1700, 250), 24),
        'from_tower': ((3080, -1650, 560), (100, -1500, 0), 22),
        'windmill': ((-1300, -1900, 330), (-3180, -2720, 450), 26),
    }
    for name, (pos, tgt, lens) in views.items():
        cam.location = U(*pos)
        cam.rotation_euler = (U(*tgt) - U(*pos)).to_track_quat('-Z', 'Y').to_euler()
        cam.data.lens = lens
        sc.render.filepath = os.path.join(OUT, name + '.png')
        bpy.ops.render.render(write_still=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT, 'Blend', 'S01R_LayoutPreview.blend'))
    print('[S01R] layout COMPLETE')


main()
