import unreal,json,traceback
from pathlib import Path
out={'blueprints':[]}
try:
 E=unreal.EditorAssetLibrary
 mat=unreal.load_asset('/Game/REBoarGetch/Art/VFX/Capture/M_CaptureGlow');vfx=unreal.load_asset('/Game/REBoarGetch/Art/VFX/Capture/NS_CaptureSuccess');assert mat and vfx
 for path in ['/Game/BP/Player/BP_BoarPlayerController','/Game/BP/Lobby/BP_PC_Lobby']:
  bp=unreal.load_asset(path);assert bp
  cd=unreal.get_default_object(bp.generated_class());c=cd.get_component_by_class(unreal.CapturePresentationComponent);assert c,path
  c.set_editor_property('glow_material',mat);c.set_editor_property('capture_vfx',vfx)
  unreal.BlueprintEditorLibrary.compile_blueprint(bp);assert E.save_loaded_asset(bp)
  cd=unreal.get_default_object(bp.generated_class());c=cd.get_component_by_class(unreal.CapturePresentationComponent)
  out['blueprints'].append({'path':path,'component':c.get_path_name(),'glow':c.glow_material.get_path_name(),'vfx':c.capture_vfx.get_path_name(),'hit_stop':c.hit_stop_duration,'slow_duration':c.slow_duration,'slow_scale':c.slow_time_scale,'zoom':c.camera_zoom_amount,'camera_duration':c.camera_duration,'glow_duration':c.glow_duration,'impact_sound':str(c.impact_sound),'confirm_sound':str(c.capture_success_sound)})
 # Compile dependent visual/gameplay Blueprints without changing their existing fields.
 for path in ['/Game/BP/Boar/BP_NormalBoar','/Game/BP/Gadget/BP_NetGadget']:
  bp=unreal.load_asset(path);assert bp;unreal.BlueprintEditorLibrary.compile_blueprint(bp);assert E.save_loaded_asset(bp)
 out['PIE']=bool(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world())
 out['dirty_maps']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
 out['dirty_assets']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
except Exception:out['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
