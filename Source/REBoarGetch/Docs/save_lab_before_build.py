import unreal,json
from pathlib import Path
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
dirty_maps=list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
dirty_assets=list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
assert not dirty_maps,'Unexpected unsaved level: stop without closing'
assert all(p.get_name()=='/Game/InportAssets/GadgetPod' for p in dirty_assets),'Unrelated unsaved assets: stop without closing'
if dirty_assets:
 assert unreal.EditorAssetLibrary.save_asset('/Game/InportAssets/GadgetPod',True)
out={'maps':[str(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'assets':[str(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
Path(__file__).with_suffix('.json').write_text(json.dumps(out))
assert not out['maps'] and not out['assets']
unreal.SystemLibrary.execute_console_command(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),'QUIT_EDITOR')
