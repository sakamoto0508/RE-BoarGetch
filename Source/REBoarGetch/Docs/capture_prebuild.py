import unreal,json
from pathlib import Path
p='/Game/REBoarGetch/Art/VFX/Capture/NS_CaptureSuccess'
assert unreal.EditorAssetLibrary.save_asset(p)
out={'dirty_maps':[x.get_path_name() for x in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'dirty_assets':[x.get_path_name() for x in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
Path(__file__).with_suffix('.json').write_text(json.dumps(out))
assert not out['dirty_maps'] and not out['dirty_assets'],'Do not close with unrelated unsaved edits'
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.SystemLibrary.execute_console_command(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),'QUIT_EDITOR')
