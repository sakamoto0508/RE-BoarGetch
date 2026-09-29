import unreal,json,traceback
from pathlib import Path
out={}
try:
 bp=unreal.load_asset('/Game/BP/Lobby/BP_Display_Net');cd=unreal.get_default_object(bp.generated_class())
 for a in [cd]+[a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label().startswith('DisplayNet_')]:
  c=a.get_component_by_class(unreal.SkeletalMeshComponent);c.set_update_animation_in_editor(True);c.set_component_tick_enabled(True)
  c.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
 unreal.EditorAssetLibrary.save_loaded_asset(bp)
 unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
 out['saved']=True
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
Path(__file__).with_suffix('.json').write_text(json.dumps(out))
