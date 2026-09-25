import unreal
for path in ['/Game/Lobby/Materials/M_Lobby_CyanEdge']:
    m=unreal.load_asset(path);m.set_editor_property('two_sided',True);unreal.MaterialEditingLibrary.recompile_material(m);unreal.get_editor_subsystem(unreal.EditorAssetSubsystem).save_loaded_asset(m)
# Only this pass's new portal cyan strips need a two-sided material variant.
assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
path='/Game/Lobby/Materials/MI_Lobby_PortalCyan'
mi=assets.duplicate_asset('/Game/Lobby/Materials/MI_Lobby_Cyan',path)
over=mi.get_editor_property('base_property_overrides');over.set_editor_property('override_two_sided',True);over.set_editor_property('two_sided',True);mi.set_editor_property('base_property_overrides',over);assets.save_loaded_asset(mi)
mesh=unreal.load_asset('/Game/Lobby/Meshes/SM_LobbyRef_Cyan');mesh.set_material(0,mi);assets.save_loaded_asset(mesh)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
