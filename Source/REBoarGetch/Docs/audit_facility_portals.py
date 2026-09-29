import unreal,json
from pathlib import Path
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if '/Game/Level/L_Lobby.' not in w.get_path_name():raise RuntimeError('Wrong map')
gm=w.get_world_settings().get_editor_property('default_game_mode');d=unreal.get_default_object(gm)
out={'world':w.get_path_name(),'game_mode':gm.get_path_name(),'pawn':str(d.get_editor_property('default_pawn_class')),'controller':str(d.get_editor_property('player_controller_class')),'levels':unreal.EditorAssetLibrary.list_assets('/Game/Level',False),'actors':[]}
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_actor_label().startswith(('LobbyArt_Hub3_','Stage01_UIEntrance','LobbyPlayerStart','Gadget','Practice','Step','Port_Gadget','LobbyArt_Gadget','LobbyArt_Practice')):
        p=a.get_actor_location();out['actors'].append({'label':a.get_actor_label(),'class':a.get_class().get_path_name(),'p':[p.x,p.y,p.z]})
for p in ['/Game/BP/Widget/WBP_GadgetLoadout','/Game/BP/Widget/WBP_BoarEncyclopedia']:
    bp=unreal.load_asset(p);out[p]=str(bp.generated_class())
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2,default=str))

