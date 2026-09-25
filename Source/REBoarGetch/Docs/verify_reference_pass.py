import unreal,json
from pathlib import Path
r=Path(__file__).parent
before=json.loads((r/'audit_reference_pass.json').read_text(encoding='utf-8'))
es=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
result={}
for level in ['/Game/Level/L_Lobby','/Game/Level/Test']:
    es.load_level(level);actors=ea.get_all_level_actors();lookup={a.get_path_name():a for a in actors};changes=[]
    for row in before[level]:
        a=lookup.get(row['path'])
        if not a:changes.append(row['name']+' missing');continue
        p=a.get_actor_location();s=a.get_actor_scale3d()
        if any(abs(x-y)>.001 for x,y in zip(row['p']+row['s'],[p.x,p.y,p.z,s.x,s.y,s.z])):changes.append(row['name'])
    data={'existing_transform_changes':changes}
    if level.endswith('Test'):
        data['grass_material_actors']=[a.get_actor_label() for a in actors for c in a.get_components_by_class(unreal.StaticMeshComponent) if any(m and m.get_name()=='MI_Grass_Stage01' for m in c.get_materials())]
    else:
        data['new_collision_nav_issues']=[a.get_actor_label() for a in actors if a.get_actor_label().startswith('LobbyArt_Ref_') for c in a.get_components_by_class(unreal.StaticMeshComponent) if c.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION or c.get_editor_property('can_ever_affect_navigation')]
    result[level]=data
result['dirty_maps']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
result['dirty_art']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages() if p.get_path_name().startswith(('/Game/Lobby','/Game/Stage01/Ground'))]
(r/'verify_reference_pass.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
