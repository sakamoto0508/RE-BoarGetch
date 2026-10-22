"""Move the redesign assets: level -> /Game/Level, meshes/MIs -> /Game/InportAssets/Stage01Redesign. Fixes referencers and redirectors."""
import unreal
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
OLD = '/Game/Stage01/Redesign'  # already moved on 2026-10-22; kept for reference
MOVES = {OLD + '/Maps': '/Game/Level', OLD + '/Meshes': '/Game/InportAssets/Stage01Redesign/Meshes',
         OLD + '/Materials': '/Game/InportAssets/Stage01Redesign/Materials'}
DA = '/Game/DataAssets/StageConfig/DA_TestStageConfig'
cfg = unreal.load_asset(DA)
# The StageConfig soft reference makes AssetTools ask an OK/Cancel dialog (Cancel when unattended).
# Point it at Stage1 for the move, then at the moved level afterwards.
cfg.set_editor_property('level', unreal.load_asset('/Game/Level/Stage1'))
assert eal.save_loaded_asset(cfg, False)
ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_modified_asset_files([DA])
data = []
for src, dst in MOVES.items():
    for p in eal.list_assets(src, recursive=False):
        obj = eal.load_asset(p)
        data.append(unreal.AssetRenameData(obj, dst, obj.get_name()))
        unreal.log('[S01] move %s -> %s' % (p, dst))
assert tools.rename_assets(data), 'rename failed'
# resave the level so it holds the new mesh paths, then point StageConfig at it again
assert eal.save_asset('/Game/Level/L_Stage01_Redesign', only_if_is_dirty=False)
cfg.set_editor_property('level', unreal.load_asset('/Game/Level/L_Stage01_Redesign'))
assert eal.save_loaded_asset(cfg, False)
# fix up any redirectors left in the old folder, then remove it
redirs = [eal.load_asset(p) for p in eal.list_assets(OLD, recursive=True)]
redirs = [r for r in redirs if isinstance(r, unreal.ObjectRedirector)]
if redirs:
    tools.fixup_referencers(redirs)
left = eal.list_assets(OLD, recursive=True)
unreal.log('[S01] left in old folder: %s' % left)
if not left:
    eal.delete_directory(OLD)
unreal.log('[S01] StageConfig level=%s' % cfg.get_editor_property('level').get_path_name())
unreal.log('[S01] COMPLETE')
