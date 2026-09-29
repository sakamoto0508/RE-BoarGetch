import unreal,json,traceback
from pathlib import Path
out={}
try:
 EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);AS=unreal.EditorAssetLibrary
 w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();out['world']=w.get_path_name()
 out['dirty_maps']=[str(x) for x in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
 folder='/Game/InportAssets/ImportMeshy/Meshy_AI_Neon_Portal_Frame_0928055927_texture/StaticMeshes'
 out['imported']=[]
 for p in AS.list_assets(folder,True,False):
  a=unreal.load_asset(p)
  if isinstance(a,unreal.StaticMesh):out['imported'].append({'path':p,'bounds':str(a.get_bounding_box()),'materials':[str(x.material_interface) for x in a.get_editor_property('static_materials')]})
 out['actors']=[]
 for a in EA.get_all_level_actors():
  n=a.get_actor_label()
  if any(s.lower() in n.lower() for s in ['gate','portal','return','entrance','HubTest','Meshy']):
   r={'name':n,'path':a.get_path_name(),'class':a.get_class().get_name(),'p':str(a.get_actor_location()),'rot':str(a.get_actor_rotation()),'scale':str(a.get_actor_scale3d()),'hidden':a.get_editor_property('hidden')}
   if isinstance(a,unreal.StaticMeshActor):r['mesh']=str(a.static_mesh_component.static_mesh)
   out['actors'].append(r)
 out['existing_portal_assets']=list(AS.list_assets('/Game/Lobby/Materials',False,False))
except Exception:out['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))

