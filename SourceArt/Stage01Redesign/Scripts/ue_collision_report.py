"""Read-only: per mesh, the asset's default collision vs the placed components' collision in the redesign level."""
import collections, unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert les.load_level('/Game/Level/L_Stage01')
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rows = collections.defaultdict(collections.Counter)
asset = {}
for a in eas.get_all_level_actors():
    if not isinstance(a, unreal.StaticMeshActor):
        continue
    c = a.static_mesh_component
    m = c.static_mesh
    if not m:
        continue
    n = m.get_path_name().split('.')[-1]
    bs = m.get_editor_property('body_setup')
    agg = bs.get_editor_property('agg_geom')
    sml = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary
    nsimple = sml.get_simple_collision_count(m)
    asset[n] = '%s simple=%d default_profile=%s' % (str(bs.get_editor_property('collision_trace_flag')).split('.')[-1].split(':')[0],
                                                   nsimple, bs.get_editor_property('default_instance').get_editor_property('collision_profile_name'))
    rows[n]['%s/%s' % (c.get_collision_profile_name(), str(c.get_collision_enabled()).split('.')[-1].split(':')[0])] += 1
for n in sorted(rows):
    unreal.log('[S01] %-30s asset: %-55s placed: %s' % (n, asset[n], dict(rows[n])))
unreal.log('[S01] COMPLETE')
