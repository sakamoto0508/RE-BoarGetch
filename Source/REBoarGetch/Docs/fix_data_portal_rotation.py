import unreal,json,traceback
from pathlib import Path
EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
plans={'L_Lobby':{'Stage':-90,'Gadget':180,'Archive':0},'L_GadgetLab':{'Return':90,'Test':-90},'L_Archive':{'Return':90},'L_GadgetTest_Net':{'Return':90}}
out={}
try:
 for level,angles in plans.items():
  assert LS.load_level('/Game/Level/'+level)
  for a in EA.get_all_level_actors():
   n=a.get_actor_label()
   if n.startswith('DataPortal_'):a.set_actor_rotation(unreal.Rotator(pitch=0,yaw=angles[n.split('_')[1]],roll=0),False)
  assert LS.save_current_level();out[level]=True
 assert LS.load_level('/Game/Level/L_Lobby')
except Exception:out['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(out))
