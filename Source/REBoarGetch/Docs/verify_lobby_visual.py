import unreal,json
from pathlib import Path
root=Path(__file__).parent
before=json.loads((root/'audit_lobby_visual.json').read_text(encoding='utf-8'))
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
lookup={a.get_path_name():a for a in actors}
out={'existing_actor_changes':[],'new_collision_or_navigation':[],'texture_settings':{},'material_stats':{},'new_actor_count':0}
for row in before:
    a=lookup.get(row['path'])
    if not a:out['existing_actor_changes'].append([row['label'],'missing']);continue
    p=a.get_actor_location();r=a.get_actor_rotation();s=a.get_actor_scale3d()
    for k,v in [('p',[p.x,p.y,p.z]),('r',[r.pitch,r.yaw,r.roll]),('s',[s.x,s.y,s.z])]:
        if any(abs(x-y)>.001 for x,y in zip(row[k],v)):out['existing_actor_changes'].append([row['label'],k])
    for old,c in zip(row.get('meshes',[]),a.get_components_by_class(unreal.StaticMeshComponent)):
        if old['collision']!=str(c.get_collision_enabled()):out['existing_actor_changes'].append([row['label'],'collision'])
for a in actors:
    if not a.get_actor_label().startswith('LobbyArt_'):continue
    out['new_actor_count']+=1
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        if c.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION or c.get_editor_property('can_ever_affect_navigation'):out['new_collision_or_navigation'].append(a.get_actor_label())
for n in ['GrassBaseColor','GrassNormalMap','GrassRoughnessMap']:
    t=unreal.load_asset('/Game/Stage01/Ground/'+n)
    out['texture_settings'][n]={'srgb':t.get_editor_property('srgb'),'compression':str(t.get_editor_property('compression_settings'))}
for p in ['/Game/Stage01/Ground/Materials/M_Grass_LowPoly','/Game/Lobby/Materials/M_Lobby_ResearchHologram']:
    m=unreal.load_asset(p);out['material_stats'][p]={'loaded':bool(m)}
out['dirty_packages']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages() if p.get_path_name().startswith(('/Game/Lobby','/Game/Stage01/Ground'))]
out['dirty_maps']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
(root/'verify_lobby_visual.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
