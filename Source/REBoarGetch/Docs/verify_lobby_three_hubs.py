import unreal,json
from pathlib import Path
root=Path(__file__).parent
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();lookup={a.get_path_name():a for a in actors}
out={'existing_changes':[],'new_collision':[],'new_actors':0}
for row in json.loads((root/'audit_lobby_three_hubs.json').read_text()):
    a=lookup.get(row['path'])
    if not a:out['existing_changes'].append([row['label'],'missing']);continue
    p=a.get_actor_location();r=a.get_actor_rotation();s=a.get_actor_scale3d()
    for k,v in [('p',[p.x,p.y,p.z]),('r',[r.pitch,r.yaw,r.roll]),('s',[s.x,s.y,s.z])]:
        if any(abs(x-y)>.001 for x,y in zip(row[k],v)):out['existing_changes'].append([row['label'],k])
    for old,c in zip(row.get('meshes',[]),a.get_components_by_class(unreal.StaticMeshComponent)):
        if old['collision']!=str(c.get_collision_enabled()):out['existing_changes'].append([row['label'],'collision'])
for a in actors:
    if not a.get_actor_label().startswith('LobbyArt_Hub3_'):continue
    out['new_actors']+=1
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        if c.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION or c.get_editor_property('can_ever_affect_navigation'):out['new_collision'].append(a.get_actor_label())
out['dirty_maps']=[str(x) for x in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
(root/'verify_lobby_three_hubs.json').write_text(json.dumps(out,indent=2))
unreal.log('HUB3_VERIFY '+json.dumps(out))
