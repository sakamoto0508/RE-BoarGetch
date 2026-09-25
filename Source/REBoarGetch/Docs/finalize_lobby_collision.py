import unreal
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if not a.get_actor_label().startswith('LobbyArt_'):continue
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        c.set_editor_property('use_default_collision',False)
        c.set_collision_profile_name('NoCollision')
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        c.set_editor_property('can_ever_affect_navigation',False)
unreal.EditorLevelLibrary.save_current_level()
exec(compile(open('C:/UnrealGames/REBoarGetch/Source/REBoarGetch/Docs/verify_lobby_visual.py',encoding='utf-8').read(),'verify_lobby_visual.py','exec'))
