import unreal,json
from pathlib import Path
EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert LS.load_level('/Game/Level/L_GadgetLab')
out={'actors':[],'gadgets':[]}
for a in EA.get_all_level_actors():
 p=a.get_actor_location();out['actors'].append({'label':a.get_actor_label(),'class':a.get_class().get_name(),'p':[p.x,p.y,p.z]})
bp=unreal.load_asset('/Game/BP/Core/BP_BoarGameInstance');d=unreal.get_default_object(bp.generated_class())
out['gi']=bp.get_path_name()
for c in d.get_editor_property('gadget_catalog'):
 cd=unreal.get_default_object(c)
 row={'class':c.get_path_name()}
 # Discover existing definition property without guessing actor instances.
 for key in ['gadget_data','gadget_definition','gadget_data_asset','data_asset']:
  try:
   data=cd.get_editor_property(key)
   if data:
    row.update({'property':key,'data':data.get_path_name(),'id':str(data.get_editor_property('gadget_id')),'name':str(data.get_editor_property('display_name')),'old_condition':str(data.get_editor_property('required_cleared_stage_id'))});break
  except Exception:pass
 out['gadgets'].append(row)
out['levels']=list(unreal.EditorAssetLibrary.list_assets('/Game/Level',True,False))
bp=unreal.load_asset('/Game/BP/Widget/WBP_GadgetLoadout');d=unreal.get_default_object(bp.generated_class());out['widget']={}
for k in ['entry_class','slots_panel_name','candidates_panel_name','status_widget_name','back_button_name']:out['widget'][k]=str(d.get_editor_property(k))
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
