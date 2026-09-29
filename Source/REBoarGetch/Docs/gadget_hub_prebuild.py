import unreal,json
from pathlib import Path
out={'maps':[str(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'assets':[str(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
Path(__file__).with_suffix('.json').write_text(json.dumps(out),encoding='utf-8')
assert not out['maps'] and not out['assets'],'Unsaved user assets: do not close'
unreal.SystemLibrary.execute_console_command(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),'QUIT_EDITOR')
