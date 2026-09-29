import unreal,json,traceback
from pathlib import Path
ROOT=Path(__file__).parent
exec((ROOT/'build_facility_levels.py').read_text(encoding='utf-8').split('def run():')[0])
out={}
try:
 # A dark backing makes the actual pale net readable without altering its material.
 mesh('DisplayStationBackdrop',(0,-150,240),(213,5,170),'Navy')
 mesh('DisplayCollectionBackdrop',(0,684,273),(300,5,225),'Navy')
 bp=unreal.load_asset('/Game/BP/Lobby/BP_Display_Net')
 unreal.BlueprintEditorLibrary.compile_blueprint(bp)
 objects=[unreal.get_default_object(bp.generated_class())]+[a for a in EA.get_all_level_actors() if a.get_actor_label().startswith('DisplayNet_')]
 for a in objects:
  c=a.get_component_by_class(unreal.SkeletalMeshComponent)
  c.set_editor_property('animation_mode',unreal.AnimationMode.ANIMATION_BLUEPRINT)
  c.set_update_animation_in_editor(False)
  c.set_component_tick_enabled(False)
 assert AS.save_loaded_asset(bp);assert LS.save_current_level()
 # Reload saved Lab and check the serialized instances, not just authoring objects.
 assert LS.load_level('/Game/Level/L_GadgetLab')
 exec((ROOT/'verify_real_display.py').read_text(encoding='utf-8'))
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
(ROOT/'complete_real_display.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
