import unreal
P = ['/Game/Stage01/Quality/Materials/MI_S01_Quality_%s' % n for n in ['Blue', 'Cyan', 'Glass', 'Ivory', 'Machine', 'Yellow', 'Stone']] + \
    ['/Game/Stage01/Quality/Materials/MI_S01_Nature_%s' % n for n in ['Cliff', 'Rock', 'Leaves', 'Trunk']] + \
    ['/Game/Stage01/Blockout/Materials/MI_Stage01_Blockout_%s' % n for n in ['Wood', 'White', 'Blue', 'Amber', 'Path', 'Rock', 'Grass']] + \
    ['/Game/Stage01/Ground/Materials/' + n for n in ['MI_Grass_Stage01', 'MI_Ground_Stage01_Dry', 'MI_Ground_Stage01_Damp', 'MI_S01_Ground_Turf', 'MI_S01_Ground_TurfLight', 'MI_S01_Ground_DrySoil', 'MI_S01_Ground_DampSoil']] + \
    ['/Game/Stage01/Art/Materials/MI_Stage01_ShallowWater', '/Game/Stage01/Art/Materials/MI_Stage01_Grass', '/Game/InportAssets/LPRiverForest/Materials/Trees/MI_TreeWood']
for p in P:
    m = unreal.load_asset(p)
    if not m:
        unreal.log('[S01] MISSING ' + p); continue
    par = m.get_editor_property('parent')
    s = [(str(x.parameter_info.name), round(x.parameter_value, 3)) for x in m.get_editor_property('scalar_parameter_values')]
    v = [(str(x.parameter_info.name), [round(x.parameter_value.r, 2), round(x.parameter_value.g, 2), round(x.parameter_value.b, 2)]) for x in m.get_editor_property('vector_parameter_values')]
    t = [(str(x.parameter_info.name), x.parameter_value.get_name() if x.parameter_value else None) for x in m.get_editor_property('texture_parameter_values')]
    unreal.log('[S01] %s <- %s | %s %s %s' % (p.split('/')[-1], par.get_path_name() if par else None, s, v, t))
unreal.log('[S01] COMPLETE')
