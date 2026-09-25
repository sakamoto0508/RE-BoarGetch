import unreal,json
from pathlib import Path
assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
es=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():raise RuntimeError('PIE active')
if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():raise RuntimeError('Unsaved map')
out={'textures':[],'surfaces':[]}
for p in assets.list_assets('/Game/Stage01/Ground',recursive=False,include_folder=False):
    a=unreal.load_asset(p)
    if isinstance(a,unreal.Texture2D):out['textures'].append({'path':p,'srgb':a.get_editor_property('srgb'),'compression':str(a.get_editor_property('compression_settings'))})
es.load_level('/Game/Level/Test')
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        for i,m in enumerate(c.get_materials()):
            if m and any(t in m.get_name().lower() for t in ['dirt','soil','earth','path']):
                out['surfaces'].append({'actor':a.get_actor_label(),'component':c.get_path_name(),'slot':i,'material':m.get_path_name()})
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
