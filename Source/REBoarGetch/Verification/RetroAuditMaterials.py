"""Read-only: character meshes/materials and texture-sample UV sources of project base materials.
Writes Verification/RetroAuditMaterials.json."""
from pathlib import Path
import json
import unreal

HERE = Path(__file__).resolve().parent
mel = unreal.MaterialEditingLibrary
out = {'characters': {}, 'materials': {}}

def walk_bp(path):
    bp = unreal.load_asset(path)
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    cdo = unreal.get_default_object(cls)
    info = []
    subsys = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = subsys.k2_gather_subobject_data_for_blueprint(bp)
    for h in handles:
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
        if isinstance(obj, unreal.SkinnedMeshComponent) or isinstance(obj, unreal.StaticMeshComponent):
            mesh = None
            try:
                mesh = obj.get_skeletal_mesh_asset() if isinstance(obj, unreal.SkinnedMeshComponent) else obj.get_editor_property('static_mesh')
            except Exception:
                pass
            mats = []
            src = []
            if mesh:
                try:
                    src = [m.get_editor_property('material_interface') for m in mesh.get_editor_property('materials')]
                except Exception:
                    src = [m.get_editor_property('material_interface') for m in mesh.get_editor_property('static_materials')]
            overrides = obj.get_editor_property('override_materials')
            for i, m in enumerate(src):
                o = overrides[i] if i < len(overrides) and overrides[i] else m
                if o:
                    mats.append({'mi': o.get_path_name(), 'base': o.get_base_material().get_path_name()})
            info.append({'comp': obj.get_name(), 'class': obj.get_class().get_name(),
                         'mesh': mesh.get_path_name() if mesh else None, 'materials': mats,
                         'tris': mesh.get_num_triangles(0) if mesh and hasattr(mesh, 'get_num_triangles') else None})
    return info

for bp in ['/Game/BP/Player/BP_PlayerCharacter', '/Game/BP/Boar/BP_NormalBoar']:
    try:
        out['characters'][bp] = walk_bp(bp)
    except Exception as e:
        out['characters'][bp] = 'error ' + str(e)

def describe(mat_path):
    m = unreal.load_asset(mat_path)
    exprs = mel.get_material_expressions(m)
    res = {'exprs': len(exprs), 'samples': [], 'classes': {}}
    for e in exprs:
        c = e.get_class().get_name()
        res['classes'][c] = res['classes'].get(c, 0) + 1
        if isinstance(e, unreal.MaterialExpressionTextureSample):
            inputs = mel.get_inputs_for_material_expression(m, e)
            names = mel.get_material_expression_input_names(e)
            src = {n: (i.get_class().get_name() if i else None) for n, i in zip(names, inputs)}
            tex = e.get_editor_property('texture')
            res['samples'].append({'node': e.get_name(), 'class': c, 'inputs': src,
                                   'texture': tex.get_path_name() if tex else None,
                                   'param': str(e.get_editor_property('parameter_name')) if hasattr(e, 'parameter_name') else None})
    try:
        res['used_textures'] = [t.get_path_name() for t in mel.get_material_used_textures(m)]
    except Exception as ex:
        res['used_textures'] = str(ex)
    for t in (res['used_textures'] if isinstance(res['used_textures'], list) else []):
        tx = unreal.load_asset(t)
        res.setdefault('tex_info', {})[t] = {
            'size': [tx.blueprint_get_size_x(), tx.blueprint_get_size_y()],
            'filter': str(tx.get_editor_property('filter')), 'lod_group': str(tx.get_editor_property('lod_group')),
            'max': tx.get_editor_property('max_texture_size'), 'compression': str(tx.get_editor_property('compression_settings'))}
    return res

paths = set()
for v in out['characters'].values():
    if isinstance(v, list):
        for c in v:
            for mm in c['materials']:
                paths.add(mm['base'])
paths |= {'/Game/Stage01/Ground/Materials/M_Grass_LowPoly_World', '/Game/Stage01/Ground/Materials/M_Ground_LowPoly_World',
          '/Game/InportAssets/LPRiverForest/Materials/M_LowPoly', '/Game/InportAssets/LevelPrototyping/Materials/M_FlatCol',
          '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyGrass', '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyTerrain',
          '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyWater'}
for p in sorted(paths):
    p = p.split('.')[0]
    try:
        out['materials'][p] = describe(p)
    except Exception as e:
        out['materials'][p] = 'error ' + str(e)
(HERE / 'RetroAuditMaterials.json').write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding='utf-8')
unreal.log('[RetroAudit] COMPLETE')
