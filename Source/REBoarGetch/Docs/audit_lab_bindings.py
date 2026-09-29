import unreal,json,traceback
from pathlib import Path
out={}
try:
 for p in ['/Game/BP/Core/BP_BoarGameInstance','/Game/BP/Widget/WBP_GadgetLoadout','/Game/BP/Lobby/BP_GM_GadgetLab','/Game/BP/Player/BP_BoarPlayerController','/Game/DataAssets/Gadget/DA_Gadget']:
  a=unreal.load_asset(p)
  if not a:continue
  c=unreal.get_default_object(a.generated_class()) if isinstance(a,unreal.Blueprint) else a
  r={'class':c.get_class().get_path_name()}
  for prop in ['gadget_catalog','loadout_widget_class','default_mapping_context','gadget_id','initially_unlocked','required_special_coin_count','test_level','slots_panel_name','candidates_panel_name','archive','gadget_test']:
   try:r[prop]=str(c.get_editor_property(prop))
   except:pass
  out[p]=r
 for p in ['/Game/Input/IMC_Default','/Game/Input/IMC_Global']:
  a=unreal.load_asset(p);out[p]=[(str(x.action.get_name()),str(x.key)) for x in a.get_editor_property('mappings')]
 out['assets_in_lab']=[(a.get_actor_label(),a.static_mesh_component.static_mesh.get_path_name()) for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a,unreal.StaticMeshActor) and a.static_mesh_component.static_mesh and ('GadgetResearch' in a.static_mesh_component.static_mesh.get_name() or 'GadgetPod' in a.static_mesh_component.static_mesh.get_name())]
except Exception:out['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
