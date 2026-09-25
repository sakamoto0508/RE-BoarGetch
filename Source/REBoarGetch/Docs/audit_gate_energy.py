import unreal,json
from pathlib import Path
if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():raise RuntimeError('PIE active')
if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():raise RuntimeError('Unsaved map')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/Level/L_Lobby')
out=[]
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    n=a.get_actor_label()
    if not any(s in n for s in ['Gate','Column','Stage1Sign','Stage01','Entrance','Wall','PlayerStart']):continue
    p=a.get_actor_location();s=a.get_actor_scale3d();r=a.get_actor_rotation()
    d={'name':n,'path':a.get_path_name(),'class':a.get_class().get_name(),'p':[p.x,p.y,p.z],'s':[s.x,s.y,s.z],'r':[r.pitch,r.yaw,r.roll],'components':[]}
    for c in a.get_components_by_class(unreal.StaticMeshComponent):d['components'].append({'path':c.get_path_name(),'collision':str(c.get_collision_enabled()),'mesh':str(c.get_editor_property('static_mesh')),'mats':[m.get_path_name() if m else None for m in c.get_materials()]})
    if a.get_components_by_class(unreal.TextRenderComponent):
        t=a.get_components_by_class(unreal.TextRenderComponent)[0];d['text']=str(t.get_editor_property('text'));d['font']=str(t.get_editor_property('font'));d['text_mat']=str(t.get_editor_property('text_material'))
    out.append(d)
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
