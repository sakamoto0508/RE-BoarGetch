import unreal,json,traceback
from pathlib import Path
out={'blueprints':{},'levels':{}}
EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);AS=unreal.EditorAssetLibrary
try:
 assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 for p in ['/Game/BP/Widget/WBP_GadgetLoadout','/Game/BP/Widget/WBP_LoadoutEntry','/Game/BP/Lobby/BP_GM_GadgetLab','/Game/BP/Lobby/BP_GM_GadgetTest_Net','/Game/BP/Core/BP_BoarGameInstance']:
  b=unreal.load_asset(p);unreal.BlueprintEditorLibrary.compile_blueprint(b);out['blueprints'][p]=AS.save_loaded_asset(b)
 d=unreal.load_asset('/Game/DataAssets/Gadget/DA_Gadget');out['net']={'id':str(d.get_editor_property('gadget_id')),'initial':d.get_editor_property('initially_unlocked'),'test_level':str(d.get_editor_property('test_level'))};assert out['net']['initial'] and 'L_GadgetTest_Net' in out['net']['test_level']
 for level in ['L_GadgetTest_Net','L_GadgetLab']:
  assert LS.load_level('/Game/Level/'+level)
  aa={a.get_actor_label():a for a in EA.get_all_level_actors()};portals=[]
  for n,a in aa.items():
   if isinstance(a,unreal.HubPortal):portals.append({'name':n,'action':str(a.get_editor_property('action')),'destination':str(a.get_editor_property('destination'))})
  row={'portals':portals}
  if level=='L_GadgetTest_Net':
   assert 'L_GadgetLab' in str(aa['ReturnToGadgetLab'].get_editor_property('destination'))
   targets=[a for a in aa.values() if isinstance(a,unreal.NetPracticeBoar)];assert len(targets)==2
   mode=unreal.get_default_object(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_world_settings().get_editor_property('default_game_mode'))
   assert mode.get_editor_property('gadget_test') and 'BP_NetGadget' in str(mode.get_editor_property('test_gadget_class'))
   row['targets']=len(targets);row['trial_class']=str(mode.get_editor_property('test_gadget_class'))
  else:
   assert 'L_Lobby' in str(aa['ReturnToLobby'].get_editor_property('destination'))
   tests=[a for a in aa.values() if isinstance(a,unreal.HubPortal) and a.get_editor_property('action')==unreal.HubPortalAction.GADGET_TEST];assert len(tests)==1
   row['test_visuals']=len(tests[0].get_editor_property('test_visual_actors'));assert row['test_visuals']>0
   baseline=json.loads((Path(__file__).parent/'build_gadget_lab_hub.json').read_text())
   row['retained_hidden']=[]
   for n in baseline['hidden']:
    a=aa[n];assert a.get_editor_property('hidden') and not a.get_actor_enable_collision(),n;row['retained_hidden'].append(n)
  assert LS.save_current_level();out['levels'][level]=row
 out['dirty_maps']=[str(x) for x in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
 out['dirty_assets']=[str(x) for x in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')


