import unreal
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_actor_label()=='Stage1Sign':
        c=a.get_components_by_class(unreal.TextRenderComponent)[0]
        unreal.log('SIGNFONT '+str(c.get_editor_property('font'))+' '+str(c.get_world_rotation()))
    if a.get_actor_label().startswith('LobbyArt_Hub3_') and isinstance(a,unreal.TextRenderActor):
        c=a.get_components_by_class(unreal.TextRenderComponent)[0]
        c.set_font(unreal.load_asset('/Engine/EngineFonts/RobotoDistanceField'))
        unreal.log('NEW_SIGN '+a.get_actor_label()+' '+str(c.get_editor_property('text')))
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
