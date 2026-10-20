import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level('/Game/Art/Rendering/Retro/L_RetroCompare')
rows = []
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        m = c.get_editor_property('static_mesh')
        if not m: continue
        mats = [x.get_base_material().get_name() if x else '-' for x in c.get_materials()]
        origin, ext = a.get_actor_bounds(False)
        r = (ext.x**2 + ext.y**2 + ext.z**2) ** .5
        rows.append((r, a.get_actor_label(), m.get_name(), ','.join(mats), round(origin.z,1), round(ext.z,1)))
rows.sort(reverse=True)
for row in rows[:25]:
    unreal.log('[Bounds] r=%.0f %s mesh=%s mats=%s z=%s ez=%s' % row)
unreal.log('[Bounds] COMPLETE')
