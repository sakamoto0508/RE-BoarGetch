import unreal,json,traceback
from pathlib import Path
out={}
try:
 EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);aa={a.get_actor_label():a for a in EA.get_all_level_actors()}
 for name,p in [('ReturnToLobby',(-1070,0,140)),('OpenFacilityUI',(0,80,130)),('HubTestTrigger',(1010,0,140))]:
  assert (aa[name].get_actor_location()-unreal.Vector(*p)).length()<.01,name
 assert 'L_Lobby' in str(aa['ReturnToLobby'].get_editor_property('destination'))
 assert aa['OpenFacilityUI'].get_editor_property('action')==unreal.HubPortalAction.LOADOUT
 assert aa['HubTestTrigger'].get_editor_property('action')==unreal.HubPortalAction.GADGET_TEST
 out['display']=[]
 for name in ['DisplayNet_Station','DisplayNet_Collection']:
  a=aa[name];c=a.get_component_by_class(unreal.SkeletalMeshComponent)
  assert c.get_skinned_asset().get_path_name()=='/Game/InportAssets/mushitoriami/mushitoriami.mushitoriami'
  assert not a.get_actor_enable_collision() and not c.get_editor_property('generate_overlap_events')
  out['display'].append({'name':name,'mesh':c.get_skinned_asset().get_path_name(),'materials':[m.get_path_name() for m in c.get_materials()],'collision':str(c.get_collision_enabled()),'components':[x.get_class().get_name() for x in a.get_components_by_class(unreal.ActorComponent)]})
 assert LS.save_current_level();out['saved']=True
 out['dirty_maps']=[str(x) for x in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
 out['dirty_assets']=[str(x) for x in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
