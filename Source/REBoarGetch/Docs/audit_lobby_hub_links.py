import unreal,json
from pathlib import Path
out={}
for p in ['/Game/BP/Widget','/Game/UI']:
    for s in unreal.EditorAssetLibrary.list_assets(p,True,False):
        if any(k in s for k in ['LobbyStageSelect','Loadout','Encyclopedia','LobbyHUD']):
            a=unreal.load_asset(s);out[s]=a.get_class().get_name()
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_actor_label()=='Stage01_UIEntrance':
        out['entrance']={'class':a.get_class().get_path_name(),'widget':str(a.get_editor_property('lobby_widget_class')),'stage_catalog':str(a.get_editor_property('stage_catalog'))}
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
