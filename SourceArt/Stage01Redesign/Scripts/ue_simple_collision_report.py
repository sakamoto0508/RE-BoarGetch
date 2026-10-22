"""Read-only (GUI editor): simple collision shape count per mesh placed with collision in the redesign level."""
import unreal
sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert les.load_level('/Game/Level/L_Stage01')
seen = {}
for a in eas.get_all_level_actors():
    if isinstance(a, unreal.StaticMeshActor) and a.static_mesh_component.static_mesh and \
            str(a.static_mesh_component.get_collision_enabled()).find('NO_COLLISION') < 0:
        m = a.static_mesh_component.static_mesh
        seen[m.get_path_name()] = m
for p, m in sorted(seen.items()):
    flag = str(m.get_editor_property('body_setup').get_editor_property('collision_trace_flag')).split('.')[-1].split(':')[0]
    unreal.log('[S01] %-75s simple=%d %s' % (p.split('.')[0], sms.get_simple_collision_count(m), flag))
unreal.log('[S01] COMPLETE')
unreal.SystemLibrary.quit_editor()
