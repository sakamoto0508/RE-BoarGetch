import unreal,json,traceback
from pathlib import Path
E=unreal.EditorAssetLibrary
o={'blueprints':[],'assets':{},'audio_options':[]}
try:
 base='/Game/REBoarGetch/Art/VFX/Capture/'
 glow=unreal.load_asset(base+'M_CaptureGlow')
 data=unreal.load_asset(base+'M_CaptureDataTransfer')
 vfx=unreal.load_asset(base+'NS_CaptureSuccess')
 assert glow and data and vfx
 for a in [glow,data,vfx]: o['assets'][a.get_path_name()]=a.get_class().get_name()
 for p in ['/Game/BP/Player/BP_BoarPlayerController','/Game/BP/Lobby/BP_PC_Lobby']:
  bp=unreal.load_asset(p);assert bp,p
  cd=unreal.get_default_object(bp.generated_class())
  c=cd.get_component_by_class(unreal.CapturePresentationComponent);assert c,p
  c.set_editor_property('glow_material',glow)
  c.set_editor_property('data_material',data)
  c.set_editor_property('capture_vfx',vfx)
  c.set_editor_property('cage_reception_vfx',vfx)
  c.set_editor_property('hit_stop_duration',.1)
  c.set_editor_property('slow_duration',.27)
  c.set_editor_property('capture_dissolve_duration',.3)
  c.set_editor_property('transfer_finish_duration',.18)
  c.set_editor_property('cage_reception_duration',.18)
  c.set_editor_property('camera_zoom_amount',12.)
  c.set_editor_property('camera_duration',.38)
  c.set_editor_property('fragment_count',16)
  c.set_editor_property('transfer_rise_distance',105.)
  unreal.BlueprintEditorLibrary.compile_blueprint(bp)
  assert E.save_loaded_asset(bp),p
  cd=unreal.get_default_object(bp.generated_class())
  c=cd.get_component_by_class(unreal.CapturePresentationComponent)
  o['blueprints'].append({'path':p,'status':str(bp.get_editor_property('status')),'data_material':str(c.data_material),'reception_vfx':str(c.cage_reception_vfx),'hit_stop':c.hit_stop_duration,'slow':c.slow_duration,'dissolve':c.capture_dissolve_duration,'finish':c.transfer_finish_duration,'reception':c.cage_reception_duration,'zoom':c.camera_zoom_amount,'fragments':c.fragment_count,'impact_sound':str(c.impact_sound),'transfer_sound':str(c.transfer_start_sound),'confirm_sound':str(c.capture_success_sound)})
 o['audio_options']=[p for p in E.list_assets('/Game/InportAssets/Audio',recursive=True,include_folder=False) if p.endswith('.SoundWave') or 'SoundCue' in p][:50] if E.does_directory_exist('/Game/InportAssets/Audio') else []
 o['dirty_maps']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
 o['dirty_assets']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
 o['PIE']=bool(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world())
except Exception:o['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(o,indent=2))
