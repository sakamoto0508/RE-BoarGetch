import unreal,json
from pathlib import Path
es=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():raise RuntimeError('PIE active')
if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():raise RuntimeError('Unsaved map present')
out={}
for level in ['/Game/Level/L_Lobby','/Game/Level/Test']:
    if not es.load_level(level):raise RuntimeError(level)
    rows=[]
    for a in ea.get_all_level_actors():
        p=a.get_actor_location();s=a.get_actor_scale3d()
        row={'name':a.get_actor_label(),'path':a.get_path_name(),'p':[p.x,p.y,p.z],'s':[s.x,s.y,s.z],'meshes':[]}
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            mesh=c.get_editor_property('static_mesh')
            row['meshes'].append({'mesh':mesh.get_path_name() if mesh else None,'materials':[m.get_path_name() if m else None for m in c.get_materials()]})
        rows.append(row)
    out[level]=rows
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
