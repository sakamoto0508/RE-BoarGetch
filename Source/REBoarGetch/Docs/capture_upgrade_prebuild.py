import unreal,json
from pathlib import Path
p='/Game/REBoarGetch/Art/VFX/Capture/M_CaptureDataTransfer'
assert unreal.EditorAssetLibrary.save_asset(p)
assert unreal.EditorAssetLibrary.save_asset('/Game/BP/Player/BP_BoarPlayerController')
out={'dirty_maps':[x.get_path_name() for x in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()], 'dirty_assets':[x.get_path_name() for x in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
assert not out['dirty_maps'] and not out['dirty_assets'], 'Unexpected unsaved editor changes'
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.SystemLibrary.execute_console_command(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),'QUIT_EDITOR')
