"""Import the Stage 2 diorama and register Stage 2 in the Stage Select (headless commandlet, editor closed):
  sh Tools/Verification/run_ue_py.sh C:/UnrealGames/REBoarGetch/SourceArt/Stage02/Diorama/ue_apply_diorama.py

1. FBX/SM_*.fbx -> /Game/UI/StagePreview/Meshes (vertex colours replaced from the FBX, no collision, no Nanite)
2. /Game/UI/StagePreview/M_Preview_VertexColor: unlit, vertex colour x the same fake key light as M_Preview_* (Stage 1)
3. /Game/UI/StagePreview/BP_Stage02_Diorama (ABoarStagePreviewActor): island + props (yaw 45, north away from the
   capture camera), lowered so the front bottom edge sits where Stage 1's does on the UI's shared holographic pedestal;
   OrthoWidth / camera unchanged from the C++ defaults (= Stage 1)
4. BP_Stage01_Diorama: left as it was (an earlier run's 3D pedestal part and asset are removed again)
5. DA_Stage02StageConfig: DisplayName 段丘牧場, PreviewActorClass = BP_Stage02_Diorama (unlock condition untouched)
6. BP_StageEntrance_Stage01.StageCatalog: [Stage01, Stage02] (the GameInstance catalog is empty, so this is the one in use)
Results: SourceArt/Stage02/Logs/ue_apply_diorama.json"""
import json, os, traceback
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), 'Logs', 'ue_apply_diorama.json')
BASE = '/Game/UI/StagePreview'
MESHES = BASE + '/Meshes'
ML = unreal.MaterialEditingLibrary
AS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
AT = unreal.AssetToolsHelpers.get_asset_tools()
R = {'saved': [], 'warnings': []}
ISLAND_Z = -87.0        # = build_diorama.ISLAND_Z
OLD_PEDESTAL = MESHES + '/SM_StageDiorama_Pedestal'


def log(m):
    unreal.log_warning('[S02DioramaApply] ' + m)


def save(a):
    assert AS.save_loaded_asset(a, False), a.get_path_name()
    R['saved'].append(a.get_path_name())


def import_meshes():
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
    for f in ('SM_S02_Diorama_Island', 'SM_S02_Diorama_Props'):
        t = unreal.AssetImportTask()
        t.set_editor_property('filename', os.path.join(HERE, 'FBX', f + '.fbx'))
        t.set_editor_property('destination_path', MESHES)
        t.set_editor_property('destination_name', f)
        t.set_editor_property('replace_existing', True)
        t.set_editor_property('automated', True)
        t.set_editor_property('save', False)
        t.set_editor_property('options', ui)
        tasks.append(t)
    AT.import_asset_tasks(tasks)
    out = {}
    for t in tasks:
        for p in t.get_editor_property('imported_object_paths'):
            sm = unreal.load_asset(p)
            if isinstance(sm, unreal.StaticMesh):
                out[sm.get_name()] = sm
    assert len(out) == 2, list(out)
    return out


