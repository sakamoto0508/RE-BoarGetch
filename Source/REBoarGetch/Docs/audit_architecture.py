import unreal,json,traceback
from pathlib import Path
EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);out={}
def v(p):return [p.x,p.y,p.z]
try:
 assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 out['dirty']=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
 assert all(p in ['/Game/Level/L_Lobby','/Game/Level/L_GadgetLab'] for p in out['dirty'])
 if out['dirty']:assert LS.save_current_level()
 for level in ['L_GadgetLab','L_Lobby']:
  assert LS.load_level('/Game/Level/'+level);rows=[]
  for a in EA.get_all_level_actors():
   n=a.get_actor_label();c=a.get_class().get_name()
   if a.get_editor_property('hidden'):continue
   if any(s in n.lower() for s in ['floor','wall','pod','station','portal','start','light','sun','plaza','gate','ceiling']) or c in ['HubPortal','GadgetCollectionPod']:
    r={'name':n,'class':c,'p':v(a.get_actor_location()),'s':v(a.get_actor_scale3d())}
    if isinstance(a,unreal.StaticMeshActor):r['mesh']=a.static_mesh_component.static_mesh.get_path_name() if a.static_mesh_component.static_mesh else None
    rows.append(r)
  out[level]=rows
 assert LS.load_level('/Game/Level/L_GadgetLab')
except Exception:out['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
