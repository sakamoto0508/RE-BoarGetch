import unreal, json
from pathlib import Path
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
out=[]
for a in actors:
    p=a.get_actor_location(); r=a.get_actor_rotation(); s=a.get_actor_scale3d()
    row={'label':a.get_actor_label(),'path':a.get_path_name(),'class':a.get_class().get_name(),
         'p':[p.x,p.y,p.z],'r':[r.pitch,r.yaw,r.roll],'s':[s.x,s.y,s.z]}
    meshes=[]
    for comp in a.get_components_by_class(unreal.StaticMeshComponent):
        mesh=comp.get_editor_property('static_mesh')
        meshes.append({'component':comp.get_path_name(),'mesh':mesh.get_path_name() if mesh else None,
                       'materials':[m.get_path_name() if m else None for m in comp.get_materials()],
                       'collision':str(comp.get_collision_enabled())})
    if meshes: row['meshes']=meshes
    out.append(row)
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
unreal.log('LOBBY_AUDIT '+str(len(out)))
