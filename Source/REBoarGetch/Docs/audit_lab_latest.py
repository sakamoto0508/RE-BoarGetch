import unreal,json,traceback
from pathlib import Path
out={};ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
try:
 w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();out['world']=w.get_path_name()
 out['actors']=[]
 for a in ea.get_all_level_actors():
  r={'name':a.get_actor_label(),'class':a.get_class().get_name(),'pos':str(a.get_actor_location()),'hidden':a.get_editor_property('hidden')}
  if isinstance(a,unreal.StaticMeshActor):r['mesh']=a.static_mesh_component.static_mesh.get_path_name() if a.static_mesh_component.static_mesh else None
  if a.get_class().get_name()=='HubPortal':r.update(action=str(a.get_editor_property('action')),destination=str(a.get_editor_property('destination')))
  out['actors'].append(r)
 out['imports']=[p for p in unreal.EditorAssetLibrary.list_assets('/Game/InportAssets/ImportMeshy',True,False) if '/StaticMeshes/' in p]
 out['gadget_assets']=list(unreal.EditorAssetLibrary.list_assets('/Game/Gadget',True,False))
except Exception:out['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
