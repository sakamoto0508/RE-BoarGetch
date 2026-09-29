import unreal,json
from pathlib import Path
if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(): raise RuntimeError('PIE active')
if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():raise RuntimeError('Dirty map')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/Level/L_Lobby')
exec(Path(__file__).with_name('audit_lobby_visual.py').read_text().replace("Path(__file__).with_suffix('.json')","Path(__file__).with_name('audit_lobby_three_hubs.json')"))
