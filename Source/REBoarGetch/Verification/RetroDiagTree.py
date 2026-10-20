import unreal
mel = unreal.MaterialEditingLibrary
for p in ['/Game/InportAssets/LPRiverForest/Meshes/Trees/SM_LPTree01', '/Game/InportAssets/LPRiverForest/Meshes/Trees/SM_LPTree02']:
    m = unreal.load_asset(p)
    for s in m.get_editor_property('static_materials'):
        mi = s.get_editor_property('material_interface')
        info = mi.get_path_name() if mi else None
        if isinstance(mi, unreal.MaterialInstanceConstant):
            par = mi.get_editor_property('parent')
            sw = [ (str(x.get_editor_property('parameter_info').get_editor_property('name')), x.get_editor_property('value')) for x in mi.get_editor_property('static_parameters_runtime').get_editor_property('static_switch_parameters')] if hasattr(mi,'static_parameters_runtime') else []
            vec = [(str(v.get_editor_property('parameter_info').get_editor_property('name')), v.get_editor_property('parameter_value')) for v in mi.get_editor_property('vector_parameter_values')]
            sc = [(str(v.get_editor_property('parameter_info').get_editor_property('name')), v.get_editor_property('parameter_value')) for v in mi.get_editor_property('scalar_parameter_values')]
            info += ' parent=%s vec=%s scal=%s sw=%s' % (par.get_path_name() if par else None, vec, sc, sw)
        unreal.log('[Tree] %s slot %s: %s' % (p.split('/')[-1], s.get_editor_property('material_slot_name'), info))
unreal.log('[Tree] COMPLETE')
