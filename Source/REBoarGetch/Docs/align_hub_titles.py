import unreal
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_actor_label() in ['LobbyArt_Hub3_GadgetTitle','LobbyArt_Hub3_ArchiveTitle']:
        p=a.get_actor_location();p.z=569;a.set_actor_location(p,False,False)
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
