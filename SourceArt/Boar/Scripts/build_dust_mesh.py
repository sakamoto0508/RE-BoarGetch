"""土煙（NS_BoarBrakeDust）用のローポリ粒子Mesh SM_BoarDustPuff（AKI-172）。半径12cmの低分割Icosphere（フラットシェーディング）。
Run: blender -b --python build_dust_mesh.py"""
import bpy
ROOT = 'C:/UnrealGames/REBoarGetch/SourceArt/Boar/'
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.12, location=(0, 0, 0))
o = bpy.context.active_object
o.name = 'SM_BoarDustPuff'
# 少し潰した塊にして、土の塊らしい不揃いさを出す
o.scale = (1.0, 0.92, 0.82)
bpy.ops.object.transform_apply(scale=True)
for p in o.data.polygons:
    p.use_smooth = False
bpy.ops.export_scene.fbx(filepath=ROOT + 'FBX/SM_BoarDustPuff.fbx', use_selection=True, object_types={'MESH'},
                         apply_unit_scale=True, global_scale=1.0, mesh_smooth_type='FACE', add_leaf_bones=False)
print('DUST_MESH_DONE', len(o.data.polygons))
