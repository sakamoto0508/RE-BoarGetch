import unreal,json
from pathlib import Path
LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
out={}
for path in ['/Game/Level/L_GadgetLab','/Game/Level/L_Archive','/Game/Level/L_Lobby']:
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    assert LS.load_level(path)
    w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();gm=w.get_world_settings().get_editor_property('default_game_mode')
    d={'game_mode':str(gm),'portals':[],'targets':0}
    for a in EA.get_all_level_actors():
        if isinstance(a,unreal.HubPortal):
            t=a.get_editor_property('trigger');d['portals'].append({'name':a.get_actor_label(),'action':str(a.get_editor_property('action')),'destination':str(a.get_editor_property('destination')),'location':str(a.get_actor_location()),'extent':str(t.get_unscaled_box_extent()),'collision':str(t.get_collision_enabled())})
        if isinstance(a,unreal.GadgetTestTarget):d['targets']+=1
        if isinstance(a,unreal.PlayerStart):d['spawn']=str(a.get_actor_location())
        if a.get_actor_label()=='Stage01_UIEntrance':d['stage_entrance']={'location':str(a.get_actor_location()),'catalog':str(a.get_editor_property('stage_catalog')),'widget':str(a.get_editor_property('lobby_widget_class'))}
    out[path]=d
out['dirty_maps']=[str(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
