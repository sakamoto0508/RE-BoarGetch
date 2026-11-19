"""Import the Stage 2 generated meshes (terrain, river, pond) into /Game/Stage02/Meshes (headless commandlet).
UnrealEditor-Cmd <uproject> -run=pythonscript -script=<this>
Reuses the existing Stage 1 ground/water material instances (no new master materials). Terrain: complex-as-simple
collision (walkable faceted ground). Water: visual only. Saves only assets under /Game/Stage02 (tracked; /Game/InportAssets is git-ignored)."""
import os, unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FBX = os.path.join(ROOT, 'FBX')
DST = '/Game/Stage02/Meshes'
eal = unreal.EditorAssetLibrary
sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary

UE_MATS = {
    'Grass': '/Game/Stage01/Ground/Materials/MI_Grass_Stage01',
    'GrassLight': '/Game/InportAssets/Stage01Redesign/Materials/MI_S01R_GrassLight',
    'Cliff': '/Game/Stage01/Quality/Materials/MI_S01_Nature_Cliff',
    'Shore': '/Game/Stage01/Ground/Materials/MI_Ground_Stage01_Damp',
    'Path': '/Game/Stage01/Ground/Materials/MI_Ground_Stage01_Dry',
    'Water': '/Game/Stage01/Art/Materials/MI_Stage01_ShallowWater',
}


def log(m):
    unreal.log_warning('[S02Import] ' + m)


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
        if name == 'SM_S02_Terrain':
            body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        else:
            body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        b = sm.get_bounding_box()
        log('imported %s slots=%s bounds=(%s)-(%s) col=%s' % (
            name, [str(s.material_slot_name) for s in sm.get_editor_property('static_materials')],
            [round(v) for v in (b.min.x, b.min.y, b.min.z)], [round(v) for v in (b.max.x, b.max.y, b.max.z)],
            body.get_editor_property('collision_trace_flag')))
        eal.save_loaded_asset(sm)
log('COMPLETE')