def make_material():
    path = BASE + '/M_Preview_VertexColor'
    if AS.does_asset_exist(path):
        AS.delete_asset(path)
    m = AT.create_asset('M_Preview_VertexColor', BASE, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    # identical to the Stage 1 M_Preview_* graph, with the colour constant replaced by the mesh vertex colour
    normal = ML.create_material_expression(m, unreal.MaterialExpressionPixelNormalWS, -900, 0)
    key = ML.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -900, 150)
    key.set_editor_property('constant', unreal.LinearColor(-.35, -.45, .82, 1))
    dot = ML.create_material_expression(m, unreal.MaterialExpressionDotProduct, -650, 50)
    ML.connect_material_expressions(normal, '', dot, 'A')
    ML.connect_material_expressions(key, '', dot, 'B')
    mul = ML.create_material_expression(m, unreal.MaterialExpressionMultiply, -450, 50)
    mul.set_editor_property('const_b', .3)
    ML.connect_material_expressions(dot, '', mul, 'A')
    add = ML.create_material_expression(m, unreal.MaterialExpressionAdd, -300, 50)
    add.set_editor_property('const_b', .7)
    ML.connect_material_expressions(mul, '', add, 'A')
    vc = ML.create_material_expression(m, unreal.MaterialExpressionVertexColor, -450, -150)
    shade = ML.create_material_expression(m, unreal.MaterialExpressionMultiply, -150, -50)
    ML.connect_material_expressions(vc, '', shade, 'A')
    ML.connect_material_expressions(add, '', shade, 'B')
    ML.connect_material_property(shade, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    ML.layout_material_expressions(m)
    ML.recompile_material(m)
    save(m)
    return m


def part(mesh, mat, loc, yaw=0.0, scale=1.0):
    p = unreal.BoarPreviewPart()
    p.set_editor_property('mesh', mesh)
    p.set_editor_property('material', mat)
    p.set_editor_property('transform', unreal.Transform(location=unreal.Vector(*loc), rotation=unreal.Rotator(roll=0, pitch=0, yaw=yaw),
                                                        scale=unreal.Vector(scale, scale, scale)))
    return p


def run():
    meshes = import_meshes()
    mat = make_material()
    for name, sm in meshes.items():
        sm.set_material(0, mat)
        body = sm.get_editor_property('body_setup')
        if body:
            (unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary).remove_collisions(sm)
            body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        ns = sm.get_editor_property('nanite_settings')
        ns.set_editor_property('enabled', False)
        sm.set_editor_property('nanite_settings', ns)
        b = sm.get_bounding_box()
        R.setdefault('meshes', {})[name] = {'tris': sm.get_num_triangles(0),
                                            'min': [round(b.min.x), round(b.min.y), round(b.min.z)],
                                            'max': [round(b.max.x), round(b.max.y), round(b.max.z)]}
        save(sm)

    # Stage 2 diorama BP
    bp_path = BASE + '/BP_Stage02_Diorama'
    bp = unreal.load_asset(bp_path)
    if not bp:
        f = unreal.BlueprintFactory()
        f.set_editor_property('parent_class', unreal.BoarStagePreviewActor)
        bp = AT.create_asset('BP_Stage02_Diorama', BASE, unreal.Blueprint, f)
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property('parts', [
        part(meshes['SM_S02_Diorama_Island'], mat, (0, 0, ISLAND_Z), yaw=45),
        part(meshes['SM_S02_Diorama_Props'], mat, (0, 0, ISLAND_Z), yaw=45),
    ])
    cdo.set_editor_property('ortho_width', 1250.0)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    save(bp)

    # Stage 1: only undo an earlier run's pedestal part (the 53 original parts stay untouched)
    s1 = unreal.load_asset(BASE + '/BP_Stage01_Diorama')
    s1cdo = unreal.get_default_object(s1.generated_class())
    parts = list(s1cdo.get_editor_property('parts'))
    R['stage1_parts_before'] = len(parts)
    kept = [p for p in parts if not (p.get_editor_property('mesh') and p.get_editor_property('mesh').get_name() == 'SM_StageDiorama_Pedestal')]
    if len(kept) != len(parts):
        s1cdo.set_editor_property('parts', kept)
        unreal.BlueprintEditorLibrary.compile_blueprint(s1)
        save(s1)
    parts = kept
    if AS.does_asset_exist(OLD_PEDESTAL):
        R['deleted_pedestal'] = AS.delete_asset(OLD_PEDESTAL)
    R['stage1_parts_after'] = len(parts)

    # StageConfig
    cfg = unreal.load_asset('/Game/DataAssets/StageConfig/DA_Stage02StageConfig')
    cfg.set_editor_property('display_name', '段丘牧場')
    cfg.set_editor_property('preview_actor_class', bp.generated_class())
    save(cfg)
    R['stage02'] = {k: str(cfg.get_editor_property(k)) for k in ('stage_id', 'display_name', 'description', 'level', 'preview_actor_class', 'target_capture_count')}
    R['stage02']['unlock'] = str(cfg.get_editor_property('unlock_condition').get_editor_property('required_cleared_stage_id'))

    # Stage Select catalog (the entrance BP; L_Lobby's instance has no override)
    ent = unreal.load_asset('/Game/BP/Lobby/BP_StageEntrance_Stage01')
    ecdo = unreal.get_default_object(ent.generated_class())
    s1cfg = unreal.load_asset('/Game/DataAssets/StageConfig/DA_TestStageConfig')
    cat = [c for c in ecdo.get_editor_property('stage_catalog') if c]
    if cfg not in cat:
        cat.append(cfg)
    assert cat[0] == s1cfg, cat
    ecdo.set_editor_property('stage_catalog', cat)
    unreal.BlueprintEditorLibrary.compile_blueprint(ent)
    save(ent)
    R['catalog'] = [c.get_path_name() for c in ecdo.get_editor_property('stage_catalog')]


try:
    run()
    R['status'] = 'COMPLETE'
except Exception:
    R['error'] = traceback.format_exc()
    unreal.log_error(R['error'])
with open(OUT, 'w', encoding='utf-8') as f:
    json.dump(R, f, indent=1, ensure_ascii=False)
log(R.get('status', 'ERROR'))
