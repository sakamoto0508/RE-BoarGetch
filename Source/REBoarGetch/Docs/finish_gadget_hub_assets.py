import unreal,json,traceback
from pathlib import Path
ROOT=Path(__file__).parent
exec((ROOT/'build_facility_levels.py').read_text(encoding='utf-8').split('def run():')[0])
out={}
try:
 assert LS.load_level('/Game/Level/L_GadgetLab')
 created=[]
 for m in ['Navy','Ivory','Yellow','Glow','Ice','Cyan','Holo','Panel']:
  sm=unreal.load_asset('/Game/Lobby/Meshes/SM_GadgetHub_Equipment_'+m);assert sm
  old=actors().get('Equipment_'+m)
  if old and old.static_mesh_component.static_mesh==sm:
   old.set_actor_hidden_in_game(False);old.set_is_temporarily_hidden_in_editor(False);old.static_mesh_component.set_visibility(True)
   created.append(old.get_actor_label())
  else:
   a=mesh('HubStructure_'+m,(0,0,0),(100,100,100),m,shape=sm);a.set_folder_path('Facility/GadgetHub');created.append(a.get_actor_label())
 assert LS.save_current_level();out['visible_batches']=created
 b=unreal.load_asset('/Game/BP/Widget/WBP_LoadoutEntry');tree=unreal.find_object(b,'WidgetTree');w=unreal.find_object(tree,'RowSize');out['row_height']=w.get_editor_property('height_override')
 out['dirty_maps']=[str(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
(ROOT/'finish_gadget_hub_assets.json').write_text(json.dumps(out,indent=2))
