# SM_Cage を Blender で組み立てて FBX に書き出します。
# 実行: blender -b --python SourceArt/Cage/Scripts/build_sm_cage.py
# 寸法・配置は旧 UCageVisualComponent の箱組み（UE cm, +X が正面）と同じです。
# Material Slot 名は UE 側の M_Cage_White / Navy / Yellow / Glow に対応し、Glow が HP ランプです。
import bpy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BLEND = ROOT / 'Blend' / 'SM_Cage.blend'
FBX = ROOT / 'FBX' / 'SM_Cage.fbx'

# (位置cm, サイズcm, Slot)
BOXES = [((0, 0, 14), (550, 550, 24), 'Navy')]
for x in (-1, 1):
    for y in (-1, 1):
        BOXES.append(((x * 238, y * 238, 273), (48, 48, 500), 'White'))
        for z in (40, 521):
            BOXES.append(((x * 238, y * 238, z), (66, 66, 42), 'Yellow'))
        BOXES.append(((x * 264, y * 238, 280), (7, 20, 200), 'Glow'))
for s in (-1, 1):
    for z in (46, 514):
        BOXES.append(((s * 239, 0, z), (43, 475, 35), 'White'))
        BOXES.append(((0, s * 239, z), (475, 43, 35), 'White'))
BOXES.append(((271, 0, 466), (16, 180, 56), 'Navy'))
BOXES.append(((282, 0, 466), (7, 145, 26), 'Glow'))

PREVIEW = {'White': (.88, .93, .96), 'Navy': (.015, .035, .065), 'Yellow': (1, .65, .02), 'Glow': (.01, .65, 1)}

bpy.ops.wm.read_factory_settings(use_empty=True)
mats = {}
for name, rgb in PREVIEW.items():
    m = bpy.data.materials.new('M_Cage_' + name)
    m.diffuse_color = (*rgb, 1)
    mats[name] = m
parts = []
for (px, py, pz), (sx, sy, sz), slot in BOXES:
    bpy.ops.mesh.primitive_cube_add(size=1)
    o = bpy.context.object
    # UE (cm, 左手系) → Blender (m, 右手系): Y を反転
    o.location = (px / 100, -py / 100, pz / 100)
    o.scale = (sx / 100, sy / 100, sz / 100)
    o.data.materials.append(mats[slot])
    parts.append(o)
bpy.ops.object.select_all(action='DESELECT')
for o in parts:
    o.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
bpy.ops.object.join()
cage = bpy.context.object
cage.name = cage.data.name = 'SM_Cage'
# 原点はワールド原点（=旧 CagePresentation の位置）のまま
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for p in cage.data.polygons:
    p.use_smooth = False

BLEND.parent.mkdir(parents=True, exist_ok=True)
FBX.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(BLEND))
bpy.ops.export_scene.fbx(filepath=str(FBX), use_selection=True, object_types={'MESH'},
                         apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', mesh_smooth_type='FACE',
                         use_mesh_modifiers=True, add_leaf_bones=False, bake_anim=False)
print('SM_CAGE_EXPORTED', FBX, len(BOXES), 'boxes')
