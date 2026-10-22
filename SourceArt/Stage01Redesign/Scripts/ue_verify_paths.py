"""After the move: every placed mesh resolves, none points at the old folder, StageConfig points at the moved level."""
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert les.load_level('/Game/Level/L_Stage01')
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
new = old = missing = 0
for a in eas.get_all_level_actors():
    if isinstance(a, unreal.StaticMeshActor):
        m = a.static_mesh_component.static_mesh
        if m is None:
            missing += 1
        elif m.get_path_name().startswith('/Game/Stage01/Redesign'):
            old += 1
        elif m.get_path_name().startswith('/Game/InportAssets/Stage01Redesign'):
            new += 1
unreal.log('[S01] meshes new_path=%d old_path=%d missing=%d' % (new, old, missing))
ar = unreal.AssetRegistryHelpers.get_asset_registry()
unreal.log('[S01] assets left under /Game/Stage01/Redesign: %d' % len(ar.get_assets_by_path('/Game/Stage01/Redesign', recursive=True)))
cfg = unreal.load_asset('/Game/DataAssets/StageConfig/DA_TestStageConfig')
unreal.log('[S01] StageConfig level=%s' % cfg.get_editor_property('level').get_path_name())
unreal.log('[S01] COMPLETE')
