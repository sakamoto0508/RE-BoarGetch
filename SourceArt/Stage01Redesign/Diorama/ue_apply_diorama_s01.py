"""Import the rebuilt Stage 1 diorama and switch BP_Stage01_Diorama to it (headless commandlet, editor closed):
  sh Tools/Verification/run_ue_py.sh C:/UnrealGames/REBoarGetch/SourceArt/Stage01Redesign/Diorama/ue_apply_diorama_s01.py
FBX/SM_S01_Diorama_{Island,Props}.fbx -> /Game/UI/StagePreview/Meshes (vertex colours, no collision, no Nanite),
material M_Preview_VertexColor (created by SourceArt/Stage02/Diorama/ue_apply_diorama.py), parts at yaw 135 / z -87.
The previous 53 primitive parts are dumped to ../Logs/stage01_diorama_old_parts.json before being replaced.
Results: ../Logs/ue_apply_diorama_s01.json"""
import json, os, traceback
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
LOGS = os.path.join(os.path.dirname(HERE), 'Logs')
BASE = '/Game/UI/StagePreview'
AS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
AT = unreal.AssetToolsHelpers.get_asset_tools()
R = {'saved': []}
YAW, ISLAND_Z = 135.0, -80.0   # front bottom edge on the same capture row as Stage 2 (measured with capture_dioramas.py)


def save(a):
    assert AS.save_loaded_asset(a, False), a.get_path_name()
    R['saved'].append(a.get_path_name())


def run():
    mat = unreal.load_asset(BASE + '/M_Preview_VertexColor')
    assert mat, 'run the Stage 2 ue_apply_diorama.py first'
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
    sd.set_editor_property('generate_lightmap_u_vs', False)
    sd.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    sd.set_editor_property('vertex_color_import_option', unreal.VertexColorImportOption.REPLACE)
    sd.set_editor_property('remove_degenerates', True)
    tasks = []
    for f in ('SM_S01_Diorama_Island', 'SM_S01_Diorama_Props'):
        t = unreal.AssetImportTask()
        t.set_editor_property('filename', os.path.join(HERE, 'FBX', f + '.fbx'))
        t.set_editor_property('destination_path', BASE + '/Meshes')
        t.set_editor_property('destination_name', f)
        t.set_editor_property('replace_existing', True)
        t.set_editor_property('automated', True)
        t.set_editor_property('save', False)
        t.set_editor_property('options', ui)
        tasks.append(t)
    AT.import_asset_tasks(tasks)
    meshes = {}
    for t in tasks:
        for p in t.get_editor_property('imported_object_paths'):
            sm = unreal.load_asset(p)
            if isinstance(sm, unreal.StaticMesh):
                meshes[sm.get_name()] = sm
    assert len(meshes) == 2, list(meshes)
    sml = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary
    for name, sm in meshes.items():
        sm.set_material(0, mat)
        sml.remove_collisions(sm)
        ns = sm.get_editor_property('nanite_settings')
        ns.set_editor_property('enabled', False)
        sm.set_editor_property('nanite_settings', ns)
        R.setdefault('tris', {})[name] = sm.get_num_triangles(0)
        save(sm)

    bp = unreal.load_asset(BASE + '/BP_Stage01_Diorama')
    cdo = unreal.get_default_object(bp.generated_class())
    old = cdo.get_editor_property('parts')
    if not any(p.get_editor_property('mesh') and p.get_editor_property('mesh').get_name().startswith('SM_S01_Diorama') for p in old):
        dump = []
        for p in old:
            t = p.get_editor_property('transform')
            r = t.rotation.rotator()
            dump.append({'mesh': p.get_editor_property('mesh').get_path_name() if p.get_editor_property('mesh') else None,
                         'material': p.get_editor_property('material').get_path_name() if p.get_editor_property('material') else None,
                         'loc': [t.translation.x, t.translation.y, t.translation.z], 'rot': [r.roll, r.pitch, r.yaw],
                         'scale': [t.scale3d.x, t.scale3d.y, t.scale3d.z]})
        with open(os.path.join(LOGS, 'stage01_diorama_old_parts.json'), 'w', encoding='utf-8') as f:
            json.dump(dump, f, indent=1)
        R['old_parts_dumped'] = len(dump)
    parts = []
    for n in ('SM_S01_Diorama_Island', 'SM_S01_Diorama_Props'):
        p = unreal.BoarPreviewPart()
        p.set_editor_property('mesh', meshes[n])
        p.set_editor_property('material', mat)
        p.set_editor_property('transform', unreal.Transform(location=unreal.Vector(0, 0, ISLAND_Z),
                                                            rotation=unreal.Rotator(roll=0, pitch=0, yaw=YAW),
                                                            scale=unreal.Vector(1, 1, 1)))
        parts.append(p)
    cdo.set_editor_property('parts', parts)
    cdo.set_editor_property('ortho_width', 1250.0)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    save(bp)
    R['parts'] = len(parts)


try:
    run()
    R['status'] = 'COMPLETE'
except Exception:
    R['error'] = traceback.format_exc()
    unreal.log_error(R['error'])
os.makedirs(LOGS, exist_ok=True)
with open(os.path.join(LOGS, 'ue_apply_diorama_s01.json'), 'w', encoding='utf-8') as f:
    json.dump(R, f, indent=1)
unreal.log_warning('[S01DioramaApply] ' + R.get('status', 'ERROR'))
