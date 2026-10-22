"""Import the Stage01 Redesign FBX set into /Game/InportAssets/Stage01Redesign (headless commandlet).
Assigns existing material instances by slot name (no new master materials), creates two small MIs for
light grass / light wood, sets collision per mesh, saves only assets under /Game/InportAssets/Stage01Redesign."""
import os, unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FBX = os.path.join(ROOT, 'FBX')
DST = '/Game/InportAssets/Stage01Redesign/Meshes'
MATDIR = '/Game/InportAssets/Stage01Redesign/Materials'
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary


def log(m):
    unreal.log('[S01] ' + m)


def make_mi(name, parent, vectors=None, scalars=None):
    path = MATDIR + '/' + name
    mi = unreal.load_asset(path) if eal.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MATDIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', unreal.load_asset(parent))
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v, 1.0))
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    mel.update_material_instance(mi)
    eal.save_loaded_asset(mi)
    return path


# grass master: report its parameters so the light variant uses a real one
gm = unreal.load_asset('/Game/Stage01/Ground/Materials/M_Grass_LowPoly_World')
log('grass params scalar=%s vector=%s' % ([str(n) for n in mel.get_scalar_parameter_names(gm)],
                                          [str(n) for n in mel.get_vector_parameter_names(gm)]))
gnames = [str(n) for n in mel.get_scalar_parameter_names(gm)]
FLAT = '/Game/InportAssets/LevelPrototyping/Materials/M_FlatCol'
UE_MATS = {
    'Grass': '/Game/Stage01/Ground/Materials/MI_Grass_Stage01',
    'GrassLight': make_mi('MI_S01R_GrassLight', '/Game/Stage01/Ground/Materials/M_Grass_LowPoly_World',
                          scalars={}),
    'Cliff': '/Game/Stage01/Quality/Materials/MI_S01_Nature_Cliff',
    'Shore': '/Game/Stage01/Ground/Materials/MI_Ground_Stage01_Damp',
    'Path': '/Game/Stage01/Ground/Materials/MI_Ground_Stage01_Dry',
    'Ivory': '/Game/Stage01/Quality/Materials/MI_S01_Quality_Ivory',
    'Blue': '/Game/Stage01/Quality/Materials/MI_S01_Quality_Blue',
    'Cyan': '/Game/Stage01/Quality/Materials/MI_S01_Quality_Cyan',
    'Yellow': '/Game/Stage01/Quality/Materials/MI_S01_Quality_Yellow',
    'Glass': '/Game/Stage01/Quality/Materials/MI_S01_Quality_Glass',
    'Machine': '/Game/Stage01/Quality/Materials/MI_S01_Quality_Machine',
    # MI_TreeWood/MI_LowPoly rely on the LP pack's vertex colours (our FBX has none -> white), so wood uses flat colours
    'Wood': make_mi('MI_S01R_Wood', FLAT, vectors={'Base Color': (0.50, 0.31, 0.13)}, scalars={'Roughness': 0.85}),
    'WoodLight': make_mi('MI_S01R_WoodLight', FLAT, vectors={'Base Color': (0.62, 0.40, 0.17)}, scalars={'Roughness': 0.85}),
    'WoodDark': make_mi('MI_S01R_WoodDark', FLAT, vectors={'Base Color': (0.26, 0.14, 0.06)}, scalars={'Roughness': 0.9}),
    'Water': '/Game/Stage01/Art/Materials/MI_Stage01_ShallowWater',
    'Rock': '/Game/Stage01/Quality/Materials/MI_S01_Nature_Rock',
}
COMPLEX = {'SM_S01R_Terrain', 'SM_S01R_CagePlaza', 'SM_S01R_WoodBridge', 'SM_S01R_Watchtower', 'SM_S01R_WindmillBody',
           'SM_S01R_FacilityMain', 'SM_S01R_FacilityDome', 'SM_S01R_FacilityAntenna', 'SM_S01R_ResearchFence', 'SM_S01R_ResearchGate'}
BOXES = {'SM_S01R_GuideBeacon', 'SM_S01R_Bollard', 'SM_S01R_ResearchCrate', 'SM_S01R_Signpost', 'SM_S01R_ResearchFencePost'}

ui = unreal.FbxImportUI()
ui.set_editor_property('import_mesh', True)
ui.set_editor_property('import_as_skeletal', False)
ui.set_editor_property('import_materials', False)
ui.set_editor_property('import_textures', False)
ui.set_editor_property('import_animations', False)
ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
sd = ui.static_mesh_import_data
sd.set_editor_property('combine_meshes', True)
sd.set_editor_property('auto_generate_collision', False)
sd.set_editor_property('generate_lightmap_u_vs', True)
sd.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
sd.set_editor_property('remove_degenerates', True)
tasks = []
for f in sorted(os.listdir(FBX)):
    if not f.endswith('.fbx'):
        continue
    t = unreal.AssetImportTask()
    t.set_editor_property('filename', os.path.join(FBX, f))
    t.set_editor_property('destination_path', DST)
    t.set_editor_property('replace_existing', True)
    t.set_editor_property('automated', True)
    t.set_editor_property('save', False)
    t.set_editor_property('options', ui)
    tasks.append(t)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

for t in tasks:
    for p in t.get_editor_property('imported_object_paths'):
        sm = unreal.load_asset(p)
        if not isinstance(sm, unreal.StaticMesh):
            continue
        name = sm.get_name()
        for i, slot in enumerate(sm.get_editor_property('static_materials')):
            key = str(slot.material_slot_name)
            mat = unreal.load_asset(UE_MATS[key]) if key in UE_MATS else None
            if mat is None:
                log('WARN no material for %s.%s' % (name, key))
                continue
            sm.set_material(i, mat)
        body = sm.get_editor_property('body_setup')
        sms.remove_collisions(sm)
        if name in COMPLEX:
            body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        elif name in BOXES and sms is unreal.EditorStaticMeshLibrary:
            # headless: add_simple_collisions is a silent no-op here; the low-poly shape still blocks.
            # Run ue_fix_collision.py in the full editor afterwards for proper boxes.
            body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        elif name in BOXES:
            body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
            sms.add_simple_collisions(sm, unreal.ScriptCollisionShapeType.BOX)
        else:  # water, paths, rotor: visual only
            body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        b = sm.get_bounding_box()
        log('imported %s slots=%d tris=%d bounds=(%s)-(%s) col=%s' % (
            name, len(sm.get_editor_property('static_materials')), sms.get_number_verts(sm, 0),
            [round(v) for v in (b.min.x, b.min.y, b.min.z)], [round(v) for v in (b.max.x, b.max.y, b.max.z)],
            body.get_editor_property('collision_trace_flag')))
        eal.save_loaded_asset(sm)
log('COMPLETE')
