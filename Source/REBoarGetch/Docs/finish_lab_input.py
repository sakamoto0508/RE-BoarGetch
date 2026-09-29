import unreal,json,traceback
from pathlib import Path
AT=unreal.AssetToolsHelpers.get_asset_tools();AS=unreal.EditorAssetLibrary;out={}
try:
 action=unreal.load_asset('/Game/Input/Actions/IA_FacilityInteract')
 if not action:action=AT.create_asset('IA_FacilityInteract','/Game/Input/Actions',unreal.InputAction,unreal.InputAction_Factory())
 context=unreal.load_asset('/Game/Input/IMC_Facility')
 if not context:
  context=AT.create_asset('IMC_Facility','/Game/Input',unreal.InputMappingContext,unreal.InputMappingContext_Factory())
 context.unmap_all_keys_from_action(action)
 for name in ['E','Gamepad_DPad_Up']:
  key=unreal.Key();key.set_editor_property('key_name',name);context.map_key(action,key)
 assert AS.save_loaded_asset(action);assert AS.save_loaded_asset(context)
 bp=unreal.load_asset('/Game/BP/Player/BP_BoarPlayerController');cd=unreal.get_default_object(bp.generated_class())
 cd.set_editor_property('interact_action',action);cd.set_editor_property('facility_mapping_context',context)
 out['compiled']=[]
 for p in ['/Game/BP/Player/BP_BoarPlayerController','/Game/BP/Widget/WBP_GadgetLoadout','/Game/BP/Core/BP_BoarGameInstance','/Game/BP/Lobby/BP_GM_GadgetLab']:
  b=unreal.load_asset(p);unreal.BlueprintEditorLibrary.compile_blueprint(b);assert AS.save_loaded_asset(b);out['compiled'].append(p)
 out['controller_action']=str(cd.get_editor_property('interact_action'))
 out['context']=str(cd.get_editor_property('facility_mapping_context'))
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
