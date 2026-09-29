import unreal,json,traceback
from pathlib import Path
E=unreal.EditorAssetLibrary;M=unreal.MaterialEditingLibrary
o={}
try:
 p='/Game/REBoarGetch/Art/VFX/Capture/M_CaptureDataTransfer';mat=unreal.load_asset(p)
 assert mat
 o['material']={'path':p,'blend':str(mat.get_editor_property('blend_mode')),'shading':str(mat.get_editor_property('shading_model')),'two_sided':mat.get_editor_property('two_sided'),'expressions':M.get_num_material_expressions(mat),'emissive_connected':bool(M.get_material_property_input_node(mat,unreal.MaterialProperty.MP_EMISSIVE_COLOR)),'opacity_mask_connected':bool(M.get_material_property_input_node(mat,unreal.MaterialProperty.MP_OPACITY_MASK))}
 o['controllers']=[]
 for p in ['/Game/BP/Player/BP_BoarPlayerController','/Game/BP/Lobby/BP_PC_Lobby']:
  bp=unreal.load_asset(p);assert bp
  c=unreal.get_default_object(bp.generated_class()).get_component_by_class(unreal.CapturePresentationComponent)
  o['controllers'].append({'path':p,'status':str(bp.get_editor_property('status')),'data_material':c.data_material.get_path_name() if c.data_material else None,'capture_vfx':c.capture_vfx.get_path_name() if c.capture_vfx else None,'cage_reception_vfx':c.cage_reception_vfx.get_path_name() if c.cage_reception_vfx else None,'presentation_duration':c.hit_stop_duration+c.slow_duration+c.capture_dissolve_duration+c.transfer_finish_duration,'reception_duration':c.cage_reception_duration,'impact_sound':str(c.impact_sound),'transfer_sound':str(c.transfer_start_sound),'confirm_sound':str(c.capture_success_sound)})
 boar=unreal.load_asset('/Game/BP/Boar/BP_NormalBoar');cd=unreal.get_default_object(boar.generated_class());mesh=cd.get_component_by_class(unreal.SkeletalMeshComponent)
 o['boar_materials']=[str(mesh.get_material(i)) for i in range(mesh.get_num_materials())]
 o['dirty_assets']=[x.get_path_name() for x in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
 o['dirty_maps']=[x.get_path_name() for x in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
 o['PIE']=bool(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world())
except Exception:o['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(o,indent=2))
