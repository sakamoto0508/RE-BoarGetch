import unreal
mel = unreal.MaterialEditingLibrary
path = '/Game/InportAssets/LPRiverForest/Materials/M_LowPoly'
m = unreal.load_asset(path)
for e in mel.get_material_expressions(m):
    names = mel.get_material_expression_input_names(e)
    srcs = mel.get_inputs_for_material_expression(m, e)
    extra = ''
    for prop in ['parameter_name', 'material_function']:
        try:
            v = e.get_editor_property(prop)
            extra += ' %s=%s' % (prop, v.get_name() if hasattr(v, 'get_name') else v)
        except Exception:
            pass
    unreal.log('[Graph] %s%s pos=%s desc=%s in=%s' % (e.get_name(), extra, mel.get_material_expression_node_position(e), e.get_editor_property('desc'),
               [(n, s.get_name() if s else None) for n, s in zip(names, srcs)]))
for p in ['MP_BASE_COLOR', 'MP_EMISSIVE_COLOR', 'MP_WORLD_POSITION_OFFSET', 'MP_NORMAL', 'MP_ROUGHNESS']:
    n = mel.get_material_property_input_node(m, getattr(unreal.MaterialProperty, p))
    unreal.log('[Graph] prop %s <- %s' % (p, n.get_name() if n else None))
unreal.log('[Graph] COMPLETE')
