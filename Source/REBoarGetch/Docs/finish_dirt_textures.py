import unreal,json
from pathlib import Path
root=Path(__file__).parent
audit=json.loads((root/'audit_dirt_textures.json').read_text(encoding='utf-8'))
report=json.loads((root/'apply_dirt_textures.json').read_text(encoding='utf-8'))
components={c.get_path_name():c for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() for c in a.get_components_by_class(unreal.StaticMeshComponent)}
def state(c):return (str(c.get_collision_enabled()),str(c.get_editor_property('relative_location')),str(c.get_editor_property('relative_rotation')),str(c.get_editor_property('relative_scale3d')))
before={p:state(c) for p,c in components.items()}
for row in audit['surfaces']:
    c=components[row['component']]
    if c.get_material(row['slot']).get_path_name()!=row['material']:raise RuntimeError('Unexpected material')
    mi=unreal.load_asset('/Game/Stage01/Ground/Materials/MI_Ground_Stage01_'+('Damp' if 'DampSoil' in row['material'] else 'Dry'))
    c.set_material(row['slot'],mi)
    if c.get_material(row['slot'])!=mi:raise RuntimeError('Assignment failed')
    report['changed'].append({'actor':row['actor'],'material':mi.get_path_name()})
report['transform_collision_changes']=[p for p,c in components.items() if before[p]!=state(c)]
if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():raise RuntimeError('Save failed')
report.pop('error',None);report['saved']=True
report['dirty_maps']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
report['dirty_ground']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages() if p.get_path_name().startswith('/Game/Stage01/Ground')]
(root/'apply_dirt_textures.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
