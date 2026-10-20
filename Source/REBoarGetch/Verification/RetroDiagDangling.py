import unreal
mel = unreal.MaterialEditingLibrary
PROPS = ['MP_BASE_COLOR','MP_EMISSIVE_COLOR','MP_NORMAL','MP_ROUGHNESS','MP_METALLIC','MP_SPECULAR','MP_OPACITY','MP_OPACITY_MASK','MP_AMBIENT_OCCLUSION','MP_WORLD_POSITION_OFFSET','MP_SUBSURFACE_COLOR']
def key(o): return o.get_path_name() if o else None
for path in ['/Game/InportAssets/LPRiverForest/Materials/M_LowPoly', '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyGrass',
             '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyWater', '/Game/Stage01/Ground/Materials/M_Grass_LowPoly_World']:
    m = unreal.load_asset(path)
    exprs = mel.get_material_expressions(m)
    used = set()
    for e in exprs:
        for src in mel.get_inputs_for_material_expression(m, e):
            if src: used.add(key(src))
    props = {}
    for p in PROPS:
        try:
            n = mel.get_material_property_input_node(m, getattr(unreal.MaterialProperty, p))
        except Exception:
            n = None
        if n: props[p] = (n.get_name(), str(n.get_editor_property('desc')))
    dangling = [(e.get_name(), e.get_class().get_name(), str(e.get_editor_property('desc')), mel.get_material_expression_node_position(e))
                for e in exprs if key(e) not in used and e.get_name() not in [v[0] for v in props.values()]]
    unreal.log('[Diag] %s props=%s' % (path.split('/')[-1], props))
    for d in dangling:
        if 'Comment' not in d[1] and 'Parameter' not in d[1]:
            unreal.log('[Diag]    dangling %s' % (d,))
    for e in exprs:
        if isinstance(e, unreal.MaterialExpressionTextureSample) and 'Object' not in e.get_class().get_name():
            names = mel.get_material_expression_input_names(e)
            srcs = mel.get_inputs_for_material_expression(m, e)
            unreal.log('[Diag]    sample %s mode=%s inputs=%s' % (e.get_name(), e.get_editor_property('mip_value_mode'), [(n, s.get_name() if s else None) for n, s in zip(names, srcs)]))
        if isinstance(e, unreal.MaterialExpressionPanner):
            srcs = mel.get_inputs_for_material_expression(m, e)
            unreal.log('[Diag]    panner %s inputs=%s' % (e.get_name(), [s.get_name() if s else None for s in srcs]))
unreal.log('[Diag] COMPLETE')
