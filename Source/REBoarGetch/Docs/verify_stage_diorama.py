import unreal,json
from pathlib import Path
assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
bp=unreal.load_asset('/Game/BP/Widget/WBP_LobbyStageSelect');cdo=unreal.get_default_object(bp.generated_class());tree=unreal.find_object(bp,'WidgetTree')
preview=unreal.load_asset('/Game/UI/StagePreview/BP_Stage01_Diorama');pcdo=unreal.get_default_object(preview.generated_class())
stage=unreal.load_asset('/Game/DataAssets/StageConfig/DA_TestStageConfig')
for a in [bp,preview,stage]:
    if not assets.save_loaded_asset(a):raise RuntimeError('Save failed '+a.get_path_name())
out={'carousel':cdo.get_editor_property('use_carousel'),'preview_material':str(cdo.get_editor_property('preview_material')),'preview_class':str(stage.get_editor_property('preview_actor_class')),'parts':len(pcdo.get_editor_property('parts')),'catalog_visibility':str(unreal.find_object(tree,'CatalogPanel').get_visibility()),'input_previous':str(cdo.get_editor_property('previous_stage_keys')),'input_next':str(cdo.get_editor_property('next_stage_keys')),'input_cancel':str(cdo.get_editor_property('cancel_keys')),'dirty': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages() if any(s in p.get_path_name() for s in ['StagePreview','WBP_LobbyStageSelect','DA_TestStageConfig'])]}
assert out['carousel'] and out['parts']>0 and not out['dirty']
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
