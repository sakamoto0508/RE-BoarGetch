import unreal
mel = unreal.MaterialEditingLibrary
m = unreal.load_asset('/Game/InportAssets/LPRiverForest/Materials/M_LowPolyWater')
ex = {e.get_name(): e for e in mel.get_material_expressions(m)}
def show(name, depth=0):
    e = ex[name]
    names = mel.get_material_expression_input_names(e)
    srcs = mel.get_inputs_for_material_expression(m, e)
    unreal.log('[Diag] %s%s %s' % ('  ' * depth, name, [(n, s.get_name() if s else None) for n, s in zip(names, srcs)]))
    if depth < 3:
        for s in srcs:
            if s: show(s.get_name(), depth + 1)
for n in ['MaterialExpressionReroute_13', 'MaterialExpressionConstant_10', 'MaterialExpressionMultiply_6']:
    show(n)
for p in ['MP_REFRACTION', 'MP_PIXEL_DEPTH_OFFSET', 'MP_AMBIENT_OCCLUSION', 'MP_TANGENT', 'MP_ANISOTROPY', 'MP_CUSTOM_DATA0', 'MP_CUSTOM_DATA1']:
    n = mel.get_material_property_input_node(m, getattr(unreal.MaterialProperty, p))
    unreal.log('[Diag] prop %s -> %s' % (p, n.get_name() if n else None))
unreal.log('[Diag] blend=%s shading=%s' % (m.get_editor_property('blend_mode'), m.get_editor_property('shading_model')))
