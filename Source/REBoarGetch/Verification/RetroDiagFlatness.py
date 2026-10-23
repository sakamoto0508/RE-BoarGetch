import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level('/Game/Level/L_Stage01')
rows = []
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        m = c.get_editor_property('static_mesh')
        if not m: continue
        o, e = a.get_actor_bounds(False)
        ratio = e.z / max(e.x, e.y, 1.0)
        rows.append((round(ratio, 3), round(max(e.x, e.y)), round(e.z), m.get_name()))
seen = set()
for r in sorted(rows):
    if r[3] in seen: continue
    seen.add(r[3])
    unreal.log('[Flat] ratio=%s halfXY=%s halfZ=%s %s' % r)
unreal.log('[Flat] COMPLETE')
