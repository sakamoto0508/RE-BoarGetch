import unreal
from pathlib import Path
exec((Path(__file__).parent/'build_lobby_visual.py').read_text(encoding='utf-8').split('try:build()')[0])
for n,color in [('White',(.88,.93,.96)),('Navy',(.015,.035,.065)),('Yellow',(1,.65,.02)),('Glow',(.01,.65,1))]:
    path='/Game/BP/Cage/Materials/M_Cage_'+n
    if AS.does_asset_exist(path):raise RuntimeError('Existing asset '+path)
    m=create(path,unreal.Material,unreal.MaterialFactoryNew())
    tint=node(m,unreal.MaterialExpressionVectorParameter,parameter_name='Tint',default_value=unreal.LinearColor(*color,1))
    output(tint,unreal.MaterialProperty.MP_BASE_COLOR)
    output(constant(m,.8),unreal.MaterialProperty.MP_ROUGHNESS)
    if n=='Glow':
        mult=node(m,unreal.MaterialExpressionMultiply);link(tint,mult,'A');link(scalar(m,'Intensity',1.2),mult,'B');output(mult,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    ML.recompile_material(m);save(m)
unreal.log('CAGE_MATERIALS_SAVED')
unreal.log('CAGE_DIRTY '+str(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())+' '+str(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()))
