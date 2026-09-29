import unreal
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    n=a.get_actor_label()
    if n.startswith('LobbyArt_Hub3_') and isinstance(a,unreal.TextRenderActor):
        unreal.log('BEFORE_ROT '+n+' '+str(a.get_actor_rotation()))
        yaw=90 if n in ['LobbyArt_Hub3_GadgetTitle','LobbyArt_Hub3_WorkshopSubtitle'] else -90
        a.set_actor_rotation(unreal.Rotator(pitch=0,yaw=yaw,roll=0),False)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
