import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level('/Game/Level/L_Stage01')
mel = unreal.MaterialEditingLibrary
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        m = c.get_editor_property('static_mesh')
        if not m or 'CagePlaza' not in m.get_name():
            continue
        o, e = a.get_actor_bounds(False)
        unreal.log('[Plaza] actor=%s label=%s mesh=%s loc=%s rot=%s scale=%s bounds_ext=%s tris=%d nanite=%s' % (
            a.get_name(), a.get_actor_label(), m.get_path_name(), a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d(),
            e, m.get_num_triangles(0), m.get_editor_property('nanite_settings').get_editor_property('enabled')))
        for i, mat in enumerate(c.get_materials()):
            base = mat.get_base_material() if mat else None
            node = mel.get_material_property_input_node(base, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET) if base else None
            unreal.log('[Plaza]   slot%d %s base=%s wpo=%s' % (i, mat.get_path_name() if mat else None, base.get_path_name() if base else None,
                       node.get_editor_property('desc') if node else None))
unreal.log('[Plaza] COMPLETE')
