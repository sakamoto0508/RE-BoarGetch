import unreal,json,traceback
from pathlib import Path
EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);out={}
def vec(v):return [v.x,v.y,v.z]
try:
 assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 # Preserve the user's current Lab edits before inspecting other requested gate levels.
 assert LS.save_current_level()
 out['level_assets']=list(unreal.EditorAssetLibrary.list_assets('/Game/Level',False,False))
 for level in ['L_Lobby','L_Archive','L_GadgetTest_Net']:
  assert LS.load_level('/Game/Level/'+level);rows=[]
  for a in EA.get_all_level_actors():
   n=a.get_actor_label()
   if any(s.lower() in n.lower() for s in ['gate','portal','return','entrance','Energy','Hub3']):
    r={'name':n,'class':a.get_class().get_name(),'p':vec(a.get_actor_location()),'r':str(a.get_actor_rotation()),'s':vec(a.get_actor_scale3d()),'hidden':a.get_editor_property('hidden')}
    if isinstance(a,unreal.StaticMeshActor):r.update(mesh=a.static_mesh_component.static_mesh.get_path_name() if a.static_mesh_component.static_mesh else None,visible=a.static_mesh_component.get_editor_property('visible'))
    rows.append(r)
  out[level]=rows
 assert LS.load_level('/Game/Level/L_GadgetLab')
except Exception:out['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
