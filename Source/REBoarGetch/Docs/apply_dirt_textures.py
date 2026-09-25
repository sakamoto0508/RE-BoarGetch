import unreal,json,traceback
from pathlib import Path
root=Path(__file__).parent
exec((root/'build_lobby_visual.py').read_text(encoding='utf-8').split('try:build()')[0])
REPORT={'assets':[],'changed':[]}
def run():
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():raise RuntimeError('PIE active')
    if '/Game/Level/Test.' not in unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name():raise RuntimeError('Wrong map')
    audit=json.loads((root/'audit_dirt_textures.json').read_text(encoding='utf-8'))
    components={c.get_path_name():c for a in EA.get_all_level_actors() for c in a.get_components_by_class(unreal.StaticMeshComponent)}
    for row in audit['surfaces']:
        if components[row['component']].get_material(row['slot']).get_path_name()!=row['material']:raise RuntimeError('Material changed since audit')
    textures=[]
    for n,srgb,compression in [('GroundBaseColor',True,unreal.TextureCompressionSettings.TC_DEFAULT),('GroundNormalMap',False,unreal.TextureCompressionSettings.TC_NORMALMAP),('GroundRoughnessMap',False,unreal.TextureCompressionSettings.TC_GRAYSCALE)]:
        t=unreal.load_asset('/Game/Stage01/Ground/'+n)
        if not t:raise RuntimeError(n)
        t.set_editor_property('srgb',srgb);t.set_editor_property('compression_settings',compression);save(t);textures.append(t)
    m=create('/Game/Stage01/Ground/Materials/M_Ground_LowPoly_World',unreal.Material,unreal.MaterialFactoryNew())
    pos=node(m,unreal.MaterialExpressionWorldPosition)
    xy=node(m,unreal.MaterialExpressionComponentMask,r=True,g=True,b=False,a=False);link(pos,xy)
    uv=node(m,unreal.MaterialExpressionDivide);link(xy,uv,'A');link(scalar(m,'TileSizeCm',200),uv,'B')
    samples=[]
    for i,t in enumerate(textures):
        s=node(m,unreal.MaterialExpressionTextureSampleParameter2D,parameter_name=['GroundColor','GroundNormal','GroundRoughness'][i],texture=t,sampler_type=[unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE][i]);link(uv,s);samples.append(s)
    tint=node(m,unreal.MaterialExpressionMultiply);link(samples[0],tint,'A','RGB');link(scalar(m,'ColorBrightness',1),tint,'B');output(tint,unreal.MaterialProperty.MP_BASE_COLOR)
    flat=node(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(0,0,1,1))
    normal=node(m,unreal.MaterialExpressionLinearInterpolate);link(flat,normal,'A');link(samples[1],normal,'B','RGB');link(scalar(m,'NormalStrength',.15),normal,'Alpha');output(normal,unreal.MaterialProperty.MP_NORMAL)
    rough=node(m,unreal.MaterialExpressionLinearInterpolate);link(scalar(m,'RoughnessMin',.78),rough,'A');link(scalar(m,'RoughnessMax',.95),rough,'B');link(samples[2],rough,'Alpha','R');output(rough,unreal.MaterialProperty.MP_ROUGHNESS)
    ML.layout_material_expressions(m);ML.recompile_material(m);save(m)
    instances={}
    for name,brightness in [('Dry',1),('Damp',.78)]:
        mi=create('/Game/Stage01/Ground/Materials/MI_Ground_Stage01_'+name,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew());ML.set_material_instance_parent(mi,m);ML.set_material_instance_scalar_parameter_value(mi,'ColorBrightness',brightness);save(mi);instances[name]=mi
    before={c.get_path_name():(str(c.get_collision_enabled()),str(c.get_component_transform())) for c in components.values()}
    for row in audit['surfaces']:
        mi=instances['Damp' if 'DampSoil' in row['material'] else 'Dry']
        c=components[row['component']];c.set_material(row['slot'],mi)
        if c.get_material(row['slot'])!=mi:raise RuntimeError('Assignment failed')
        REPORT['changed'].append({'actor':row['actor'],'material':mi.get_path_name()})
    REPORT['transform_collision_changes']=[p for p,c in components.items() if before[p]!=(str(c.get_collision_enabled()),str(c.get_component_transform()))]
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():raise RuntimeError('Save failed')
    REPORT['saved']=True
    REPORT['dirty_maps']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    REPORT['dirty_ground']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages() if p.get_path_name().startswith('/Game/Stage01/Ground')]
try:run()
except Exception:REPORT['error']=traceback.format_exc();unreal.log_error(REPORT['error'])
finally:(root/'apply_dirt_textures.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
