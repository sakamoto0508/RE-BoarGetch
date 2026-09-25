import unreal,json
from pathlib import Path
r=Path(__file__).parent
before=json.loads((r/'audit_gate_energy.json').read_text(encoding='utf-8'))
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();lookup={a.get_path_name():a for a in actors}
out={'unexpected_changes':[],'new_collision_nav_issues':[]}
for row in before:
    a=lookup.get(row['path'])
    if not a:out['unexpected_changes'].append(row['name']+' missing');continue
    p=a.get_actor_location();s=a.get_actor_scale3d()
    if row['name']!='Stage1Sign' and not row['name'].startswith('Port_ColumnCap_'):
        if any(abs(x-y)>.001 for x,y in zip(row['p']+row['s'],[p.x,p.y,p.z,s.x,s.y,s.z])):out['unexpected_changes'].append(row['name'])
    for old,c in zip(row['components'],a.get_components_by_class(unreal.StaticMeshComponent)):
        if old['collision']!=str(c.get_collision_enabled()):out['unexpected_changes'].append(row['name']+' collision')
for a in actors:
    if a.get_actor_label().startswith('LobbyArt_Energy_'):
        c=a.static_mesh_component
        if c.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION or c.get_editor_property('can_ever_affect_navigation'):out['new_collision_nav_issues'].append(a.get_actor_label())
    if a.get_actor_label()=='Stage1Sign':out['text']=str(a.get_components_by_class(unreal.TextRenderComponent)[0].get_editor_property('text'))
out['dirty_maps']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
out['dirty_lobby']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages() if p.get_path_name().startswith('/Game/Lobby')]
(r/'verify_gate_energy.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
